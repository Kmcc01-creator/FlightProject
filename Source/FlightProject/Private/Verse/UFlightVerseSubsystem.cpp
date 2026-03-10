// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightCompileArtifacts.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexSimdExecutor.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Vex/FlightVexIr.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Swarm/SwarmSimulationTypes.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "uLang/Toolchain/Toolchain.h"
#include "uLang/Toolchain/ProgramBuildManager.h"
#include "uLang/SourceProject/SourceDataProject.h"
#include "VerseVM/VVMBytecodeEmitter.h"
#include "VerseVM/VVMInterpreter.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMOpResult.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMPlaceholder.h"
#include "VerseVM/VVMFalse.h"
#include "Vex/FlightVexVvmAssembler.h"
#include "Vex/FlightVexNativeRegistry.h"
#include "Vex/FlightVexSymbolRegistry.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightVerseSubsystem, Log, All);

namespace
{
	enum class ENativeValueType : uint8
	{
		Float,
		Int,
		Bool,
		Vec3
	};

	struct FNativeValue
	{
		ENativeValueType Type = ENativeValueType::Float;
		float FloatValue = 0.0f;
		int32 IntValue = 0;
		bool BoolValue = false;
		FVector3f Vec3Value = FVector3f::ZeroVector;
	};

	FNativeValue MakeFloat(const float Value)
	{
		FNativeValue Result;
		Result.Type = ENativeValueType::Float;
		Result.FloatValue = Value;
		Result.IntValue = static_cast<int32>(Value);
		Result.BoolValue = !FMath::IsNearlyZero(Value);
		return Result;
	}

	FNativeValue MakeInt(const int32 Value)
	{
		FNativeValue Result;
		Result.Type = ENativeValueType::Int;
		Result.IntValue = Value;
		Result.FloatValue = static_cast<float>(Value);
		Result.BoolValue = Value != 0;
		return Result;
	}

	FNativeValue MakeBool(const bool Value)
	{
		FNativeValue Result;
		Result.Type = ENativeValueType::Bool;
		Result.BoolValue = Value;
		Result.IntValue = Value ? 1 : 0;
		Result.FloatValue = Value ? 1.0f : 0.0f;
		return Result;
	}

	FNativeValue MakeVec3(const FVector3f& Value)
	{
		FNativeValue Result;
		Result.Type = ENativeValueType::Vec3;
		Result.Vec3Value = Value;
		Result.FloatValue = Value.Size();
		Result.BoolValue = !Value.IsNearlyZero();
		return Result;
	}

	float AsFloat(const FNativeValue& Value)
	{
		switch (Value.Type)
		{
		case ENativeValueType::Float: return Value.FloatValue;
		case ENativeValueType::Int: return static_cast<float>(Value.IntValue);
		case ENativeValueType::Bool: return Value.BoolValue ? 1.0f : 0.0f;
		case ENativeValueType::Vec3: return Value.Vec3Value.Size();
		default: return 0.0f;
		}
	}

	int32 AsInt(const FNativeValue& Value)
	{
		switch (Value.Type)
		{
		case ENativeValueType::Int: return Value.IntValue;
		case ENativeValueType::Bool: return Value.BoolValue ? 1 : 0;
		case ENativeValueType::Float: return static_cast<int32>(Value.FloatValue);
		case ENativeValueType::Vec3: return static_cast<int32>(Value.Vec3Value.Size());
		default: return 0;
		}
	}

	bool AsBool(const FNativeValue& Value)
	{
		switch (Value.Type)
		{
		case ENativeValueType::Bool: return Value.BoolValue;
		case ENativeValueType::Int: return Value.IntValue != 0;
		case ENativeValueType::Float: return !FMath::IsNearlyZero(Value.FloatValue);
		case ENativeValueType::Vec3: return !Value.Vec3Value.IsNearlyZero();
		default: return false;
		}
	}

	FVector3f AsVec3(const FNativeValue& Value)
	{
		switch (Value.Type)
		{
		case ENativeValueType::Vec3: return Value.Vec3Value;
		case ENativeValueType::Float: return FVector3f(Value.FloatValue);
		case ENativeValueType::Int: return FVector3f(static_cast<float>(Value.IntValue));
		case ENativeValueType::Bool: return FVector3f(Value.BoolValue ? 1.0f : 0.0f);
		default: return FVector3f::ZeroVector;
		}
	}

	FNativeValue CastToType(const FNativeValue& Value, const FString& TypeName)
	{
		if (TypeName == TEXT("float"))
		{
			return MakeFloat(AsFloat(Value));
		}
		if (TypeName == TEXT("int"))
		{
			return MakeInt(AsInt(Value));
		}
		if (TypeName == TEXT("bool"))
		{
			return MakeBool(AsBool(Value));
		}
		if (TypeName == TEXT("float3"))
		{
			return MakeVec3(AsVec3(Value));
		}
		return Value;
	}

	const void* GetDroidStateTypeKey()
	{
		return Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	}
}

	bool IsSupportedNativeFunction(const FString& FunctionName)
	{
		return FunctionName == TEXT("normalize")
			|| FunctionName == TEXT("dot")
			|| FunctionName == TEXT("clamp")
			|| FunctionName == TEXT("smoothstep")
			|| FunctionName == TEXT("sin")
			|| FunctionName == TEXT("cos")
			|| FunctionName == TEXT("log")
			|| FunctionName == TEXT("exp")
			|| FunctionName == TEXT("pow")
			|| FunctionName.StartsWith(TEXT("vec"));
	}

	bool CanCompileNativeFallback(const Flight::Vex::FVexProgramAst& Program, FString& OutReason)
	{
		for (const Flight::Vex::FVexStatementAst& Statement : Program.Statements)
		{
			if (Statement.Kind == Flight::Vex::EVexStatementKind::TargetDirective && Statement.SourceSpan == TEXT("@async"))
			{
				OutReason = TEXT("@async behavior execution is not yet supported by the native fallback.");
				return false;
			}
		}

		for (const Flight::Vex::FVexExpressionAst& Expression : Program.Expressions)
		{
			if (Expression.Kind == Flight::Vex::EVexExprKind::FunctionCall && !IsSupportedNativeFunction(Expression.Lexeme))
			{
				OutReason = FString::Printf(TEXT("Unsupported function '%s' in native fallback."), *Expression.Lexeme);
				return false;
			}
		}

		return true;
	}

	FString BuildCompileTargetFingerprint()
	{
		return FString::Printf(
			TEXT("%hs|WITH_VERSE_VM=%d"),
			FPlatformProperties::IniPlatformName(),
			WITH_VERSE_VM ? 1 : 0);
	}

	FString CompileStateToString(const EFlightVerseCompileState CompileState)
	{
		switch (CompileState)
		{
		case EFlightVerseCompileState::GeneratedOnly:
			return TEXT("GeneratedOnly");
		case EFlightVerseCompileState::VmCompiled:
			return TEXT("VmCompiled");
		case EFlightVerseCompileState::VmCompileFailed:
			return TEXT("VmCompileFailed");
		default:
			return TEXT("Unknown");
		}
	}

	FNativeValue ReadSymbolValue(const FString& SymbolName, const Flight::Swarm::FDroidState& DroidState)
	{
		if (SymbolName == TEXT("@position")) return MakeVec3(DroidState.Position);
		if (SymbolName == TEXT("@velocity")) return MakeVec3(DroidState.Velocity);
		if (SymbolName == TEXT("@shield")) return MakeFloat(DroidState.Shield);
		if (SymbolName == TEXT("@status")) return MakeInt(static_cast<int32>(DroidState.Status));
		if (SymbolName == TEXT("@dt")) return MakeFloat(0.0f);
		if (SymbolName == TEXT("@frame")) return MakeInt(0);
		if (SymbolName == TEXT("@time")) return MakeFloat(0.0f);
		return MakeFloat(0.0f);
	}

	void WriteSymbolValue(const FString& SymbolName, const FNativeValue& Value, Flight::Swarm::FDroidState& DroidState)
	{
		if (SymbolName == TEXT("@position")) DroidState.Position = AsVec3(Value);
		else if (SymbolName == TEXT("@velocity")) DroidState.Velocity = AsVec3(Value);
		else if (SymbolName == TEXT("@shield")) DroidState.Shield = AsFloat(Value);
		else if (SymbolName == TEXT("@status")) DroidState.Status = static_cast<uint32>(FMath::Max(0, AsInt(Value)));
	}

	FNativeValue ApplyArithmetic(const FString& OperatorLexeme, const FNativeValue& Left, const FNativeValue& Right)
	{
		if (Left.Type == ENativeValueType::Vec3 || Right.Type == ENativeValueType::Vec3)
		{
			const FVector3f L = AsVec3(Left);
			const FVector3f R = AsVec3(Right);
			if (OperatorLexeme == TEXT("+")) return MakeVec3(L + R);
			if (OperatorLexeme == TEXT("-")) return MakeVec3(L - R);
			if (OperatorLexeme == TEXT("*")) return MakeVec3(L * R);
			if (OperatorLexeme == TEXT("/")) return MakeVec3(FVector3f(
				FMath::IsNearlyZero(R.X) ? L.X : L.X / R.X,
				FMath::IsNearlyZero(R.Y) ? L.Y : L.Y / R.Y,
				FMath::IsNearlyZero(R.Z) ? L.Z : L.Z / R.Z));
			return Left;
		}

		const float L = AsFloat(Left);
		const float R = AsFloat(Right);
		if (OperatorLexeme == TEXT("+")) return MakeFloat(L + R);
		if (OperatorLexeme == TEXT("-")) return MakeFloat(L - R);
		if (OperatorLexeme == TEXT("*")) return MakeFloat(L * R);
		if (OperatorLexeme == TEXT("/")) return MakeFloat(FMath::IsNearlyZero(R) ? 0.0f : (L / R));
		return Left;
	}

	FNativeValue EvaluateFunction(
		const FString& FunctionName,
		const TArray<FNativeValue>& Arguments)
	{
		if (FunctionName.StartsWith(TEXT("vec")))
		{
			const float X = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			const float Y = Arguments.Num() > 1 ? AsFloat(Arguments[1]) : 0.0f;
			const float Z = Arguments.Num() > 2 ? AsFloat(Arguments[2]) : 0.0f;
			return MakeVec3(FVector3f(X, Y, Z));
		}

		if (FunctionName == TEXT("sin"))
		{
			const float Value = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			return MakeFloat(FMath::Sin(Value));
		}

		if (FunctionName == TEXT("cos"))
		{
			const float Value = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			return MakeFloat(FMath::Cos(Value));
		}

		if (FunctionName == TEXT("log"))
		{
			const float Value = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			return MakeFloat(FMath::Loge(FMath::Max(UE_SMALL_NUMBER, Value)));
		}

		if (FunctionName == TEXT("exp"))
		{
			const float Value = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			return MakeFloat(FMath::Exp(Value));
		}

		if (FunctionName == TEXT("pow"))
		{
			const float Base = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			const float Exp = Arguments.Num() > 1 ? AsFloat(Arguments[1]) : 1.0f;
			return MakeFloat(FMath::Pow(Base, Exp));
		}

		if (FunctionName == TEXT("normalize"))
		{
			const FVector3f Value = Arguments.Num() > 0 ? AsVec3(Arguments[0]) : FVector3f::ZeroVector;
			return MakeVec3(Value.IsNearlyZero() ? FVector3f::ZeroVector : Value.GetSafeNormal());
		}

		if (FunctionName == TEXT("dot"))
		{
			const FVector3f A = Arguments.Num() > 0 ? AsVec3(Arguments[0]) : FVector3f::ZeroVector;
			const FVector3f B = Arguments.Num() > 1 ? AsVec3(Arguments[1]) : FVector3f::ZeroVector;
			return MakeFloat(FVector3f::DotProduct(A, B));
		}

		if (FunctionName == TEXT("clamp"))
		{
			const float Value = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			const float MinV = Arguments.Num() > 1 ? AsFloat(Arguments[1]) : 0.0f;
			const float MaxV = Arguments.Num() > 2 ? AsFloat(Arguments[2]) : 1.0f;
			return MakeFloat(FMath::Clamp(Value, MinV, MaxV));
		}

		if (FunctionName == TEXT("smoothstep"))
		{
			const float MinV = Arguments.Num() > 0 ? AsFloat(Arguments[0]) : 0.0f;
			const float MaxV = Arguments.Num() > 1 ? AsFloat(Arguments[1]) : 1.0f;
			const float Value = Arguments.Num() > 2 ? AsFloat(Arguments[2]) : 0.0f;
			const float T = FMath::Clamp((Value - MinV) / FMath::Max(UE_SMALL_NUMBER, MaxV - MinV), 0.0f, 1.0f);
			return MakeFloat(T * T * (3.0f - (2.0f * T)));
		}

		return MakeFloat(0.0f);
	}

	FNativeValue EvaluateExpression(
		const int32 NodeIdx,
		const Flight::Vex::FVexProgramAst& Program,
		const Flight::Swarm::FDroidState& DroidState,
		const TMap<FString, FNativeValue>& Locals)
	{
		if (!Program.Expressions.IsValidIndex(NodeIdx))
		{
			return MakeFloat(0.0f);
		}

		const Flight::Vex::FVexExpressionAst& Node = Program.Expressions[NodeIdx];
		switch (Node.Kind)
		{
		case Flight::Vex::EVexExprKind::NumberLiteral:
			return Node.Lexeme.Contains(TEXT(".")) ? MakeFloat(FCString::Atof(*Node.Lexeme)) : MakeInt(FCString::Atoi(*Node.Lexeme));
		case Flight::Vex::EVexExprKind::SymbolRef:
			return ReadSymbolValue(Node.Lexeme, DroidState);
		case Flight::Vex::EVexExprKind::Identifier:
		{
			if (const FNativeValue* Local = Locals.Find(Node.Lexeme))
			{
				return *Local;
			}
			return MakeFloat(0.0f);
		}
		case Flight::Vex::EVexExprKind::UnaryOp:
		{
			const FNativeValue Operand = EvaluateExpression(Node.LeftNodeIndex, Program, DroidState, Locals);
			if (Node.Lexeme == TEXT("-")) return MakeFloat(-AsFloat(Operand));
			if (Node.Lexeme == TEXT("!")) return MakeBool(!AsBool(Operand));
			return Operand;
		}
		case Flight::Vex::EVexExprKind::BinaryOp:
		{
			const FNativeValue Left = EvaluateExpression(Node.LeftNodeIndex, Program, DroidState, Locals);

			if (Node.Lexeme == TEXT("."))
			{
				if (!Program.Expressions.IsValidIndex(Node.RightNodeIndex))
				{
					return MakeFloat(0.0f);
				}

				const Flight::Vex::FVexExpressionAst& RightNode = Program.Expressions[Node.RightNodeIndex];
				if (Left.Type != ENativeValueType::Vec3 || RightNode.Kind != Flight::Vex::EVexExprKind::Identifier)
				{
					return MakeFloat(0.0f);
				}

				if (RightNode.Lexeme == TEXT("x")) return MakeFloat(Left.Vec3Value.X);
				if (RightNode.Lexeme == TEXT("y")) return MakeFloat(Left.Vec3Value.Y);
				if (RightNode.Lexeme == TEXT("z")) return MakeFloat(Left.Vec3Value.Z);
				return MakeFloat(0.0f);
			}

			const FNativeValue Right = EvaluateExpression(Node.RightNodeIndex, Program, DroidState, Locals);
			if (Node.Lexeme == TEXT("==")) return MakeBool(FMath::IsNearlyEqual(AsFloat(Left), AsFloat(Right)));
			if (Node.Lexeme == TEXT("!=")) return MakeBool(!FMath::IsNearlyEqual(AsFloat(Left), AsFloat(Right)));
			if (Node.Lexeme == TEXT("<")) return MakeBool(AsFloat(Left) < AsFloat(Right));
			if (Node.Lexeme == TEXT(">")) return MakeBool(AsFloat(Left) > AsFloat(Right));
			if (Node.Lexeme == TEXT("<=")) return MakeBool(AsFloat(Left) <= AsFloat(Right));
			if (Node.Lexeme == TEXT(">=")) return MakeBool(AsFloat(Left) >= AsFloat(Right));
			if (Node.Lexeme == TEXT("&&")) return MakeBool(AsBool(Left) && AsBool(Right));
			if (Node.Lexeme == TEXT("||")) return MakeBool(AsBool(Left) || AsBool(Right));
			return ApplyArithmetic(Node.Lexeme, Left, Right);
		}
		case Flight::Vex::EVexExprKind::FunctionCall:
		{
			TArray<FNativeValue> Args;
			Args.Reserve(Node.ArgumentNodeIndices.Num());
			for (const int32 ArgIdx : Node.ArgumentNodeIndices)
			{
				Args.Add(EvaluateExpression(ArgIdx, Program, DroidState, Locals));
			}
			return EvaluateFunction(Node.Lexeme, Args);
		}
		case Flight::Vex::EVexExprKind::Pipe:
		{
			const FNativeValue Piped = EvaluateExpression(Node.LeftNodeIndex, Program, DroidState, Locals);
			if (Program.Expressions.IsValidIndex(Node.RightNodeIndex))
			{
				const Flight::Vex::FVexExpressionAst& RightNode = Program.Expressions[Node.RightNodeIndex];
				if (RightNode.Kind == Flight::Vex::EVexExprKind::FunctionCall)
				{
					TArray<FNativeValue> Args;
					Args.Reserve(RightNode.ArgumentNodeIndices.Num() + 1);
					Args.Add(Piped);
					for (const int32 ArgIdx : RightNode.ArgumentNodeIndices)
					{
						Args.Add(EvaluateExpression(ArgIdx, Program, DroidState, Locals));
					}
					return EvaluateFunction(RightNode.Lexeme, Args);
				}
			}
			return Piped;
		}
		default:
			return MakeFloat(0.0f);
		}
	}

	void SetMemberComponent(FNativeValue& Target, const FString& Member, const FNativeValue& Value)
	{
		if (Target.Type != ENativeValueType::Vec3)
		{
			return;
		}

		const float Component = AsFloat(Value);
		if (Member == TEXT("x")) Target.Vec3Value.X = Component;
		else if (Member == TEXT("y")) Target.Vec3Value.Y = Component;
		else if (Member == TEXT("z")) Target.Vec3Value.Z = Component;
	}

	void ExecuteNativeProgram(const Flight::Vex::FVexProgramAst& Program, Flight::Swarm::FDroidState& DroidState)
	{
		TMap<FString, FNativeValue> Locals;
		bool bExecuteCurrent = true;
		TArray<bool> BlockExecutionStack;
		TOptional<bool> PendingIfResult;

		for (const Flight::Vex::FVexStatementAst& Statement : Program.Statements)
		{
			switch (Statement.Kind)
			{
			case Flight::Vex::EVexStatementKind::TargetDirective:
				continue;
			case Flight::Vex::EVexStatementKind::IfHeader:
			{
				const FNativeValue Cond = EvaluateExpression(Statement.ExpressionNodeIndex, Program, DroidState, Locals);
				PendingIfResult = bExecuteCurrent && AsBool(Cond);
				continue;
			}
			case Flight::Vex::EVexStatementKind::BlockOpen:
			{
				BlockExecutionStack.Add(bExecuteCurrent);
				if (PendingIfResult.IsSet())
				{
					bExecuteCurrent = PendingIfResult.GetValue();
					PendingIfResult.Reset();
				}
				continue;
			}
			case Flight::Vex::EVexStatementKind::BlockClose:
			{
				if (BlockExecutionStack.Num() > 0)
				{
					bExecuteCurrent = BlockExecutionStack.Pop(EAllowShrinking::No);
				}
				continue;
			}
			case Flight::Vex::EVexStatementKind::Expression:
				if (bExecuteCurrent)
				{
					(void)EvaluateExpression(Statement.ExpressionNodeIndex, Program, DroidState, Locals);
				}
				continue;
			case Flight::Vex::EVexStatementKind::Assignment:
				break;
			}

			if (!bExecuteCurrent || !Program.Tokens.IsValidIndex(Statement.StartTokenIndex) || !Program.Tokens.IsValidIndex(Statement.EndTokenIndex))
			{
				continue;
			}

			int32 OpIdx = INDEX_NONE;
			for (int32 TokenIndex = Statement.StartTokenIndex; TokenIndex <= Statement.EndTokenIndex; ++TokenIndex)
			{
				if (Program.Tokens[TokenIndex].Kind == Flight::Vex::EVexTokenKind::Operator
					&& (Program.Tokens[TokenIndex].Lexeme == TEXT("=")
						|| Program.Tokens[TokenIndex].Lexeme == TEXT("+=")
						|| Program.Tokens[TokenIndex].Lexeme == TEXT("-=")
						|| Program.Tokens[TokenIndex].Lexeme == TEXT("*=")
						|| Program.Tokens[TokenIndex].Lexeme == TEXT("/=")))
				{
					OpIdx = TokenIndex;
					break;
				}
			}

			if (OpIdx == INDEX_NONE)
			{
				continue;
			}

			const FString OpLexeme = Program.Tokens[OpIdx].Lexeme;
			const FNativeValue RhsValue = EvaluateExpression(Statement.ExpressionNodeIndex, Program, DroidState, Locals);

			const Flight::Vex::FVexToken& LhsToken = Program.Tokens[Statement.StartTokenIndex];
			if (LhsToken.Kind == Flight::Vex::EVexTokenKind::Identifier
				&& Statement.StartTokenIndex + 1 < OpIdx
				&& Program.Tokens[Statement.StartTokenIndex + 1].Kind == Flight::Vex::EVexTokenKind::Identifier)
			{
				const FString DeclType = LhsToken.Lexeme;
				const FString VariableName = Program.Tokens[Statement.StartTokenIndex + 1].Lexeme;
				Locals.Add(VariableName, CastToType(RhsValue, DeclType));
				continue;
			}

			FString MemberSuffix;
			if (Statement.StartTokenIndex + 2 < OpIdx
				&& Program.Tokens[Statement.StartTokenIndex + 1].Kind == Flight::Vex::EVexTokenKind::Dot
				&& Program.Tokens[Statement.StartTokenIndex + 2].Kind == Flight::Vex::EVexTokenKind::Identifier)
			{
				MemberSuffix = Program.Tokens[Statement.StartTokenIndex + 2].Lexeme;
			}

			if (LhsToken.Kind == Flight::Vex::EVexTokenKind::Symbol)
			{
				FNativeValue LhsValue = ReadSymbolValue(LhsToken.Lexeme, DroidState);
				if (!MemberSuffix.IsEmpty())
				{
					LhsValue = MakeFloat(MemberSuffix == TEXT("x") ? LhsValue.Vec3Value.X : (MemberSuffix == TEXT("y") ? LhsValue.Vec3Value.Y : LhsValue.Vec3Value.Z));
				}

				FNativeValue ResultValue = RhsValue;
				if (OpLexeme != TEXT("="))
				{
					const FString ArithmeticOp = OpLexeme.LeftChop(1);
					ResultValue = ApplyArithmetic(ArithmeticOp, LhsValue, RhsValue);
				}

				if (MemberSuffix.IsEmpty())
				{
					WriteSymbolValue(LhsToken.Lexeme, ResultValue, DroidState);
				}
				else
				{
					FNativeValue BaseValue = ReadSymbolValue(LhsToken.Lexeme, DroidState);
					SetMemberComponent(BaseValue, MemberSuffix, ResultValue);
					WriteSymbolValue(LhsToken.Lexeme, BaseValue, DroidState);
				}
				continue;
			}

			if (LhsToken.Kind == Flight::Vex::EVexTokenKind::Identifier)
			{
				FNativeValue LhsValue = Locals.FindRef(LhsToken.Lexeme);
				FNativeValue ResultValue = RhsValue;
				if (OpLexeme != TEXT("="))
				{
					const FString ArithmeticOp = OpLexeme.LeftChop(1);
					ResultValue = ApplyArithmetic(ArithmeticOp, LhsValue, RhsValue);
				}
				Locals.Add(LhsToken.Lexeme, ResultValue);
			}
		}
	}

void UFlightVerseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cache symbol definitions for VEX parsing
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
	SymbolDefinitions = Flight::Vex::BuildSymbolDefinitionsFromManifest(Manifest);

	// Build Verse projections for our components
	RegisterNativeComponents();

	// Register VEX @symbols and built-in functions
	RegisterNativeVerseSymbols();
	RegisterNativeVerseFunctions();
}

void UFlightVerseSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool UFlightVerseSubsystem::CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, UScriptStruct* TargetStruct)
{
	// Initialize behavior state pessimistically; flip to executable only when a runnable path is available.
	FVerseBehavior& Behavior = Behaviors.FindOrAdd(BehaviorID);
	Behavior.CompileState = EFlightVerseCompileState::VmCompileFailed;
	Behavior.bHasExecutableProcedure = false;
	Behavior.bUsesNativeFallback = false;
	Behavior.bUsesVmEntryPoint = false;
	Behavior.NativeProgram = Flight::Vex::FVexProgramAst();
	Behavior.GeneratedVerseCode.Reset();
	Behavior.LastCompileDiagnostics.Reset();
	Behavior.SimdCompileDiagnostics.Reset();
	Behavior.CompileArtifactReport = Flight::Vex::FFlightCompileArtifactReport();
	Behavior.bHasCompileArtifactReport = false;

	Flight::Vex::FFlightCompileArtifactReport ArtifactReport;
	ArtifactReport.GeneratedAtUtc = FDateTime::UtcNow();
	ArtifactReport.BehaviorID = BehaviorID;
	ArtifactReport.SourceHash = FCrc::StrCrc32(*VexSource);
	ArtifactReport.TargetFingerprint = BuildCompileTargetFingerprint();

	const double CompileStartTime = FPlatformTime::Seconds();
	FString BackendPath = TEXT("GeneratedPreviewOnly");
	FString IrErrors;
	TSharedPtr<Flight::Vex::FVexIrProgram> IrProgram;
	double ParseTimeMs = 0.0;
	double IrCompileTimeMs = 0.0;
	double VmAssembleTimeMs = 0.0;

	auto FinalizeArtifactReport = [&]()
	{
		ArtifactReport.GeneratedAtUtc = FDateTime::UtcNow();
		ArtifactReport.CompileOutcome = CompileStateToString(Behavior.CompileState);
		ArtifactReport.BackendPath = BackendPath;
		ArtifactReport.Diagnostics = Behavior.LastCompileDiagnostics;
		ArtifactReport.GeneratedVerseCode = Behavior.GeneratedVerseCode;
		ArtifactReport.IrCompileErrors = IrErrors;
		ArtifactReport.Tier = Behavior.Tier;
		ArtifactReport.bAsync = Behavior.bIsAsync;
		ArtifactReport.bHasIr = IrProgram.IsValid();
		ArtifactReport.bUsesNativeFallback = Behavior.bUsesNativeFallback;
		ArtifactReport.bUsesVmEntryPoint = Behavior.bUsesVmEntryPoint;
		ArtifactReport.AvailableArtifacts.Reset();
		ArtifactReport.AvailableArtifacts.Add(Flight::Vex::EFlightCompileArtifactKind::CompileTelemetry);
		if (IrProgram.IsValid())
		{
			ArtifactReport.AvailableArtifacts.Add(Flight::Vex::EFlightCompileArtifactKind::IrProgram);
			ArtifactReport.CodeShapeMetrics = Flight::Vex::AnalyzeIrCodeShape(*IrProgram, Behavior.bIsAsync);
		}
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		if (Behavior.Procedure.Get())
		{
			ArtifactReport.AvailableArtifacts.Add(Flight::Vex::EFlightCompileArtifactKind::VvmBytecode);
		}
#endif
		ArtifactReport.WarmupMetrics.ParseTimeMs = ParseTimeMs;
		ArtifactReport.WarmupMetrics.IrCompileTimeMs = IrCompileTimeMs;
		ArtifactReport.WarmupMetrics.VmAssembleTimeMs = VmAssembleTimeMs;
		ArtifactReport.WarmupMetrics.TotalCompileTimeMs = (FPlatformTime::Seconds() - CompileStartTime) * 1000.0;
		ArtifactReport.WarmupMetrics.CacheFillCount = Behavior.bHasExecutableProcedure ? 1 : 0;
		ArtifactReport.WarmupMetrics.RegistryMissCount = Behavior.bUsesVmEntryPoint ? 0 : (Behavior.bUsesNativeFallback ? 1 : 0);
		Flight::Vex::FinalizeWarmupMetrics(ArtifactReport.WarmupMetrics);
		Behavior.CompileArtifactReport = ArtifactReport;
		Behavior.bHasCompileArtifactReport = true;
	};

	// 1. Resolve Symbol Definitions for this compilation
	TArray<Flight::Vex::FVexSymbolDefinition> LocalDefinitions = SymbolDefinitions;
	if (TargetStruct)
	{
		if (const Flight::Vex::FVexTypeSchema* Schema = Flight::Vex::FVexSymbolRegistry::Get().GetSchema(TargetStruct))
		{
			for (auto& It : Schema->Symbols)
			{
				const auto& Accessor = It.Value;
				if (LocalDefinitions.ContainsByPredicate([SymbolName = Accessor.SymbolName](const Flight::Vex::FVexSymbolDefinition& D) { return D.SymbolName == SymbolName; }))
				{
					continue;
				}

				Flight::Vex::FVexSymbolDefinition Def;
				Def.SymbolName = Accessor.SymbolName;
				Def.ValueType = TEXT("float");
				Def.Residency = Accessor.Residency;
				Def.bWritable = Accessor.Setter != nullptr;
				Def.bRequired = false;
				LocalDefinitions.Add(Def);
			}
		}
	}

	const double ParseStartTime = FPlatformTime::Seconds();
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, LocalDefinitions, false);
	ParseTimeMs = (FPlatformTime::Seconds() - ParseStartTime) * 1000.0;

	if (Result.Issues.Num() > 0)
	{
		for (const auto& Issue : Result.Issues)
		{
			const TCHAR* SeverityLabel = Issue.Severity == Flight::Vex::EVexIssueSeverity::Error ? TEXT("Error") : TEXT("Warning");
			OutErrors += FString::Printf(TEXT("VEX %s: %s at %d:%d\n"), SeverityLabel, *Issue.Message, Issue.Line, Issue.Column);
		}
		if (!Result.bSuccess)
		{
			Behavior.LastCompileDiagnostics = OutErrors;
			BackendPath = TEXT("ParseValidationOnly");
			FinalizeArtifactReport();
			return false;
		}
	}

	// Enforce required-symbol contract only if we're using the default swarm definitions
	if (!TargetStruct)
	{
		TSet<FString> UsedSymbols;
		for (const FString& Symbol : Result.Program.UsedSymbols)
		{
			UsedSymbols.Add(Symbol);
		}

		TArray<FString> MissingRequiredSymbols;
		for (const Flight::Vex::FVexSymbolDefinition& Def : LocalDefinitions)
		{
			if (Def.bRequired && !UsedSymbols.Contains(Def.SymbolName))
			{
				MissingRequiredSymbols.Add(Def.SymbolName);
			}
		}
		MissingRequiredSymbols.Sort();

		if (MissingRequiredSymbols.Num() > 0)
		{
			OutErrors += FString::Printf(
				TEXT("VEX Contract Error: missing required symbols: %s\n"),
				*FString::Join(MissingRequiredSymbols, TEXT(", ")));
			Behavior.LastCompileDiagnostics = OutErrors;
			BackendPath = TEXT("ContractValidationOnly");
			FinalizeArtifactReport();
			return false;
		}
	}

	// Store behavior metadata regardless of VM availability.
	Behavior.ExecutionRateHz = Result.Program.ExecutionRateHz;
	Behavior.FrameInterval = Result.Program.FrameInterval;
	Behavior.Tier = Flight::Vex::FVexOptimizer::ClassifyTier(Result.Program, LocalDefinitions);

	// Always generate IR for VVM assembly if possible
	const double IrCompileStartTime = FPlatformTime::Seconds();
	IrProgram = Flight::Vex::FVexIrCompiler::Compile(Result.Program, LocalDefinitions, IrErrors);
	IrCompileTimeMs = (FPlatformTime::Seconds() - IrCompileStartTime) * 1000.0;

	if (Behavior.Tier == Flight::Vex::EVexTier::Literal)
	{
		FString SimdReason;
		const bool bSimdEligible = Flight::Vex::FVexSimdExecutor::CanCompileProgram(Result.Program, SymbolDefinitions, &SimdReason);
		if (bSimdEligible && IrProgram.IsValid())
		{
			Behavior.SimdPlan = Flight::Vex::FVexSimdExecutor::Compile(IrProgram);
			Behavior.SimdCompileDiagnostics = Behavior.SimdPlan.IsValid()
				? FString::Printf(TEXT("SIMD Tier 1 plan compiled successfully via IR. (%d instructions, %d registers)"), IrProgram->Instructions.Num(), IrProgram->MaxRegisters)
				: TEXT("SIMD Tier 1 IR generation passed, but plan generation failed.");
		}
		else
		{
			Behavior.SimdPlan.Reset();
			Behavior.SimdCompileDiagnostics = SimdReason.IsEmpty()
				? TEXT("SIMD unavailable: Tier 1 program is not compatible with current SIMD backend.")
				: FString::Printf(TEXT("SIMD unavailable: %s"), *SimdReason);
		}
	}
	else
	{
		Behavior.SimdPlan.Reset();
		Behavior.SimdCompileDiagnostics = TEXT("SIMD skipped: script classified as Tier 2/3.");
	}

	auto AppendSimdCompileDiagnostics = [&Behavior](FString& Message)
	{
		if (!Behavior.SimdCompileDiagnostics.IsEmpty())
		{
			if (!Message.IsEmpty())
			{
				Message += TEXT("\n");
			}
			Message += Behavior.SimdCompileDiagnostics;
		}
	};

	// Check for @async
	Behavior.bIsAsync = false;
	for (const auto& S : Result.Program.Statements)
	{
		if (S.Kind == Flight::Vex::EVexStatementKind::TargetDirective && S.SourceSpan == TEXT("@async"))
		{
			Behavior.bIsAsync = true;
			break;
		}
	}

	// Generate the Verse source code from the VEX AST
	TMap<FString, FString> VerseBySymbol;
	for (const auto& Def : SymbolDefinitions)
	{
		VerseBySymbol.Add(Def.SymbolName, Def.SymbolName.Mid(1)); // Fallback: just remove '@'
	}

	const FString VerseCode = Flight::Vex::LowerToVerse(Result.Program, SymbolDefinitions, VerseBySymbol);
	Behavior.GeneratedVerseCode = VerseCode;
	Behavior.bHasExecutableProcedure = false;
	Behavior.CompileState = EFlightVerseCompileState::GeneratedOnly;

	FString NativeFallbackReason;
	if (CanCompileNativeFallback(Result.Program, NativeFallbackReason))
	{
		Behavior.NativeProgram = Result.Program;
		Behavior.bUsesNativeFallback = true;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		FString VmDiagnostics;
		const double VmAssembleStartTime = FPlatformTime::Seconds();
		const bool bVerseSourceValid = TryValidateVerseSource(VerseCode, VmDiagnostics);
		const bool bVmProcedureReady = bVerseSourceValid && TryCreateVmProcedure(BehaviorID, Behavior, IrProgram, VmDiagnostics);
		VmAssembleTimeMs += (FPlatformTime::Seconds() - VmAssembleStartTime) * 1000.0;
		if (bVmProcedureReady)
		{
			Behavior.bUsesVmEntryPoint = true;
			Behavior.bHasExecutableProcedure = true;
			Behavior.CompileState = EFlightVerseCompileState::VmCompiled;
			Behavior.LastCompileDiagnostics =
				TEXT("Compiled to Verse VM procedure; native fallback retained as safety path.");
			AppendSimdCompileDiagnostics(Behavior.LastCompileDiagnostics);
			if (!VmDiagnostics.IsEmpty())
			{
				Behavior.LastCompileDiagnostics += TEXT("\n") + VmDiagnostics;
			}

			OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"),
				BehaviorID, Behavior.ExecutionRateHz, Behavior.FrameInterval, Behavior.bIsAsync ? TEXT("[Async]") : TEXT(""));
			OutErrors += Behavior.LastCompileDiagnostics + TEXT("\n");
			OutErrors += TEXT("Generated Verse:\n") + VerseCode;
			BackendPath = TEXT("VerseVmProcedure+NativeFallback");
			FinalizeArtifactReport();
			return true;
		}

		if (!VmDiagnostics.IsEmpty())
		{
			OutErrors += VmDiagnostics + TEXT("\n");
		}
#endif

		Behavior.bHasExecutableProcedure = true;
		Behavior.CompileState = EFlightVerseCompileState::VmCompiled;
		Behavior.LastCompileDiagnostics =
			TEXT("Compiled to native fallback executor; Verse VM execution path remains pending.");
		AppendSimdCompileDiagnostics(Behavior.LastCompileDiagnostics);

		OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"),
			BehaviorID, Behavior.ExecutionRateHz, Behavior.FrameInterval, Behavior.bIsAsync ? TEXT("[Async]") : TEXT(""));
		OutErrors += Behavior.LastCompileDiagnostics + TEXT("\n");
		OutErrors += TEXT("Generated Verse:\n") + VerseCode;
		BackendPath = TEXT("NativeFallback");
		FinalizeArtifactReport();
		return true;
	}

	const FString FallbackPrefix = NativeFallbackReason.IsEmpty() ? FString() : (NativeFallbackReason + TEXT(" "));

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	FString VmDiagnostics;
	const double VmAssembleStartTime = FPlatformTime::Seconds();
	const bool bVerseSourceValid = TryValidateVerseSource(VerseCode, VmDiagnostics);
	const bool bVmProcedureReady = bVerseSourceValid && TryCreateVmProcedure(BehaviorID, Behavior, IrProgram, VmDiagnostics);
	VmAssembleTimeMs += (FPlatformTime::Seconds() - VmAssembleStartTime) * 1000.0;
	if (bVmProcedureReady)
	{
		Behavior.bUsesVmEntryPoint = true;
		Behavior.bHasExecutableProcedure = true;
		Behavior.CompileState = EFlightVerseCompileState::VmCompiled;
		Behavior.LastCompileDiagnostics =
			TEXT("Compiled to Verse VM procedure via direct IR assembly.");
		AppendSimdCompileDiagnostics(Behavior.LastCompileDiagnostics);
		if (!VmDiagnostics.IsEmpty())
		{
			Behavior.LastCompileDiagnostics += TEXT("\n") + VmDiagnostics;
		}

		OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"),
			BehaviorID, Behavior.ExecutionRateHz, Behavior.FrameInterval, Behavior.bIsAsync ? TEXT("[Async]") : TEXT(""));
		OutErrors += Behavior.LastCompileDiagnostics + TEXT("\n");
		OutErrors += TEXT("Generated Verse (preview):\n") + VerseCode;
		BackendPath = TEXT("VerseVmProcedure");
		FinalizeArtifactReport();
		return true;
	}

	Behavior.LastCompileDiagnostics = FString::Printf(
		TEXT("Behavior is not executable: %sVerse VM IR assembly failed; generated Verse is preview-only."),
		*FallbackPrefix);
	AppendSimdCompileDiagnostics(Behavior.LastCompileDiagnostics);
	if (!VmDiagnostics.IsEmpty())
	{
		Behavior.LastCompileDiagnostics += TEXT("\n") + VmDiagnostics;
	}
	OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"),
		BehaviorID, Behavior.ExecutionRateHz, Behavior.FrameInterval, Behavior.bIsAsync ? TEXT("[Async]") : TEXT(""));
	OutErrors += Behavior.LastCompileDiagnostics + TEXT("\n");
	OutErrors += TEXT("Generated Verse:\n") + VerseCode;
	BackendPath = TEXT("GeneratedPreviewOnly");
	FinalizeArtifactReport();
	return false;
#else
	Behavior.LastCompileDiagnostics = FString::Printf(
		TEXT("Behavior is not executable: %sVerse VM is unavailable in this build; generated Verse is preview-only."),
		*FallbackPrefix);
	AppendSimdCompileDiagnostics(Behavior.LastCompileDiagnostics);
	OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"),
		BehaviorID, Behavior.ExecutionRateHz, Behavior.FrameInterval, Behavior.bIsAsync ? TEXT("[Async]") : TEXT(""));
	OutErrors += Behavior.LastCompileDiagnostics + TEXT("\n");
	OutErrors += TEXT("Generated Verse:\n") + VerseCode;
	BackendPath = TEXT("GeneratedPreviewOnly");
	FinalizeArtifactReport();
	return false;
#endif
}

void UFlightVerseSubsystem::ExecuteBehavior(uint32 BehaviorID, Flight::Swarm::FDroidState& DroidState)
{
	ExecuteBehaviorOnStruct(BehaviorID, &DroidState, GetDroidStateTypeKey());
}

void UFlightVerseSubsystem::ExecuteBehaviorOnStruct(uint32 BehaviorID, void* StructPtr, const void* TypeKey)
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior || !Behavior->bHasExecutableProcedure)
	{
		return;
	}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	// Zero-cost Tier 1 path (Literal scripts)
	if (Behavior->Tier == Flight::Vex::EVexTier::Literal)
	{
		if (const Flight::Vex::FVexTypeSchema* Schema = Flight::Vex::FVexSymbolRegistry::Get().GetSchema(TypeKey))
		{
			ExecuteOnSchema(Behavior->NativeProgram, StructPtr, *Schema);
			return;
		}
	}

	// Dynamic Tier 2/3 path (Verse VM)
	if (Behavior->bUsesVmEntryPoint && Behavior->Procedure.Get())
	{
		VerseContext.EnterVM([&]()
		{
			Verse::FAllocationContext AllocContext(VerseContext);
			ExecuteBehaviorInContext(BehaviorID, *Behavior, StructPtr, TypeKey, AllocContext);
		});
		return;
	}
#endif

	if (Behavior->bUsesNativeFallback)
	{
		// First try schema-based execution if not already handled
		if (const Flight::Vex::FVexTypeSchema* Schema = Flight::Vex::FVexSymbolRegistry::Get().GetSchema(TypeKey))
		{
			ExecuteOnSchema(Behavior->NativeProgram, StructPtr, *Schema);
			return;
		}

		// Fallback to legacy hardcoded path for FDroidState
		if (TypeKey == GetDroidStateTypeKey())
		{
			ExecuteNativeProgram(Behavior->NativeProgram, *static_cast<Flight::Swarm::FDroidState*>(StructPtr));
		}
		return;
	}
}

void UFlightVerseSubsystem::ExecuteOnSchema(const Flight::Vex::FVexProgramAst& Program, void* StructPtr, const Flight::Vex::FVexTypeSchema& Schema)
{
	using namespace Flight::Vex;

	UE_LOG(LogFlightVerseSubsystem, Verbose, TEXT("ExecuteOnSchema: Program has %d statements, %d expressions"), Program.Statements.Num(), Program.Expressions.Num());

	TMap<FString, FNativeValue> Locals;

	TFunction<FNativeValue(int32)> EvaluateExpression = [&](int32 NodeIndex) -> FNativeValue
	{
		if (!Program.Expressions.IsValidIndex(NodeIndex)) return FNativeValue();
		const FVexExpressionAst& Expr = Program.Expressions[NodeIndex];

		FNativeValue Result;
		switch (Expr.Kind)
		{
		case EVexExprKind::NumberLiteral:
			Result = MakeFloat(FCString::Atof(*Expr.Lexeme));
			break;

		case EVexExprKind::Identifier:
			Result = Locals.FindRef(Expr.Lexeme);
			break;

		case EVexExprKind::SymbolRef:
			if (const FVexSymbolAccessor* Accessor = Schema.Symbols.Find(Expr.Lexeme))
			{
				if (Accessor->MemberOffset != INDEX_NONE)
				{
					Result = MakeFloat(*(float*)((uint8*)StructPtr + Accessor->MemberOffset));
				}
				else if (Accessor->Getter)
				{
					Result = MakeFloat(Accessor->Getter(StructPtr));
				}
			}
			break;

		case EVexExprKind::BinaryOp:
		{
			FNativeValue L = EvaluateExpression(Expr.LeftNodeIndex);
			FNativeValue R = EvaluateExpression(Expr.RightNodeIndex);
			Result = ApplyArithmetic(Expr.Lexeme, L, R);
			break;
		}
		
		default:
			break;
		}

		UE_LOG(LogFlightVerseSubsystem, Log, TEXT("  EvalExpr [%d]: Kind=%d, Lexeme='%s', Value=%f"), NodeIndex, static_cast<int32>(Expr.Kind), *Expr.Lexeme, AsFloat(Result));
		return Result;
	};

	for (const FVexStatementAst& Stmt : Program.Statements)
	{
		if (Stmt.Kind == EVexStatementKind::Assignment)
		{
			if (!Program.Expressions.IsValidIndex(Stmt.ExpressionNodeIndex)) continue;
			const FVexExpressionAst& AssignExpr = Program.Expressions[Stmt.ExpressionNodeIndex];
			
			if (AssignExpr.Lexeme.EndsWith(TEXT("=")))
			{
				const FVexExpressionAst& LhsExpr = Program.Expressions[AssignExpr.LeftNodeIndex];
				FNativeValue RhsValue = EvaluateExpression(AssignExpr.RightNodeIndex);

				UE_LOG(LogFlightVerseSubsystem, Log, TEXT("  Assign: LHS='%s', Op='%s', RHS=%f"), *LhsExpr.Lexeme, *AssignExpr.Lexeme, AsFloat(RhsValue));

				if (LhsExpr.Kind == EVexExprKind::SymbolRef)
				{
					if (const FVexSymbolAccessor* Accessor = Schema.Symbols.Find(LhsExpr.Lexeme))
					{
						FNativeValue FinalValue = RhsValue;
						if (AssignExpr.Lexeme != TEXT("="))
						{
							const FString ArithmeticOp = AssignExpr.Lexeme.LeftChop(1);
							FNativeValue Current;
							if (Accessor->MemberOffset != INDEX_NONE) Current = MakeFloat(*(float*)((uint8*)StructPtr + Accessor->MemberOffset));
							else if (Accessor->Getter) Current = MakeFloat(Accessor->Getter(StructPtr));
							FinalValue = ApplyArithmetic(ArithmeticOp, Current, RhsValue);
						}

						UE_LOG(LogFlightVerseSubsystem, Log, TEXT("    Store Symbol: '%s' (offset %d) = %f"), *LhsExpr.Lexeme, Accessor->MemberOffset, AsFloat(FinalValue));

						if (Accessor->MemberOffset != INDEX_NONE)
						{
							*(float*)((uint8*)StructPtr + Accessor->MemberOffset) = AsFloat(FinalValue);
						}
						else if (Accessor->Setter)
						{
							Accessor->Setter(StructPtr, AsFloat(FinalValue));
						}
					}
				}
				else if (LhsExpr.Kind == EVexExprKind::Identifier)
				{
					FNativeValue LhsValue = Locals.FindRef(LhsExpr.Lexeme);
					FNativeValue FinalValue = RhsValue;
					if (AssignExpr.Lexeme != TEXT("="))
					{
						const FString ArithmeticOp = AssignExpr.Lexeme.LeftChop(1);
						FinalValue = ApplyArithmetic(ArithmeticOp, LhsValue, RhsValue);
					}
					Locals.Add(LhsExpr.Lexeme, FinalValue);
				}
			}
		}
	}
}

void UFlightVerseSubsystem::ExecuteBehaviorBulk(uint32 BehaviorID, TArrayView<Flight::Swarm::FDroidState> DroidStates)
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior || !Behavior->bHasExecutableProcedure)
	{
		return;
	}

	// TIER 1: Optimized SIMD-ready path (Literal math)
	if (Behavior->Tier == Flight::Vex::EVexTier::Literal && Behavior->bUsesNativeFallback)
	{
		if (Behavior->SimdPlan.IsValid())
		{
			Behavior->SimdPlan->Execute(DroidStates);
			return;
		}

		// Fallback to loop if plan compilation failed
		for (Flight::Swarm::FDroidState& Droid : DroidStates)
		{
			ExecuteNativeProgram(Behavior->NativeProgram, Droid);
		}
		return;
	}

	// TIER 2/3: Standard dispatch via Fusion Reduction
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	if (Behavior->bUsesVmEntryPoint && Behavior->Procedure.Get())
	{
		VerseContext.EnterVM([&]()
		{
			Verse::FAllocationContext AllocContext(VerseContext);
			for (Flight::Swarm::FDroidState& Droid : DroidStates)
			{
				ExecuteBehaviorInContext(BehaviorID, *Behavior, Droid, AllocContext);
			}
		});
		return;
	}
#endif

	// Fallback to individual dispatch
	for (Flight::Swarm::FDroidState& Droid : DroidStates)
	{
		ExecuteBehavior(BehaviorID, Droid);
	}
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
void UFlightVerseSubsystem::ExecuteBehaviorInContext(uint32 BehaviorID, const FVerseBehavior& Behavior, void* StructPtr, const void* TypeKey, Verse::FAllocationContext& AllocContext)
{
	using namespace Verse;

	// Guard against recursion or re-entry if needed, but for bulk execution we assume safe context.
	ActiveVmBehaviorID = BehaviorID;
	ActiveVmStatePtr = StructPtr;
	ActiveVmTypeKey = TypeKey;

	VProcedure* Procedure = Behavior.Procedure.Get();
	if (Procedure)
	{
		VFunction& Function = VFunction::New(AllocContext, *Procedure, VValue(this));
		FOpResult VmResult = Function.Invoke(VerseContext, VFunction::Args());

		if (!VmResult.IsReturn())
		{
			UE_LOG(LogFlightVerseSubsystem, Verbose, TEXT("VM execution failed (kind=%d)"), static_cast<int32>(VmResult.Kind));
		}
	}

	ActiveVmStatePtr = nullptr;
	ActiveVmTypeKey = nullptr;
	ActiveVmBehaviorID = 0;
}
#endif

void UFlightVerseSubsystem::DeferBehavior(uint32 BehaviorID, void* StructPtr, const void* TypeKey)
{
	DeferredBehaviors.Add({ BehaviorID, StructPtr, TypeKey });
}

void UFlightVerseSubsystem::FlushDeferredBehaviors()
{
	if (DeferredBehaviors.Num() == 0) return;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	VerseContext.EnterVM([&]()
	{
		Verse::FAllocationContext AllocContext(VerseContext);
		for (const auto& Deferred : DeferredBehaviors)
		{
			const FVerseBehavior* Behavior = Behaviors.Find(Deferred.BehaviorID);
			if (Behavior && Behavior->bHasExecutableProcedure && Behavior->bUsesVmEntryPoint)
			{
				ExecuteBehaviorInContext(Deferred.BehaviorID, *Behavior, Deferred.StatePtr, Deferred.TypeKey, AllocContext);
			}
			else if (Behavior && Behavior->bUsesNativeFallback)
			{
				if (Deferred.TypeKey == GetDroidStateTypeKey())
				{
					ExecuteNativeProgram(Behavior->NativeProgram, *static_cast<Flight::Swarm::FDroidState*>(Deferred.StatePtr));
				}
			}
		}
	});
#else
	for (const auto& Deferred : DeferredBehaviors)
	{
		ExecuteBehaviorOnStruct(Deferred.BehaviorID, Deferred.StatePtr, Deferred.TypeKey);
	}
#endif

	DeferredBehaviors.Empty();
}

void UFlightVerseSubsystem::ExecuteBehaviorDirect(
	uint32 BehaviorID,
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates)
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior || !Behavior->bHasExecutableProcedure)
	{
		return;
	}

	// TIER 1: Direct SIMD path
	if (Behavior->Tier == Flight::Vex::EVexTier::Literal && Behavior->SimdPlan.IsValid())
	{
		Behavior->SimdPlan->ExecuteDirect(Transforms, DroidStates);
		return;
	}

	// Fallback: If not eligible for direct SIMD, we must gather/scatter (Existing path)
	// For now, we don't have a direct-scalar-fallback, so we'll just not execute or log.
	// In a real system, we'd gather to FDroidState and use ExecuteBehaviorBulk.
}

EFlightVerseCompileState UFlightVerseSubsystem::GetBehaviorCompileState(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? Behavior->CompileState : EFlightVerseCompileState::VmCompileFailed;
}

bool UFlightVerseSubsystem::HasExecutableBehavior(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? Behavior->bHasExecutableProcedure : false;
}

FString UFlightVerseSubsystem::GetBehaviorCompileDiagnostics(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? Behavior->LastCompileDiagnostics : FString();
}

const Flight::Vex::FFlightCompileArtifactReport* UFlightVerseSubsystem::GetBehaviorCompileArtifactReport(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return (Behavior && Behavior->bHasCompileArtifactReport) ? &Behavior->CompileArtifactReport : nullptr;
}

FString UFlightVerseSubsystem::GetBehaviorCompileArtifactReportJson(uint32 BehaviorID) const
{
	const Flight::Vex::FFlightCompileArtifactReport* Report = GetBehaviorCompileArtifactReport(BehaviorID);
	return Report ? Flight::Vex::BuildCompileArtifactReportJson(*Report) : FString();
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
bool UFlightVerseSubsystem::TryValidateVerseSource(const FString& VerseSource, FString& OutDiagnostics) const
{
	using namespace uLang;

	TSRef<CDiagnostics> Diagnostics = TSRef<CDiagnostics>::New();
	SBuildManagerParams BuildManagerParams;
	CProgramBuildManager BuildManager(BuildManagerParams);

	const CUTF8String PackageName("FlightProjectGenerated");
	const CUTF8String PackageVersePath("/FlightProjectGenerated");
	TSRef<CSourceDataSnippet> Snippet = TSRef<CSourceDataSnippet>::New(
		CUTF8String("Behavior.generated.verse"),
		CUTF8String(TCHAR_TO_UTF8(*VerseSource)));
	BuildManager.AddSourceSnippet(Snippet, PackageName, PackageVersePath);

	SBuildParams BuildParams;
	BuildParams._TargetVM = SBuildParams::EWhichVM::VerseVM;
	BuildParams._bGenerateDigests = false;
	BuildParams._bGenerateCode = true;
	BuildParams._bVerbose = false;

	const SBuildResults BuildResults = BuildManager.Build(BuildParams, Diagnostics);
	for (const TSRef<SGlitch>& Glitch : Diagnostics->GetGlitches())
	{
		const TCHAR* Severity = Glitch->_Result.IsError()
			? TEXT("Error")
			: (Glitch->_Result.IsWarning() ? TEXT("Warning") : TEXT("Info"));

		OutDiagnostics += FString::Printf(
			TEXT("VerseCompiler %s: %s\n"),
			Severity,
			UTF8_TO_TCHAR(Glitch->_Result._Message.AsCString()));
	}

	if (BuildResults.HasFailure() || Diagnostics->HasErrors())
	{
		if (OutDiagnostics.IsEmpty())
		{
			OutDiagnostics = TEXT("VerseCompiler failed to validate generated Verse source.");
		}
		return false;
	}

	return true;
}

bool UFlightVerseSubsystem::TryCreateVmProcedure(uint32 BehaviorID, FVerseBehavior& Behavior, TSharedPtr<Flight::Vex::FVexIrProgram> IrProgram, FString& OutDiagnostics)
{
	bool bCreated = false;
	VerseContext.EnterVM([&]()
	{
		using namespace Flight::Verse;
		Verse::FAllocationContext AllocContext(VerseContext);

		// 1. Direct IR-to-VVM Assembly (New Path)
		if (IrProgram.IsValid())
		{
			const FString ProcName = FString::Printf(TEXT("Behavior_%u"), BehaviorID);
			Verse::VProcedure* AssembledProcedure = Flight::Vex::FVexVvmAssembler::Assemble(
				*IrProgram, AllocContext, ProcName);

			if (AssembledProcedure)
			{
				Behavior.Procedure.Set(AllocContext, AssembledProcedure);
				Behavior.bUsesVmEntryPoint = false;
				bCreated = true;
				return;
			}
		}

		// 2. Clear registry before build to ensure fresh results
		FFlightVerseAssemblerPass::ClearRegistry();

		// 2. The Build() call inside TryValidateVerseSource will now trigger the AssemblerPass
		//    which populates the registry. We need to find the decorated name of the function.
		//    Pattern: (/FlightProjectGenerated:)Behavior_<ID>
		const FString DecoratedName = FString::Printf(TEXT("(/FlightProjectGenerated:)Behavior_%u"), BehaviorID);

		// 3. Look for the compiled procedure
		if (Verse::VProcedure* CompiledProcedure = FFlightVerseAssemblerPass::GetCompiledProcedure(DecoratedName))
		{
			Behavior.Procedure.Set(AllocContext, CompiledProcedure);
			Behavior.bUsesVmEntryPoint = false; // Direct execution, no thunk needed
			bCreated = true;
			return;
		}

		// Fallback to manual thunk bridge if assembler skipped it (Phase B safety)
		const FString VmName = FString::Printf(TEXT("(/FlightProjectGenerated/Vex:)Behavior_%u_Thunk"), BehaviorID);
		Verse::VUniqueString& ProcedureName = Verse::VUniqueString::New(AllocContext, TCHAR_TO_UTF8(*VmName));
		Verse::VUniqueString& SourcePath = Verse::VUniqueString::New(AllocContext, "/FlightProjectGenerated/Behavior.generated.verse");
		Verse::VNativeFunction& EntryPoint = Verse::VNativeFunction::New(
			AllocContext,
			1,
			&UFlightVerseSubsystem::VmBehaviorEntryThunk,
			ProcedureName,
			Verse::VValue(this));

		Verse::FOpEmitter Emitter(AllocContext, SourcePath, ProcedureName, 0, 0);
		const Verse::FConstantIndex EntryPointConstant = Emitter.AllocateConstant(Verse::VValue(&EntryPoint));
		const Verse::FConstantIndex BehaviorIdConstant = Emitter.AllocateConstant(Verse::VValue::FromInt32(static_cast<int32>(BehaviorID)));
		const Verse::FRegisterIndex ReturnRegister = Emitter.AllocateRegister(Verse::EmptyLocation());

		TArray<Verse::FValueOperand> PositionalArguments;
		PositionalArguments.Add(Verse::FValueOperand(BehaviorIdConstant));
		TArray<TWriteBarrier<Verse::VUniqueString>> NamedArguments;
		TArray<Verse::FValueOperand> NamedArgumentValues;

		Emitter.Call(
			Verse::EmptyLocation(),
			ReturnRegister,
			Verse::FValueOperand(EntryPointConstant),
			MoveTemp(PositionalArguments),
			MoveTemp(NamedArguments),
			MoveTemp(NamedArgumentValues),
			false);
		Emitter.Return(Verse::EmptyLocation(), Verse::FValueOperand(ReturnRegister));

		Verse::VProcedure& Procedure = Emitter.MakeProcedure(AllocContext);
		Behavior.Procedure.Set(AllocContext, &Procedure);
		Behavior.VmEntryPoint.Set(AllocContext, &EntryPoint);
		Behavior.bUsesVmEntryPoint = true;
		bCreated = true;
	});

	if (!bCreated)
	{
		OutDiagnostics += TEXT("Failed to allocate Verse VM procedure/entrypoint.\n");
	}

	return bCreated;
}

Verse::FOpResult UFlightVerseSubsystem::VmBehaviorEntryThunk(
	Verse::FRunningContext Context,
	Verse::VValue Self,
	Verse::VNativeFunction::Args Arguments)
{
	(void)Context;

	if (!Self.IsUObject())
	{
		return Verse::FOpResult(Verse::FOpResult::Error);
	}

	UFlightVerseSubsystem* Subsystem = Cast<UFlightVerseSubsystem>(Self.AsUObject());
	if (!Subsystem)
	{
		return Verse::FOpResult(Verse::FOpResult::Error);
	}

	return Subsystem->ExecuteVmEntryThunk(Context, Arguments);
}

Verse::FOpResult UFlightVerseSubsystem::ExecuteVmEntryThunk(
	Verse::FRunningContext Context,
	Verse::VNativeFunction::Args Arguments)
{
	(void)Context;

	if (!ActiveVmDroidState)
	{
		return Verse::FOpResult(Verse::FOpResult::Error);
	}

	uint32 BehaviorID = ActiveVmBehaviorID;
	if (Arguments.Num() > 0 && Arguments[0].IsInt32())
	{
		BehaviorID = static_cast<uint32>(FMath::Max(0, Arguments[0].AsInt32()));
	}

	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return Verse::FOpResult(Verse::FOpResult::Error);
	}

	ExecuteNativeProgram(Behavior->NativeProgram, *ActiveVmDroidState);
	return Verse::FOpResult(Verse::FOpResult::Return, Verse::VValue::FromBool(true));
}

#include "Vex/FlightVexNativeRegistry.h"
#include "VerseVM/VVMFalse.h"

namespace
{
	/**
	 * FVexStateAccessor: Handles Get/Set for @symbols via Registry
	 */
	class FVexStateAccessor : public Flight::Vex::IVexNativeFunction
	{
	public:
		explicit FVexStateAccessor(bool bInIsSetter) : bIsSetter(bInIsSetter) {}

		virtual FString GetName() const override { return bIsSetter ? TEXT("SetStateValue") : TEXT("GetStateValue"); }
		virtual int32 GetNumArgs() const override { return bIsSetter ? 2 : 1; }

		virtual Verse::FOpResult Execute(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments) override
		{
			if (Arguments.Num() < GetNumArgs() || !Arguments[0].IsString())
			{
				return Verse::FOpResult(Verse::FOpResult::Error);
			}

			UFlightVerseSubsystem* Subsystem = Cast<UFlightVerseSubsystem>(Self.AsUObject());
			if (!Subsystem || !Subsystem->ActiveVmStatePtr || !Subsystem->ActiveVmTypeKey)
			{
				return Verse::FOpResult(Verse::FOpResult::Error);
			}

			FString SymbolName = UTF8_TO_TCHAR(Arguments[0].AsString().AsUTF8());
			const Flight::Vex::FVexSymbolAccessor* Accessor = Flight::Vex::FVexSymbolRegistry::Get().FindSymbol(Subsystem->ActiveVmTypeKey, SymbolName);

			if (!Accessor)
			{
				UE_LOG(LogFlightVerseSubsystem, Warning, TEXT("VEX VM: Accessor not found for symbol '%s'"), *SymbolName);
				return Verse::FOpResult(Verse::FOpResult::Error);
			}

			if (bIsSetter)
			{
				if (Arguments.Num() < 2 || !Arguments[1].IsFloat()) return Verse::FOpResult(Verse::FOpResult::Error);
				if (!Accessor->Setter) return Verse::FOpResult(Verse::FOpResult::Error);

				Accessor->Setter(Subsystem->ActiveVmStatePtr, Arguments[1].AsFloat());
				return Verse::FOpResult(Verse::FOpResult::Return, Verse::VValue::FromBool(true));
			}
			else
			{
				if (!Accessor->Getter) return Verse::FOpResult(Verse::FOpResult::Error);

				float Result = Accessor->Getter(Subsystem->ActiveVmStatePtr);
				return Verse::FOpResult(Verse::FOpResult::Return, Verse::VValue::FromFloat(Result));
			}
		}

	private:
		bool bIsSetter = false;
	};
}

Verse::FOpResult UFlightVerseSubsystem::RegistryThunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments)
{
	(void)Context;
	return Verse::FOpResult(Verse::FOpResult::Error);
}

namespace
{
	/**
	 * WaitOnGpu_Thunk: VEX/Verse native function to suspend and wait for GPU io_uring signal.
	 */
	Verse::FOpResult WaitOnGpu_Thunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments)
	{
		using namespace Verse;
		UFlightVerseSubsystem* Subsystem = Cast<UFlightVerseSubsystem>(Self.AsUObject());
		if (!Subsystem) return FOpResult(FOpResult::Error);

		// 1. Create a placeholder to represent the future value (bool)
		VPlaceholder& Placeholder = VPlaceholder::New(Context, 0);

		// 2. Register with GPU bridge (Mocking a request ID for now)
		// In a real system, we'd get this from UFlightGpuPerceptionSubsystem.
		static int64 MockRequestId = 1000;
		int64 RequestId = MockRequestId++;

		// Wrap the placeholder in a WriteBarrier to root it for the Garbage Collector
		Subsystem->PendingReadbacks.Add(RequestId, TWriteBarrier<VPlaceholder>(Context, Placeholder));

		// 3. Suspend current behavior execution
		return FOpResult(FOpResult::Return, VValue::Placeholder(Placeholder));
	}
}

void UFlightVerseSubsystem::CompleteGpuWait(int64 RequestId)
{
	if (Verse::TWriteBarrier<Verse::VPlaceholder>* Found = PendingReadbacks.Find(RequestId))
	{
		// Copy the barrier to keep it alive during fulfillment just in case
		Verse::TWriteBarrier<Verse::VPlaceholder> BarrierCopy = *Found;
		PendingReadbacks.Remove(RequestId);

		VerseContext.EnterVM([&]()
		{
			// We use Context to access the pointer inside the barrier
			Verse::FAllocationContext AllocContext(VerseContext);
			Verse::VPlaceholder* Placeholder = const_cast<Verse::VPlaceholder*>(&BarrierCopy.Get(VerseContext));

			// Fulfill the placeholder to wake the behavior
			Placeholder->Fill(VerseContext, Verse::VValue::FromBool(true));
		});

		// Once the behavior wakes up, it might have registered new deferred behaviors.
		// We flush them now to complete the "Fusion Reduction" cycle.
		FlushDeferredBehaviors();
	}
}

void UFlightVerseSubsystem::RegisterNativeVerseFunctions()
{
	using namespace Flight::Vex;
	FVexNativeRegistry& Registry = FVexNativeRegistry::Get();

	Registry.RegisterFunction(MakeShared<FVexStateAccessor>(false));
	Registry.RegisterFunction(MakeShared<FVexStateAccessor>(true));

	// Register built-in VVM thunks
	VerseContext.EnterVM([&]()
	{
		using namespace Verse;
		FAllocationContext AllocContext(VerseContext);

		VUniqueString& WaitName = VUniqueString::New(AllocContext, "WaitOnGpu");
		VNativeFunction& WaitFn = VNativeFunction::New(AllocContext, 0, &WaitOnGpu_Thunk, WaitName, GlobalFalse());

		// Note: Mapping in VVM's global namespace for Behaviors is pending.
	});

	UE_LOG(LogFlightVerseSubsystem, Verbose, TEXT("UFlightVerseSubsystem: Registered native VEX functions and WaitOnGpu thunk."));
}

void UFlightVerseSubsystem::RegisterNativeVerseSymbols()
{
	using namespace Flight::Vex;
	
	// Register the swarm-specific symbols via the new generic reflection registry
	TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();

	UE_LOG(LogFlightVerseSubsystem, Verbose, TEXT("UFlightVerseSubsystem: Registered native VEX symbols."));
}

Verse::FOpResult UFlightVerseSubsystem::GetStateValue(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments)
{
	static FVexStateAccessor Getter(false);
	return Getter.Execute(Context, Self, Arguments);
}

Verse::FOpResult UFlightVerseSubsystem::SetStateValue(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments)
{
	static FVexStateAccessor Setter(true);
	return Setter.Execute(Context, Self, Arguments);
}

#else

void UFlightVerseSubsystem::RegisterNativeVerseFunctions() {}
void UFlightVerseSubsystem::RegisterNativeVerseSymbols() {}

#endif

void UFlightVerseSubsystem::RegisterNativeComponents()
{
}

void UFlightVerseSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	UFlightVerseSubsystem* Subsystem = Cast<UFlightVerseSubsystem>(InThis);
	if (Subsystem)
	{
		for (auto& Pair : Subsystem->Behaviors)
		{
			FVerseBehavior& Behavior = Pair.Value;
			Collector.AddReferencedVerseValue(Behavior.Procedure, Subsystem, nullptr);
			Collector.AddReferencedVerseValue(Behavior.VmEntryPoint, Subsystem, nullptr);
		}

		for (auto& Pair : Subsystem->PendingReadbacks)
		{
			Collector.AddReferencedVerseValue(Pair.Value, Subsystem, nullptr);
		}
	}
#endif
}
