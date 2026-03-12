// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/UFlightVerseSubsystem.h"
#include "FlightDataSubsystem.h"
#include "Verse/FlightGpuScriptBridge.h"
#include "Vex/FlightCompileArtifacts.h"
#include "Vex/FlightVexBackendCapabilities.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexSchemaIr.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexSimdExecutor.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Vex/FlightVexIr.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Mass/FlightMassFragments.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FlightProject.h"

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
	using FNativeValue = Flight::Vex::FVexRuntimeValue;

	FNativeValue MakeFloat(const float Value)
	{
		return FNativeValue::FromFloat(Value);
	}

	FNativeValue MakeInt(const int32 Value)
	{
		return FNativeValue::FromInt(Value);
	}

	FNativeValue MakeBool(const bool Value)
	{
		return FNativeValue::FromBool(Value);
	}

	FNativeValue MakeVec3(const FVector3f& Value)
	{
		return FNativeValue::FromVec3(Value);
	}

	float AsFloat(const FNativeValue& Value)
	{
		return Value.AsFloat();
	}

	int32 AsInt(const FNativeValue& Value)
	{
		return Value.AsInt();
	}

	bool AsBool(const FNativeValue& Value)
	{
		return Value.AsBool();
	}

	FVector3f AsVec3(const FNativeValue& Value)
	{
		return Value.AsVec3();
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
			if (Expression.Kind == Flight::Vex::EVexExprKind::PipeIn || Expression.Kind == Flight::Vex::EVexExprKind::PipeOut)
			{
				OutReason = TEXT("Boundary operators are not executable in the native fallback path.");
				return false;
			}
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

	struct FMassEntityExecutionView
	{
		FFlightTransformFragment* Transform = nullptr;
		FFlightDroidStateFragment* DroidState = nullptr;
	};

	const void* ResolveMassFragmentViewConst(const FMassEntityExecutionView& View, const FName FragmentType)
	{
		if (FragmentType == TEXT("FFlightTransformFragment"))
		{
			return View.Transform;
		}
		if (FragmentType == TEXT("FFlightDroidStateFragment"))
		{
			return View.DroidState;
		}
		return nullptr;
	}

	void* ResolveMassFragmentViewMutable(const FMassEntityExecutionView& View, const FName FragmentType)
	{
		if (FragmentType == TEXT("FFlightTransformFragment"))
		{
			return View.Transform;
		}
		if (FragmentType == TEXT("FFlightDroidStateFragment"))
		{
			return View.DroidState;
		}
		return nullptr;
	}

	uint32 ResolveStorageStride(const Flight::Vex::FVexSymbolRecord& Symbol)
	{
		if (Symbol.Storage.ElementStride != 0)
		{
			return Symbol.Storage.ElementStride;
		}

		switch (Symbol.ValueType)
		{
		case Flight::Vex::EVexValueType::Float:
			return sizeof(float);
		case Flight::Vex::EVexValueType::Float2:
			return sizeof(FVector2f);
		case Flight::Vex::EVexValueType::Float3:
			return sizeof(FVector3f);
		case Flight::Vex::EVexValueType::Float4:
			return sizeof(FVector4f);
		case Flight::Vex::EVexValueType::Int:
			return sizeof(int32);
		case Flight::Vex::EVexValueType::Bool:
			return sizeof(bool);
		default:
			return 0;
		}
	}

	FNativeValue ReadRuntimeValueFromAddress(
		const uint8* Bytes,
		const Flight::Vex::EVexValueType ValueType,
		const uint32 ElementStride)
	{
		switch (ValueType)
		{
		case Flight::Vex::EVexValueType::Float:
			return MakeFloat(*reinterpret_cast<const float*>(Bytes));
		case Flight::Vex::EVexValueType::Int:
		{
			if (ElementStride == sizeof(uint32))
			{
				return MakeInt(static_cast<int32>(*reinterpret_cast<const uint32*>(Bytes)));
			}
			return MakeInt(*reinterpret_cast<const int32*>(Bytes));
		}
		case Flight::Vex::EVexValueType::Bool:
			return MakeBool(*reinterpret_cast<const bool*>(Bytes));
		case Flight::Vex::EVexValueType::Float2:
		{
			if (ElementStride == sizeof(FVector2D))
			{
				const FVector2D& Value = *reinterpret_cast<const FVector2D*>(Bytes);
				return Flight::Vex::FVexRuntimeValue::FromVec2(FVector2f(Value));
			}
			return Flight::Vex::FVexRuntimeValue::FromVec2(*reinterpret_cast<const FVector2f*>(Bytes));
		}
		case Flight::Vex::EVexValueType::Float3:
		{
			if (ElementStride == sizeof(FVector))
			{
				const FVector& Value = *reinterpret_cast<const FVector*>(Bytes);
				return MakeVec3(FVector3f(Value));
			}
			return MakeVec3(*reinterpret_cast<const FVector3f*>(Bytes));
		}
		case Flight::Vex::EVexValueType::Float4:
		{
			if (ElementStride == sizeof(FVector4))
			{
				const FVector4& Value = *reinterpret_cast<const FVector4*>(Bytes);
				return Flight::Vex::FVexRuntimeValue::FromVec4(FVector4f(Value));
			}
			return Flight::Vex::FVexRuntimeValue::FromVec4(*reinterpret_cast<const FVector4f*>(Bytes));
		}
		default:
			return FNativeValue();
		}
	}

	bool WriteRuntimeValueToAddress(
		uint8* Bytes,
		const Flight::Vex::EVexValueType ValueType,
		const uint32 ElementStride,
		const FNativeValue& Value)
	{
		switch (ValueType)
		{
		case Flight::Vex::EVexValueType::Float:
			*reinterpret_cast<float*>(Bytes) = AsFloat(Value);
			return true;
		case Flight::Vex::EVexValueType::Int:
			if (ElementStride == sizeof(uint32))
			{
				*reinterpret_cast<uint32*>(Bytes) = static_cast<uint32>(FMath::Max(0, AsInt(Value)));
				return true;
			}
			*reinterpret_cast<int32*>(Bytes) = AsInt(Value);
			return true;
		case Flight::Vex::EVexValueType::Bool:
			*reinterpret_cast<bool*>(Bytes) = AsBool(Value);
			return true;
		case Flight::Vex::EVexValueType::Float2:
			if (ElementStride == sizeof(FVector2D))
			{
				const FVector2f Vec2Value = Value.AsVec2();
				*reinterpret_cast<FVector2D*>(Bytes) = FVector2D(Vec2Value.X, Vec2Value.Y);
				return true;
			}
			*reinterpret_cast<FVector2f*>(Bytes) = Value.AsVec2();
			return true;
		case Flight::Vex::EVexValueType::Float3:
			if (ElementStride == sizeof(FVector))
			{
				const FVector3f Vec3Value = AsVec3(Value);
				*reinterpret_cast<FVector*>(Bytes) = FVector(Vec3Value.X, Vec3Value.Y, Vec3Value.Z);
				return true;
			}
			*reinterpret_cast<FVector3f*>(Bytes) = AsVec3(Value);
			return true;
		case Flight::Vex::EVexValueType::Float4:
			if (ElementStride == sizeof(FVector4))
			{
				const FVector4f Vec4Value = Value.AsVec4();
				*reinterpret_cast<FVector4*>(Bytes) = FVector4(Vec4Value.X, Vec4Value.Y, Vec4Value.Z, Vec4Value.W);
				return true;
			}
			*reinterpret_cast<FVector4f*>(Bytes) = Value.AsVec4();
			return true;
		default:
			return false;
		}
	}

	FNativeValue ReadMassFragmentSymbolValue(
		const FMassEntityExecutionView& View,
		const Flight::Vex::FVexSymbolRecord& Symbol)
	{
		const void* FragmentPtr = ResolveMassFragmentViewConst(View, Symbol.Storage.FragmentType);
		if (!FragmentPtr)
		{
			return FNativeValue();
		}

		if (Symbol.Storage.MemberOffset == INDEX_NONE)
		{
			return FNativeValue();
		}

		const uint32 ElementStride = ResolveStorageStride(Symbol);
		const uint8* Bytes = reinterpret_cast<const uint8*>(FragmentPtr) + Symbol.Storage.MemberOffset;
		return ReadRuntimeValueFromAddress(Bytes, Symbol.ValueType, ElementStride);
	}

	bool WriteMassFragmentSymbolValue(
		const FMassEntityExecutionView& View,
		const Flight::Vex::FVexSymbolRecord& Symbol,
		const FNativeValue& Value)
	{
		void* FragmentPtr = ResolveMassFragmentViewMutable(View, Symbol.Storage.FragmentType);
		if (!FragmentPtr)
		{
			return false;
		}

		if (Symbol.Storage.MemberOffset == INDEX_NONE)
		{
			return false;
		}

		const uint32 ElementStride = ResolveStorageStride(Symbol);
		uint8* Bytes = reinterpret_cast<uint8*>(FragmentPtr) + Symbol.Storage.MemberOffset;
		if (!WriteRuntimeValueToAddress(Bytes, Symbol.ValueType, ElementStride, Value))
		{
			return false;
		}

		if (Symbol.Storage.FragmentType == TEXT("FFlightDroidStateFragment") && View.DroidState)
		{
			View.DroidState->bIsDirty = true;
		}
		return true;
	}

	bool BuildMassRuntimeSchema(
		const Flight::Vex::FVexTypeSchema& BaseSchema,
		const Flight::Vex::FVexSchemaBindingResult& Binding,
		const FMassEntityExecutionView& View,
		Flight::Vex::FVexTypeSchema& OutSchema)
	{
		OutSchema = BaseSchema;
		OutSchema.SymbolRecords.Reset();

		for (const TPair<FString, Flight::Vex::FVexSymbolRecord>& Pair : BaseSchema.SymbolRecords)
		{
			const Flight::Vex::FVexSymbolRecord& SourceRecord = Pair.Value;
			if (SourceRecord.Storage.Kind != Flight::Vex::EVexStorageKind::MassFragmentField)
			{
				return false;
			}

			const Flight::Vex::FVexSchemaBoundSymbol* BoundSymbol = Binding.FindBoundSymbolByName(Pair.Key);
			if (!BoundSymbol)
			{
				return false;
			}

			const Flight::Vex::FVexSchemaFragmentBinding* FragmentBinding =
				Binding.FindFragmentBindingByType(BoundSymbol->LogicalSymbol.Storage.FragmentType);
			if (!FragmentBinding || FragmentBinding->StorageKind != Flight::Vex::EVexStorageKind::MassFragmentField)
			{
				return false;
			}

			Flight::Vex::FVexSymbolRecord RuntimeRecord = SourceRecord;
			RuntimeRecord.Storage.FragmentType = FragmentBinding->FragmentType;
			const Flight::Vex::FVexSymbolRecord AdaptedRecord = RuntimeRecord;
			RuntimeRecord.ReadValue = [View, AdaptedRecord](const void*) -> Flight::Vex::FVexRuntimeValue
			{
				return ReadMassFragmentSymbolValue(View, AdaptedRecord);
			};
			RuntimeRecord.WriteValue = [View, AdaptedRecord](void*, const Flight::Vex::FVexRuntimeValue& Value) -> bool
			{
				return WriteMassFragmentSymbolValue(View, AdaptedRecord, Value);
			};
			RuntimeRecord.Getter = {};
			RuntimeRecord.Setter = {};
			OutSchema.SymbolRecords.Add(Pair.Key, MoveTemp(RuntimeRecord));
		}

		OutSchema.RebuildLegacyViews();
		return true;
	}

	FNativeValue ApplyArithmetic(const FString& OperatorLexeme, const FNativeValue& Left, const FNativeValue& Right)
	{
		if (Left.Type == Flight::Vex::EVexValueType::Float3 || Right.Type == Flight::Vex::EVexValueType::Float3)
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
				if (Left.Type != Flight::Vex::EVexValueType::Float3 || RightNode.Kind != Flight::Vex::EVexExprKind::Identifier)
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
		if (Target.Type != Flight::Vex::EVexValueType::Float3)
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

	Collection.InitializeDependency<UFlightGpuScriptBridgeSubsystem>();

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

UFlightVerseSubsystem::FResolvedStorageHost UFlightVerseSubsystem::ResolveStorageHost(
	const FVerseBehavior& Behavior,
	const void* RequestedTypeKey) const
{
	FResolvedStorageHost Host;
	Host.TypeKey = RequestedTypeKey ? RequestedTypeKey : Behavior.BoundTypeKey;
	Host.TypeName = Behavior.BoundTypeStableName;

	if (Host.TypeKey)
	{
		Host.Schema = Flight::Vex::FVexSymbolRegistry::Get().GetSchema(Host.TypeKey);
	}

	if (Host.Schema)
	{
		if (!Behavior.SchemaBinding.IsSet())
		{
			Host.Kind = EStorageHostKind::SchemaAccessor;
			return Host;
		}

		bool bUsesAos = false;
		bool bUsesAccessor = false;
		bool bUsesMass = false;
		bool bUsesGpu = false;
		for (const Flight::Vex::FVexSchemaSymbolUse& Use : Behavior.SchemaBinding->SymbolUses)
		{
			if (!Behavior.SchemaBinding->BoundSymbols.IsValidIndex(Use.BoundSymbolIndex))
			{
				continue;
			}

			const Flight::Vex::FVexSchemaBoundSymbol& BoundSymbol = Behavior.SchemaBinding->BoundSymbols[Use.BoundSymbolIndex];
			const Flight::Vex::FVexStorageBinding& Storage = BoundSymbol.LogicalSymbol.Storage;
			switch (Storage.Kind)
			{
			case Flight::Vex::EVexStorageKind::AosOffset:
				bUsesAos = true;
				break;
			case Flight::Vex::EVexStorageKind::Accessor:
			case Flight::Vex::EVexStorageKind::ExternalProvider:
			case Flight::Vex::EVexStorageKind::None:
				bUsesAccessor = true;
				break;
			case Flight::Vex::EVexStorageKind::MassFragmentField:
				bUsesMass = true;
				break;
			case Flight::Vex::EVexStorageKind::SoaColumn:
			case Flight::Vex::EVexStorageKind::GpuBufferElement:
				bUsesGpu = true;
				break;
			default:
				return Host;
			}
		}

		if (bUsesGpu)
		{
			Host.Kind = EStorageHostKind::GpuBuffer;
		}
		else if (bUsesMass)
		{
			Host.Kind = EStorageHostKind::MassFragments;
		}
		else if (bUsesAccessor || !bUsesAos)
		{
			Host.Kind = EStorageHostKind::SchemaAccessor;
		}
		else
		{
			Host.Kind = EStorageHostKind::SchemaAos;
		}
		return Host;
	}

	if (Host.TypeKey == GetDroidStateTypeKey())
	{
		Host.Kind = EStorageHostKind::LegacyDroidState;
	}

	return Host;
}

bool UFlightVerseSubsystem::ExecuteResolvedStorageHost(
	const FResolvedStorageHost& Host,
	const FVerseBehavior& Behavior,
	void* StructPtr)
{
	switch (Host.Kind)
	{
	case EStorageHostKind::SchemaAos:
		check(Host.Schema);
		ExecuteOnAosSchemaHost(Behavior.NativeProgram, StructPtr, *Host.Schema);
		return true;
	case EStorageHostKind::SchemaAccessor:
		check(Host.Schema);
		ExecuteOnAccessorSchemaHost(Behavior.NativeProgram, StructPtr, *Host.Schema);
		return true;
	case EStorageHostKind::LegacyDroidState:
		ExecuteNativeProgram(Behavior.NativeProgram, *static_cast<Flight::Swarm::FDroidState*>(StructPtr));
		return true;
	default:
		return false;
	}
}

bool UFlightVerseSubsystem::ExecuteResolvedDirectStorageHost(
	const FResolvedStorageHost& Host,
	const FVerseBehavior& Behavior,
	const Flight::Vex::EVexBackendKind BackendKind,
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates)
{
	switch (Host.Kind)
	{
	case EStorageHostKind::MassFragments:
		return ExecuteOnMassSchemaHost(Behavior, BackendKind, Transforms, DroidStates);
	case EStorageHostKind::GpuBuffer:
		return BackendKind == Flight::Vex::EVexBackendKind::GpuKernel
			? ExecuteOnGpuSchemaHost(Behavior)
			: false;
	default:
		return false;
	}
}

void UFlightVerseSubsystem::ExecuteOnAosSchemaHost(
	const Flight::Vex::FVexProgramAst& Program,
	void* StructPtr,
	const Flight::Vex::FVexTypeSchema& Schema)
{
	ExecuteOnSchema(Program, StructPtr, Schema);
}

void UFlightVerseSubsystem::ExecuteOnAccessorSchemaHost(
	const Flight::Vex::FVexProgramAst& Program,
	void* StructPtr,
	const Flight::Vex::FVexTypeSchema& Schema)
{
	ExecuteOnSchema(Program, StructPtr, Schema);
}

bool UFlightVerseSubsystem::ExecuteOnMassSchemaHost(
	const FVerseBehavior& Behavior,
	const Flight::Vex::EVexBackendKind BackendKind,
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates)
{
	if (!Behavior.SchemaBinding.IsSet())
	{
		return false;
	}

	const Flight::Vex::FVexTypeSchema* BaseSchema = Behavior.BoundTypeKey
		? Flight::Vex::FVexSymbolRegistry::Get().GetSchema(Behavior.BoundTypeKey)
		: nullptr;
	if (!BaseSchema)
	{
		return false;
	}

	if (BackendKind == Flight::Vex::EVexBackendKind::NativeSimd)
	{
		if (Behavior.Tier != Flight::Vex::EVexTier::Literal || !Behavior.SimdPlan.IsValid())
		{
			return false;
		}

		Behavior.SimdPlan->ExecuteDirect(Transforms, DroidStates);
		return true;
	}

	if (BackendKind != Flight::Vex::EVexBackendKind::NativeScalar)
	{
		return false;
	}

	const int32 NumEntities = FMath::Min(Transforms.Num(), DroidStates.Num());
	for (int32 Index = 0; Index < NumEntities; ++Index)
	{
		FMassEntityExecutionView View;
		View.Transform = &Transforms[Index];
		View.DroidState = &DroidStates[Index];

		Flight::Vex::FVexTypeSchema RuntimeSchema;
		if (!BuildMassRuntimeSchema(*BaseSchema, *Behavior.SchemaBinding, View, RuntimeSchema))
		{
			return false;
		}

		ExecuteOnSchema(Behavior.NativeProgram, &View, RuntimeSchema);
	}

	return true;
}

bool UFlightVerseSubsystem::ExecuteOnGpuSchemaHost(const FVerseBehavior& Behavior)
{
	UWorld* World = GetWorld();
	UFlightGpuScriptBridgeSubsystem* GpuBridge = World ? World->GetSubsystem<UFlightGpuScriptBridgeSubsystem>() : nullptr;
	if (!GpuBridge)
	{
		UE_LOG(
			LogFlightVerseSubsystem,
			Verbose,
			TEXT("Behavior %u resolved to explicit GPU storage hosting, but no GPU script bridge is available."),
			Behavior.CompileArtifactReport.BehaviorID);
		return false;
	}

	FFlightGpuInvocation Invocation;
	Invocation.ProgramId = Behavior.BoundTypeStableName.IsNone()
		? FName(*FString::Printf(TEXT("Behavior_%u"), Behavior.CompileArtifactReport.BehaviorID))
		: Behavior.BoundTypeStableName;
	Invocation.BehaviorId = Behavior.CompileArtifactReport.BehaviorID;
	Invocation.LatencyClass = EFlightGpuLatencyClass::NextFrameGpu;
	Invocation.bAwaitRequested = Behavior.bHasAwaitableBoundary;
	Invocation.bMirrorRequested = Behavior.bHasMirrorRequest;
	Invocation.bAllowDeferredExternalSignal = true;

	for (const FString& ImportedSymbol : Behavior.ImportedSymbols)
	{
		FFlightGpuResourceBinding& Binding = Invocation.ResourceBindings.AddDefaulted_GetRef();
		Binding.Name = FName(*ImportedSymbol);
		Binding.SymbolName = ImportedSymbol;
	}

	for (const FString& ExportedSymbol : Behavior.ExportedSymbols)
	{
		FFlightGpuResourceBinding& Binding = Invocation.ResourceBindings.AddDefaulted_GetRef();
		Binding.Name = FName(*ExportedSymbol);
		Binding.SymbolName = ExportedSymbol;
	}

	FString SubmissionDetail;
	const FFlightGpuSubmissionHandle Handle = GpuBridge->Submit(Invocation, SubmissionDetail);
	UE_LOG(
		LogFlightVerseSubsystem,
		Verbose,
		TEXT("Behavior %u resolved to explicit GPU storage hosting. BridgeHandle=%llu Detail=%s"),
		Behavior.CompileArtifactReport.BehaviorID,
		static_cast<unsigned long long>(Handle.Value),
		*SubmissionDetail);
	return false;
}

bool UFlightVerseSubsystem::CompileVex(
	uint32 BehaviorID,
	const FString& VexSource,
	FString& OutErrors,
	UScriptStruct* TargetStruct,
	const FCompilePolicyContext& CompilePolicyContext)
{
	const Flight::Vex::FVexTypeSchema* Schema = nullptr;
	if (TargetStruct)
	{
		Schema = Flight::Vex::FVexSymbolRegistry::Get().GetSchemaByNativeStruct(TargetStruct);
		if (!Schema)
		{
			Schema = Flight::Vex::FVexSymbolRegistry::Get().GetSchema(static_cast<const void*>(TargetStruct));
		}
	}

	return CompileVex(
		BehaviorID,
		VexSource,
		OutErrors,
		Schema ? Schema->TypeId.RuntimeKey : static_cast<const void*>(nullptr),
		CompilePolicyContext);
}

bool UFlightVerseSubsystem::CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, UScriptStruct* TargetStruct)
{
	return CompileVex(BehaviorID, VexSource, OutErrors, TargetStruct, FCompilePolicyContext());
}

bool UFlightVerseSubsystem::CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, const void* TypeKey)
{
	return CompileVex(BehaviorID, VexSource, OutErrors, TypeKey, FCompilePolicyContext());
}

bool UFlightVerseSubsystem::CompileVex(
	uint32 BehaviorID,
	const FString& VexSource,
	FString& OutErrors,
	const void* TypeKey,
	const FCompilePolicyContext& CompilePolicyContext)
{
	// Initialize behavior state pessimistically; flip to executable only when a runnable path is available.
	FVerseBehavior& Behavior = Behaviors.FindOrAdd(BehaviorID);
	Behavior.CompileState = EFlightVerseCompileState::VmCompileFailed;
	Behavior.bHasExecutableProcedure = false;
	Behavior.bUsesNativeFallback = false;
	Behavior.bUsesVmEntryPoint = false;
	Behavior.NativeProgram = Flight::Vex::FVexProgramAst();
	Behavior.BoundTypeKey = nullptr;
	Behavior.BoundTypeStableName = NAME_None;
	Behavior.BoundSchemaLayoutHash = 0;
	Behavior.SchemaBinding.Reset();
	Behavior.GeneratedVerseCode.Reset();
	Behavior.LastCompileDiagnostics.Reset();
	Behavior.SelectedBackend.Reset();
	Behavior.CommittedBackend.Reset();
	Behavior.CommitDetail.Reset();
	Behavior.SelectedPolicyRowName = NAME_None;
	Behavior.PolicyPreferredDomain = EFlightBehaviorCompileDomainPreference::Unspecified;
	Behavior.bPolicyAllowsNativeFallback = true;
	Behavior.bPolicyAllowsGeneratedOnly = false;
	Behavior.bPolicyPrefersAsync = false;
	Behavior.RequiredContracts.Reset();
	Behavior.PolicyRequiredSymbols.Reset();
	Behavior.ImportedSymbols.Reset();
	Behavior.ExportedSymbols.Reset();
	Behavior.BoundaryOperatorCount = 0;
	Behavior.bHasBoundarySemantics = false;
	Behavior.bBoundarySemanticsExecutable = true;
	Behavior.bHasAwaitableBoundary = false;
	Behavior.bHasMirrorRequest = false;
	Behavior.BoundaryExecutionDetail.Reset();
	Behavior.SimdCompileDiagnostics.Reset();
	Behavior.CompileArtifactReport = Flight::Vex::FFlightCompileArtifactReport();
	Behavior.bHasCompileArtifactReport = false;

	Flight::Vex::FFlightCompileArtifactReport ArtifactReport;
	ArtifactReport.GeneratedAtUtc = FDateTime::UtcNow();
	ArtifactReport.BehaviorID = BehaviorID;
	ArtifactReport.SourceHash = FCrc::StrCrc32(*VexSource);
	ArtifactReport.TargetFingerprint = BuildCompileTargetFingerprint();

	const FFlightBehaviorCompilePolicyRow* CompilePolicy = ResolveCompilePolicy(BehaviorID, CompilePolicyContext);
	if (CompilePolicy)
	{
		Behavior.SelectedPolicyRowName = CompilePolicy->RowName;
		Behavior.PolicyPreferredDomain = CompilePolicy->PreferredDomain;
		Behavior.bPolicyAllowsNativeFallback = CompilePolicy->AllowsNativeFallback();
		Behavior.bPolicyAllowsGeneratedOnly = CompilePolicy->AllowsGeneratedOnly();
		Behavior.bPolicyPrefersAsync = CompilePolicy->PrefersAsync();
		Behavior.RequiredContracts = CompilePolicy->RequiredContracts;
		for (const FName RequiredSymbol : CompilePolicy->RequiredSymbols)
		{
			if (!RequiredSymbol.IsNone())
			{
				Behavior.PolicyRequiredSymbols.Add(RequiredSymbol.ToString());
			}
		}
	}

	const double CompileStartTime = FPlatformTime::Seconds();
	FString BackendPath = TEXT("GeneratedPreviewOnly");
	FString IrErrors;
	TSharedPtr<Flight::Vex::FVexIrProgram> IrProgram;
	double ParseTimeMs = 0.0;
	double IrCompileTimeMs = 0.0;
	double VmAssembleTimeMs = 0.0;
	TOptional<Flight::Vex::FVexBackendSelection> BackendSelection;

	auto FinalizeArtifactReport = [&]()
	{
		const FString LegacyCommittedBackend = ResolveCommittedExecutionBackend(Behavior, TypeKey);

		ArtifactReport.GeneratedAtUtc = FDateTime::UtcNow();
		ArtifactReport.CompileOutcome = CompileStateToString(Behavior.CompileState);
		ArtifactReport.BackendPath = BackendPath;
		ArtifactReport.BoundTypeName = Behavior.BoundTypeStableName.IsNone()
			? FString()
			: Behavior.BoundTypeStableName.ToString();
		ArtifactReport.SchemaLayoutHash = Behavior.BoundSchemaLayoutHash;
		ArtifactReport.SelectedBackend = BackendSelection.IsSet()
			? Flight::Vex::VexBackendKindToString(BackendSelection->ChosenBackend)
			: LegacyCommittedBackend;
		ArtifactReport.CommittedBackend = LegacyCommittedBackend;
		ArtifactReport.CommitDetail = BuildCommitDetail(
			ArtifactReport.SelectedBackend,
			ArtifactReport.CommittedBackend,
			CompilePolicy,
			Behavior.bHasExecutableProcedure);
		ArtifactReport.SelectedPolicyRow = Behavior.SelectedPolicyRowName.IsNone()
			? FString()
			: Behavior.SelectedPolicyRowName.ToString();
		ArtifactReport.PolicyPreferredDomain = FlightBehaviorCompileDomainPreferenceToString(Behavior.PolicyPreferredDomain);
		ArtifactReport.bPolicyAllowsNativeFallback = Behavior.bPolicyAllowsNativeFallback;
		ArtifactReport.bPolicyAllowsGeneratedOnly = Behavior.bPolicyAllowsGeneratedOnly;
		ArtifactReport.bPolicyPrefersAsync = Behavior.bPolicyPrefersAsync;
		ArtifactReport.Diagnostics = Behavior.LastCompileDiagnostics;
		ArtifactReport.GeneratedVerseCode = Behavior.GeneratedVerseCode;
		ArtifactReport.IrCompileErrors = IrErrors;
		ArtifactReport.Tier = Behavior.Tier;
		ArtifactReport.bAsync = Behavior.bIsAsync;
		ArtifactReport.bHasIr = IrProgram.IsValid();
		ArtifactReport.bUsesNativeFallback = Behavior.bUsesNativeFallback;
		ArtifactReport.bUsesVmEntryPoint = Behavior.bUsesVmEntryPoint;
		ArtifactReport.AvailableArtifacts.Reset();
		ArtifactReport.BackendReports.Reset();
		ArtifactReport.ReadSymbols.Reset();
		ArtifactReport.WrittenSymbols.Reset();
		ArtifactReport.ImportedSymbols.Reset();
		ArtifactReport.ExportedSymbols.Reset();
		ArtifactReport.ReferencedStorageKinds.Reset();
		ArtifactReport.PolicyRequiredContracts.Reset();
		ArtifactReport.PolicyRequiredSymbols.Reset();
		ArtifactReport.BoundaryOperatorCount = 0;
		ArtifactReport.bHasBoundaryOperators = false;
		ArtifactReport.bHasAwaitableBoundary = false;
		ArtifactReport.bHasMirrorRequest = false;
		ArtifactReport.AvailableArtifacts.Add(Flight::Vex::EFlightCompileArtifactKind::CompileTelemetry);

		if (Behavior.SchemaBinding.IsSet())
		{
			ArtifactReport.ReadSymbols = Behavior.SchemaBinding->ReadSymbols.Array();
			ArtifactReport.ReadSymbols.Sort();
			ArtifactReport.WrittenSymbols = Behavior.SchemaBinding->WrittenSymbols.Array();
			ArtifactReport.WrittenSymbols.Sort();
			ArtifactReport.ImportedSymbols = Behavior.SchemaBinding->ImportedSymbols.Array();
			ArtifactReport.ImportedSymbols.Sort();
			ArtifactReport.ExportedSymbols = Behavior.SchemaBinding->ExportedSymbols.Array();
			ArtifactReport.ExportedSymbols.Sort();
			ArtifactReport.BoundaryOperatorCount = Behavior.SchemaBinding->BoundaryUses.Num();
			ArtifactReport.bHasBoundaryOperators = Behavior.SchemaBinding->bHasBoundaryOperators;
			ArtifactReport.bHasAwaitableBoundary = Behavior.SchemaBinding->bHasAwaitableBoundary;
			ArtifactReport.bHasMirrorRequest = Behavior.SchemaBinding->bHasMirrorRequest;

			TSet<FString> ReferencedStorageKinds;
			for (const Flight::Vex::FVexSchemaSymbolUse& Use : Behavior.SchemaBinding->SymbolUses)
			{
				if (!Behavior.SchemaBinding->BoundSymbols.IsValidIndex(Use.BoundSymbolIndex))
				{
					continue;
				}

				const Flight::Vex::FVexSchemaBoundSymbol& BoundSymbol = Behavior.SchemaBinding->BoundSymbols[Use.BoundSymbolIndex];
				ReferencedStorageKinds.Add(Flight::Vex::VexStorageKindToString(BoundSymbol.LogicalSymbol.Storage.Kind));
			}

			ArtifactReport.ReferencedStorageKinds = ReferencedStorageKinds.Array();
			ArtifactReport.ReferencedStorageKinds.Sort();
		}

		for (const FName RequiredContract : Behavior.RequiredContracts)
		{
			if (!RequiredContract.IsNone())
			{
				ArtifactReport.PolicyRequiredContracts.Add(RequiredContract.ToString());
			}
		}
		ArtifactReport.PolicyRequiredContracts.Sort();
		ArtifactReport.PolicyRequiredSymbols = Behavior.PolicyRequiredSymbols;
		ArtifactReport.PolicyRequiredSymbols.Sort();

		if (BackendSelection.IsSet())
		{
			for (const TPair<Flight::Vex::EVexBackendKind, Flight::Vex::FVexBackendCompatibilityReport>& Pair : BackendSelection->Reports)
			{
				Flight::Vex::FFlightCompileBackendReport& BackendReport = ArtifactReport.BackendReports.AddDefaulted_GetRef();
				BackendReport.Backend = Flight::Vex::VexBackendKindToString(Pair.Key);
				BackendReport.Decision = Flight::Vex::VexBackendDecisionToString(Pair.Value.Decision);
				BackendReport.Reasons = Pair.Value.Reasons;
			}
		}
		else
		{
			Flight::Vex::FFlightCompileBackendReport& BackendReport = ArtifactReport.BackendReports.AddDefaulted_GetRef();
			BackendReport.Backend = ArtifactReport.SelectedBackend;
			BackendReport.Decision = Behavior.bHasExecutableProcedure ? TEXT("Supported") : TEXT("Rejected");
			BackendReport.Reasons.Add(TEXT("Schema binding unavailable; compile used the legacy backend selection path."));
		}
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

		Behavior.SelectedBackend = ArtifactReport.SelectedBackend;
		Behavior.CommittedBackend = ArtifactReport.CommittedBackend;
		Behavior.CommitDetail = ArtifactReport.CommitDetail;
		Behavior.ImportedSymbols = ArtifactReport.ImportedSymbols;
		Behavior.ExportedSymbols = ArtifactReport.ExportedSymbols;
		Behavior.BoundaryOperatorCount = ArtifactReport.BoundaryOperatorCount;
		Behavior.bHasBoundarySemantics = ArtifactReport.bHasBoundaryOperators;
		Behavior.bBoundarySemanticsExecutable = !ArtifactReport.bHasBoundaryOperators;
		Behavior.bHasAwaitableBoundary = ArtifactReport.bHasAwaitableBoundary;
		Behavior.bHasMirrorRequest = ArtifactReport.bHasMirrorRequest;
		Behavior.BoundaryExecutionDetail = Behavior.bHasBoundarySemantics
			? FString::Printf(
				TEXT("Boundary-aware semantics are preserved for reporting only in the current runtime path. Selected backend=%s; committed backend=%s."),
				*ArtifactReport.SelectedBackend,
				*ArtifactReport.CommittedBackend)
			: FString();
		if (!Behavior.BoundaryExecutionDetail.IsEmpty()
			&& !Behavior.LastCompileDiagnostics.Contains(Behavior.BoundaryExecutionDetail))
		{
			if (!Behavior.LastCompileDiagnostics.IsEmpty())
			{
				Behavior.LastCompileDiagnostics += TEXT("\n");
			}
			Behavior.LastCompileDiagnostics += Behavior.BoundaryExecutionDetail;
			ArtifactReport.Diagnostics = Behavior.LastCompileDiagnostics;
		}

		Behavior.CompileArtifactReport = ArtifactReport;
		Behavior.bHasCompileArtifactReport = true;

		if (UWorld* World = GetWorld())
		{
			if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>())
			{
				Orchestration->Rebuild();
			}
		}
	};

	// 1. Resolve Symbol Definitions for this compilation
	const Flight::Vex::FVexTypeSchema* CompileSchema = TypeKey
		? Flight::Vex::FVexSymbolRegistry::Get().GetSchema(TypeKey)
		: nullptr;
	if (CompileSchema)
	{
		Behavior.BoundTypeKey = CompileSchema->TypeId.RuntimeKey ? CompileSchema->TypeId.RuntimeKey : TypeKey;
		Behavior.BoundTypeStableName = CompileSchema->TypeId.StableName.IsNone()
			? FName(*CompileSchema->TypeName)
			: CompileSchema->TypeId.StableName;
		Behavior.BoundSchemaLayoutHash = CompileSchema->LayoutHash;
	}
	TArray<Flight::Vex::FVexSymbolDefinition> LocalDefinitions = CompileSchema
		? Flight::Vex::FVexSchemaBinder::BuildSymbolDefinitions(*CompileSchema, SymbolDefinitions)
		: SymbolDefinitions;

	if (!Behavior.PolicyRequiredSymbols.IsEmpty())
	{
		TSet<FString> AvailableSymbols;
		for (const Flight::Vex::FVexSymbolDefinition& Definition : LocalDefinitions)
		{
			AvailableSymbols.Add(Definition.SymbolName);
		}

		TArray<FString> MissingPolicySymbols;
		for (const FString& RequiredSymbol : Behavior.PolicyRequiredSymbols)
		{
			if (!AvailableSymbols.Contains(RequiredSymbol))
			{
				MissingPolicySymbols.Add(RequiredSymbol);
			}
		}
		MissingPolicySymbols.Sort();

		if (MissingPolicySymbols.Num() > 0)
		{
			OutErrors += FString::Printf(
				TEXT("VEX Policy Error: selected policy requires symbols that are unavailable for this compile: %s\n"),
				*FString::Join(MissingPolicySymbols, TEXT(", ")));
			Behavior.LastCompileDiagnostics = OutErrors;
			BackendPath = TEXT("PolicyValidationOnly");
			FinalizeArtifactReport();
			return false;
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
	if (!TypeKey)
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
	Behavior.bIsAsync = Result.Program.Statements.ContainsByPredicate([](const Flight::Vex::FVexStatementAst& Statement)
	{
		return Statement.Kind == Flight::Vex::EVexStatementKind::TargetDirective && Statement.SourceSpan == TEXT("@async");
	});

	TOptional<Flight::Vex::EVexBackendKind> SelectedBackendKind;

	TOptional<Flight::Vex::FVexSchemaBindingResult> SchemaBinding;
	if (CompileSchema)
	{
		SchemaBinding = Flight::Vex::FVexSchemaBinder::BindProgram(Result.Program, *CompileSchema, SymbolDefinitions);
		if (!SchemaBinding->bSuccess)
		{
			if (!OutErrors.IsEmpty() && !OutErrors.EndsWith(TEXT("\n")))
			{
				OutErrors += TEXT("\n");
			}
			OutErrors += FString::Join(SchemaBinding->Errors, TEXT("\n"));
			Behavior.LastCompileDiagnostics = OutErrors;
			BackendPath = TEXT("SchemaBindingOnly");
			FinalizeArtifactReport();
			return false;
		}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		const bool bVerseVmAvailable = true;
#else
		const bool bVerseVmAvailable = false;
#endif
		BackendSelection = Flight::Vex::SelectBackendForProgram(
			*SchemaBinding,
			*CompileSchema,
			Behavior.Tier,
			Behavior.bIsAsync,
			bVerseVmAvailable,
			/* bPreferSimd */ true);
		SelectedBackendKind = BackendSelection->ChosenBackend;
		if (CompilePolicy)
		{
			if (const TOptional<Flight::Vex::EVexBackendKind> PolicyPreferredBackend =
				ResolvePreferredBackendFromPolicy(*CompilePolicy, Behavior.bIsAsync))
			{
				if (const Flight::Vex::FVexBackendCompatibilityReport* PolicyBackendReport =
					BackendSelection->Reports.Find(PolicyPreferredBackend.GetValue());
					PolicyBackendReport
					&& (PolicyBackendReport->Decision != Flight::Vex::EVexBackendDecision::Rejected
						|| PolicyPreferredBackend.GetValue() == Flight::Vex::EVexBackendKind::GpuKernel))
				{
					BackendSelection->ChosenBackend = PolicyPreferredBackend.GetValue();
					BackendSelection->SelectionReason = PolicyBackendReport->Decision != Flight::Vex::EVexBackendDecision::Rejected
						? FString::Printf(
							TEXT("Policy row '%s' preferred %s and that backend is legal for this binding."),
							*CompilePolicy->RowName.ToString(),
							*Flight::Vex::VexBackendKindToString(PolicyPreferredBackend.GetValue()))
						: FString::Printf(
							TEXT("Policy row '%s' prefers %s even though the current backend report rejects it; runtime commit must remain explicit."),
							*CompilePolicy->RowName.ToString(),
							*Flight::Vex::VexBackendKindToString(PolicyPreferredBackend.GetValue()));
					SelectedBackendKind = PolicyPreferredBackend;
				}
			}
		}
		SchemaBinding->BackendDiagnostics.Reset();
		SchemaBinding->BackendLegality.Reset();
		for (const TPair<Flight::Vex::EVexBackendKind, Flight::Vex::FVexBackendCompatibilityReport>& Pair : BackendSelection->Reports)
		{
			SchemaBinding->BackendLegality.Add(Pair.Key, Pair.Value.Decision != Flight::Vex::EVexBackendDecision::Rejected);
			for (const Flight::Vex::FVexSymbolBackendCompatibility& SymbolResult : Pair.Value.SymbolResults)
			{
				Flight::Vex::FVexBackendBindingDiagnostic& Diagnostic = SchemaBinding->BackendDiagnostics.AddDefaulted_GetRef();
				Diagnostic.Backend = Pair.Key;
				Diagnostic.SymbolName = SymbolResult.SymbolName;
				Diagnostic.AccessKind = SymbolResult.AccessKind;
				Diagnostic.Decision = SymbolResult.Decision;
				Diagnostic.bPreferredFastLane = SymbolResult.bPreferredFastLane;
				Diagnostic.Reason = SymbolResult.Reason;
			}
		}

		Behavior.SchemaBinding = *SchemaBinding;
	}

	// Always generate IR for VVM assembly if possible
	const double IrCompileStartTime = FPlatformTime::Seconds();
	IrProgram = Flight::Vex::FVexIrCompiler::Compile(Result.Program, LocalDefinitions, IrErrors);
	IrCompileTimeMs = (FPlatformTime::Seconds() - IrCompileStartTime) * 1000.0;

	if (Behavior.Tier == Flight::Vex::EVexTier::Literal)
	{
		FString SimdReason;
		const bool bSimdEligible = Flight::Vex::FVexSimdExecutor::CanCompileProgram(Result.Program, LocalDefinitions, &SimdReason);
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

	// Generate the Verse source code from the VEX AST
	TMap<FString, FString> VerseBySymbol = SchemaBinding.IsSet()
		? SchemaBinding->BuildVerseSymbolMap()
		: TMap<FString, FString>();
	if (!SchemaBinding.IsSet())
	{
		for (const Flight::Vex::FVexSymbolDefinition& Def : LocalDefinitions)
		{
			VerseBySymbol.Add(Def.SymbolName, Def.SymbolName.StartsWith(TEXT("@")) ? Def.SymbolName.Mid(1) : Def.SymbolName);
		}
	}

	const FString VerseCode = Flight::Vex::LowerToVerse(Result.Program, LocalDefinitions, VerseBySymbol);
	Behavior.GeneratedVerseCode = VerseCode;
	Behavior.bHasExecutableProcedure = false;
	Behavior.NativeProgram = Result.Program;
	Behavior.CompileState = EFlightVerseCompileState::GeneratedOnly;

	auto IsSelectedBackendSatisfied = [&SelectedBackendKind, &Behavior](
		const bool bCanUseVmPath,
		const bool bCanUseNativePath,
		const bool bCanUseSimdPath,
		const bool bCanUseGpuCommit) -> bool
	{
		if (!SelectedBackendKind.IsSet())
		{
			return true;
		}

		switch (SelectedBackendKind.GetValue())
		{
		case Flight::Vex::EVexBackendKind::NativeScalar:
			return bCanUseNativePath;
		case Flight::Vex::EVexBackendKind::NativeSimd:
			return bCanUseNativePath && bCanUseSimdPath;
		case Flight::Vex::EVexBackendKind::VerseVm:
			return bCanUseVmPath;
		case Flight::Vex::EVexBackendKind::GpuKernel:
			return bCanUseGpuCommit;
		default:
			return Behavior.bHasExecutableProcedure;
		}
	};

	auto MarkGeneratedOnlyByPolicy = [&Behavior, &SelectedBackendKind]()
	{
		Behavior.bHasExecutableProcedure = false;
		Behavior.bUsesVmEntryPoint = false;
		Behavior.CompileState = EFlightVerseCompileState::GeneratedOnly;
		if (SelectedBackendKind.IsSet())
		{
			const FString PolicyDowngradeMessage = FString::Printf(
				TEXT("Selected backend '%s' is not currently executable and policy does not allow fallback; compile remains generated-only."),
				*Flight::Vex::VexBackendKindToString(SelectedBackendKind.GetValue()));
			if (!Behavior.LastCompileDiagnostics.Contains(PolicyDowngradeMessage))
			{
				Behavior.LastCompileDiagnostics += TEXT("\n") + PolicyDowngradeMessage;
			}
		}
	};

	FString NativeFallbackReason;
	if (CanCompileNativeFallback(Result.Program, NativeFallbackReason))
	{
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
			if (IsSelectedBackendSatisfied(true, true, Behavior.SimdPlan.IsValid(), false)
				|| Behavior.bPolicyAllowsNativeFallback)
			{
				BackendPath = TEXT("VerseVmProcedure+NativeFallback");
				FinalizeArtifactReport();
				return true;
			}

			MarkGeneratedOnlyByPolicy();
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
		if (IsSelectedBackendSatisfied(false, true, Behavior.SimdPlan.IsValid(), false)
			|| Behavior.bPolicyAllowsNativeFallback)
		{
			BackendPath = TEXT("NativeFallback");
			FinalizeArtifactReport();
			return true;
		}

		MarkGeneratedOnlyByPolicy();
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
		if (IsSelectedBackendSatisfied(true, false, false, false)
			|| Behavior.bPolicyAllowsNativeFallback)
		{
			BackendPath = TEXT("VerseVmProcedure");
			FinalizeArtifactReport();
			return true;
		}

		MarkGeneratedOnlyByPolicy();
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
	return Behavior.bPolicyAllowsGeneratedOnly;
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
	return Behavior.bPolicyAllowsGeneratedOnly;
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

	const FResolvedStorageHost Host = ResolveStorageHost(*Behavior, TypeKey);
	const TOptional<Flight::Vex::EVexBackendKind> RuntimeBackend = ResolveStructExecutionBackendKind(*Behavior, Host.TypeKey);

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::VerseVm
		&& Behavior->Procedure.Get())
	{
		VerseContext.EnterVM([&]()
		{
			Verse::FAllocationContext AllocContext(VerseContext);
			ExecuteBehaviorInContext(BehaviorID, *Behavior, StructPtr, Host.TypeKey, AllocContext);
		});
		return;
	}
#endif

	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeScalar)
	{
		if (!ExecuteResolvedStorageHost(Host, *Behavior, StructPtr))
		{
			UE_LOG(
				LogFlightVerseSubsystem,
				Warning,
				TEXT("Behavior %u has no compatible storage host for type '%s'."),
				BehaviorID,
				Host.TypeName.IsNone() ? TEXT("<unknown>") : *Host.TypeName.ToString());
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
				if (const FVexSymbolRecord* SymbolRecord = Schema.FindSymbolRecord(Expr.Lexeme))
				{
					Result = SymbolRecord->ReadRuntimeValue(StructPtr);
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
			if (!Program.Expressions.IsValidIndex(Stmt.ExpressionNodeIndex)
				|| !Program.Tokens.IsValidIndex(Stmt.StartTokenIndex)
				|| !Program.Tokens.IsValidIndex(Stmt.EndTokenIndex))
			{
				continue;
			}

			int32 OpIdx = INDEX_NONE;
			for (int32 TokenIndex = Stmt.StartTokenIndex; TokenIndex <= Stmt.EndTokenIndex; ++TokenIndex)
			{
				const FVexToken& Token = Program.Tokens[TokenIndex];
				if (Token.Kind == EVexTokenKind::Operator
					&& (Token.Lexeme == TEXT("=")
						|| Token.Lexeme == TEXT("+=")
						|| Token.Lexeme == TEXT("-=")
						|| Token.Lexeme == TEXT("*=")
						|| Token.Lexeme == TEXT("/=")))
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
			const FNativeValue RhsValue = EvaluateExpression(Stmt.ExpressionNodeIndex);
			const FVexToken& LhsToken = Program.Tokens[Stmt.StartTokenIndex];

			UE_LOG(LogFlightVerseSubsystem, Log, TEXT("  Assign: LHS='%s', Op='%s', RHS=%f"), *LhsToken.Lexeme, *OpLexeme, AsFloat(RhsValue));

			if (LhsToken.Kind == EVexTokenKind::Identifier
				&& Stmt.StartTokenIndex + 1 < OpIdx
				&& Program.Tokens[Stmt.StartTokenIndex + 1].Kind == EVexTokenKind::Identifier)
			{
				const FString VariableName = Program.Tokens[Stmt.StartTokenIndex + 1].Lexeme;
				Locals.Add(VariableName, RhsValue);
				continue;
			}

				if (LhsToken.Kind == EVexTokenKind::Symbol)
				{
					if (const FVexSymbolRecord* SymbolRecord = Schema.FindSymbolRecord(LhsToken.Lexeme))
					{
						FNativeValue FinalValue = RhsValue;
						if (OpLexeme != TEXT("="))
						{
							const FString ArithmeticOp = OpLexeme.LeftChop(1);
							const FNativeValue Current = SymbolRecord->ReadRuntimeValue(StructPtr);
							FinalValue = ApplyArithmetic(ArithmeticOp, Current, RhsValue);
						}

						UE_LOG(LogFlightVerseSubsystem, Log, TEXT("    Store Symbol: '%s' (storage %d, offset %d) = %f"),
							*LhsToken.Lexeme,
							static_cast<int32>(SymbolRecord->Storage.Kind),
							SymbolRecord->Storage.MemberOffset,
							AsFloat(FinalValue));

						SymbolRecord->WriteRuntimeValue(StructPtr, FinalValue);
					}
					continue;
				}

			if (LhsToken.Kind == EVexTokenKind::Identifier)
			{
				FNativeValue FinalValue = RhsValue;
				if (OpLexeme != TEXT("="))
				{
					const FString ArithmeticOp = OpLexeme.LeftChop(1);
					FinalValue = ApplyArithmetic(ArithmeticOp, Locals.FindRef(LhsToken.Lexeme), RhsValue);
				}
				Locals.Add(LhsToken.Lexeme, FinalValue);
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

	const TOptional<Flight::Vex::EVexBackendKind> RuntimeBackend = ResolveBulkExecutionBackendKind(*Behavior);
	const FResolvedStorageHost DroidHost = ResolveStorageHost(*Behavior, GetDroidStateTypeKey());

	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeSimd)
	{
		check(Behavior->SimdPlan.IsValid());
		Behavior->SimdPlan->Execute(DroidStates);
		return;
	}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::VerseVm
		&& Behavior->Procedure.Get())
	{
		VerseContext.EnterVM([&]()
		{
			Verse::FAllocationContext AllocContext(VerseContext);
			for (Flight::Swarm::FDroidState& Droid : DroidStates)
			{
				ExecuteBehaviorInContext(BehaviorID, *Behavior, &Droid, GetDroidStateTypeKey(), AllocContext);
			}
		});
		return;
	}
#endif

	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeScalar)
	{
		for (Flight::Swarm::FDroidState& Droid : DroidStates)
		{
			(void)ExecuteResolvedStorageHost(DroidHost, *Behavior, &Droid);
		}
		return;
	}

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
			if (!Behavior || !Behavior->bHasExecutableProcedure)
			{
				continue;
			}

			const FResolvedStorageHost DeferredHost = ResolveStorageHost(*Behavior, Deferred.TypeKey);
			const TOptional<Flight::Vex::EVexBackendKind> RuntimeBackend = ResolveStructExecutionBackendKind(*Behavior, DeferredHost.TypeKey);
			if (RuntimeBackend.IsSet()
				&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::VerseVm
				&& Behavior->Procedure.Get())
			{
				ExecuteBehaviorInContext(
					Deferred.BehaviorID,
					*Behavior,
					Deferred.StatePtr,
					DeferredHost.TypeKey,
					AllocContext);
			}
			else if (RuntimeBackend.IsSet()
				&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeScalar)
			{
				(void)ExecuteResolvedStorageHost(DeferredHost, *Behavior, Deferred.StatePtr);
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

	const FResolvedStorageHost Host = ResolveStorageHost(*Behavior, Behavior->BoundTypeKey);
	const TOptional<Flight::Vex::EVexBackendKind> RuntimeBackend = ResolveDirectExecutionBackendKind(*Behavior);
	if (!RuntimeBackend.IsSet()
		|| !ExecuteResolvedDirectStorageHost(Host, *Behavior, RuntimeBackend.GetValue(), Transforms, DroidStates))
	{
		const TCHAR* HostName = TEXT("None");
		switch (Host.Kind)
		{
		case EStorageHostKind::SchemaAos:
			HostName = TEXT("SchemaAos");
			break;
		case EStorageHostKind::SchemaAccessor:
			HostName = TEXT("SchemaAccessor");
			break;
		case EStorageHostKind::MassFragments:
			HostName = TEXT("MassFragments");
			break;
		case EStorageHostKind::GpuBuffer:
			HostName = TEXT("GpuBuffer");
			break;
		case EStorageHostKind::LegacyDroidState:
			HostName = TEXT("LegacyDroidState");
			break;
		default:
			break;
		}

		UE_LOG(
			LogFlightVerseSubsystem,
			Verbose,
			TEXT("Behavior %u direct execution resolved to storage host '%s', but no direct runtime host executed it."),
			BehaviorID,
			HostName);
	}
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

FString UFlightVerseSubsystem::DescribeCommittedExecutionBackend(uint32 BehaviorID, const void* TypeKey) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? ResolveCommittedExecutionBackend(*Behavior, TypeKey) : FString(TEXT("Unknown"));
}

FString UFlightVerseSubsystem::DescribeBulkExecutionBackend(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? DescribeRuntimeExecutionBackend(ResolveBulkExecutionBackendKind(*Behavior)) : FString(TEXT("Unknown"));
}

FString UFlightVerseSubsystem::DescribeDirectExecutionBackend(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? DescribeRuntimeExecutionBackend(ResolveDirectExecutionBackendKind(*Behavior)) : FString(TEXT("Unknown"));
}

FString UFlightVerseSubsystem::DescribeResolvedStorageHost(uint32 BehaviorID, const void* TypeKey) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return TEXT("None");
	}

	switch (ResolveStorageHost(*Behavior, TypeKey).Kind)
	{
	case EStorageHostKind::SchemaAos:
		return TEXT("SchemaAos");
	case EStorageHostKind::SchemaAccessor:
		return TEXT("SchemaAccessor");
	case EStorageHostKind::MassFragments:
		return TEXT("MassFragments");
	case EStorageHostKind::GpuBuffer:
		return TEXT("GpuBuffer");
	case EStorageHostKind::LegacyDroidState:
		return TEXT("LegacyDroidState");
	default:
		return TEXT("None");
	}
}

bool UFlightVerseSubsystem::CanExecuteResolvedStorageHostDirect(const FResolvedStorageHost& Host) const
{
	switch (Host.Kind)
	{
	case EStorageHostKind::SchemaAos:
	case EStorageHostKind::SchemaAccessor:
	case EStorageHostKind::LegacyDroidState:
		return true;
	default:
		return false;
	}
}

const FFlightBehaviorCompilePolicyRow* UFlightVerseSubsystem::ResolveCompilePolicy(
	const uint32 BehaviorID,
	const FCompilePolicyContext& CompilePolicyContext) const
{
	if (CompilePolicyContext.ExplicitPolicy)
	{
		return CompilePolicyContext.ExplicitPolicy;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const UGameInstance* GameInstance = World->GetGameInstance();
	const UFlightDataSubsystem* DataSubsystem = GameInstance ? GameInstance->GetSubsystem<UFlightDataSubsystem>() : nullptr;
	return DataSubsystem
		? DataSubsystem->FindBehaviorCompilePolicy(BehaviorID, CompilePolicyContext.CohortName, CompilePolicyContext.ProfileName)
		: nullptr;
}

TOptional<Flight::Vex::EVexBackendKind> UFlightVerseSubsystem::ResolvePreferredBackendFromPolicy(
	const FFlightBehaviorCompilePolicyRow& Policy,
	const bool bIsAsync)
{
	using namespace Flight::Vex;

	switch (Policy.PreferredDomain)
	{
	case EFlightBehaviorCompileDomainPreference::NativeCpu:
		return EVexBackendKind::NativeScalar;
	case EFlightBehaviorCompileDomainPreference::VerseVm:
		return EVexBackendKind::VerseVm;
	case EFlightBehaviorCompileDomainPreference::Simd:
		return EVexBackendKind::NativeSimd;
	case EFlightBehaviorCompileDomainPreference::Gpu:
		return EVexBackendKind::GpuKernel;
	case EFlightBehaviorCompileDomainPreference::TaskGraph:
		return bIsAsync ? TOptional<EVexBackendKind>(EVexBackendKind::VerseVm) : TOptional<EVexBackendKind>();
	default:
		return {};
	}
}

FString UFlightVerseSubsystem::BuildCommitDetail(
	const FString& SelectedBackend,
	const FString& CommittedBackend,
	const FFlightBehaviorCompilePolicyRow* Policy,
	const bool bHasExecutableProcedure)
{
	if (!bHasExecutableProcedure)
	{
		if (Policy && Policy->AllowsGeneratedOnly())
		{
			return FString::Printf(
				TEXT("Policy row '%s' allows generated-only output; no executable runtime path was committed."),
				*Policy->RowName.ToString());
		}

		return TEXT("No executable runtime path was committed.");
	}

	if (SelectedBackend.IsEmpty() || SelectedBackend == CommittedBackend)
	{
		return Policy
			? FString::Printf(TEXT("Committed backend satisfies policy row '%s'."), *Policy->RowName.ToString())
			: TEXT("Committed backend matches the selected executable backend.");
	}

	if (CommittedBackend.IsEmpty() || CommittedBackend == TEXT("Unknown"))
	{
		return Policy
			? FString::Printf(
				TEXT("Selected backend '%s' came from policy row '%s', but runtime commit remains unproven or pending."),
				*SelectedBackend,
				*Policy->RowName.ToString())
			: FString::Printf(TEXT("Selected backend '%s' is not yet a committed runtime path."), *SelectedBackend);
	}

	return Policy
		? FString::Printf(
			TEXT("Policy row '%s' selected '%s', but runtime committed '%s' after fallback."),
			*Policy->RowName.ToString(),
			*SelectedBackend,
			*CommittedBackend)
		: FString::Printf(TEXT("Selected backend '%s' downgraded to committed backend '%s'."), *SelectedBackend, *CommittedBackend);
}

TOptional<Flight::Vex::EVexBackendKind> UFlightVerseSubsystem::ResolveSelectedBackendKind(const FVerseBehavior& Behavior) const
{
	using namespace Flight::Vex;

	if (Behavior.SelectedBackend == VexBackendKindToString(EVexBackendKind::NativeScalar))
	{
		return EVexBackendKind::NativeScalar;
	}

	if (Behavior.SelectedBackend == VexBackendKindToString(EVexBackendKind::NativeSimd))
	{
		return EVexBackendKind::NativeSimd;
	}

	if (Behavior.SelectedBackend == VexBackendKindToString(EVexBackendKind::VerseVm))
	{
		return EVexBackendKind::VerseVm;
	}

	if (Behavior.SelectedBackend == VexBackendKindToString(EVexBackendKind::GpuKernel))
	{
		return EVexBackendKind::GpuKernel;
	}

	return {};
}

TOptional<Flight::Vex::EVexBackendKind> UFlightVerseSubsystem::ResolveStructExecutionBackendKind(
	const FVerseBehavior& Behavior,
	const void* RequestedTypeKey) const
{
	using namespace Flight::Vex;

	if (!Behavior.bHasExecutableProcedure)
	{
		return {};
	}

	const void* EffectiveTypeKey = RequestedTypeKey
		? RequestedTypeKey
		: (Behavior.BoundTypeKey ? Behavior.BoundTypeKey : GetDroidStateTypeKey());
	const FResolvedStorageHost Host = ResolveStorageHost(Behavior, EffectiveTypeKey);
	const bool bCanUseNativeScalar = Behavior.bUsesNativeFallback && CanExecuteResolvedStorageHostDirect(Host);

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	const bool bCanUseVm = Behavior.bUsesVmEntryPoint && Behavior.Procedure.Get();
#else
	const bool bCanUseVm = false;
#endif

	if (const TOptional<EVexBackendKind> SelectedBackend = ResolveSelectedBackendKind(Behavior))
	{
		switch (SelectedBackend.GetValue())
		{
		case EVexBackendKind::NativeScalar:
			if (bCanUseNativeScalar)
			{
				return EVexBackendKind::NativeScalar;
			}
			break;
		case EVexBackendKind::VerseVm:
			if (bCanUseVm)
			{
				return EVexBackendKind::VerseVm;
			}
			break;
		default:
			break;
		}
	}

	if (Behavior.Tier == Flight::Vex::EVexTier::Literal && bCanUseNativeScalar)
	{
		return EVexBackendKind::NativeScalar;
	}

	if (bCanUseVm)
	{
		return EVexBackendKind::VerseVm;
	}

	if (bCanUseNativeScalar)
	{
		return EVexBackendKind::NativeScalar;
	}

	return {};
}

TOptional<Flight::Vex::EVexBackendKind> UFlightVerseSubsystem::ResolveBulkExecutionBackendKind(const FVerseBehavior& Behavior) const
{
	using namespace Flight::Vex;

	if (!Behavior.bHasExecutableProcedure)
	{
		return {};
	}

	const FResolvedStorageHost Host = ResolveStorageHost(Behavior, GetDroidStateTypeKey());
	const bool bCanUseNativeScalar = Behavior.bUsesNativeFallback && CanExecuteResolvedStorageHostDirect(Host);
	const bool bCanUseSimd = Behavior.Tier == EVexTier::Literal && Behavior.bUsesNativeFallback && Behavior.SimdPlan.IsValid();

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	const bool bCanUseVm = Behavior.bUsesVmEntryPoint && Behavior.Procedure.Get();
#else
	const bool bCanUseVm = false;
#endif

	if (const TOptional<EVexBackendKind> SelectedBackend = ResolveSelectedBackendKind(Behavior))
	{
		switch (SelectedBackend.GetValue())
		{
		case EVexBackendKind::NativeSimd:
			if (bCanUseSimd)
			{
				return EVexBackendKind::NativeSimd;
			}
			break;
		case EVexBackendKind::VerseVm:
			if (bCanUseVm)
			{
				return EVexBackendKind::VerseVm;
			}
			break;
		case EVexBackendKind::NativeScalar:
			if (bCanUseNativeScalar)
			{
				return EVexBackendKind::NativeScalar;
			}
			break;
		default:
			break;
		}
	}

	if (bCanUseSimd)
	{
		return EVexBackendKind::NativeSimd;
	}

	if (bCanUseVm)
	{
		return EVexBackendKind::VerseVm;
	}

	if (bCanUseNativeScalar)
	{
		return EVexBackendKind::NativeScalar;
	}

	return {};
}

TOptional<Flight::Vex::EVexBackendKind> UFlightVerseSubsystem::ResolveDirectExecutionBackendKind(const FVerseBehavior& Behavior) const
{
	using namespace Flight::Vex;

	if (!Behavior.bHasExecutableProcedure)
	{
		return {};
	}

	const FResolvedStorageHost Host = ResolveStorageHost(Behavior, Behavior.BoundTypeKey);
	const bool bCanUseNativeScalar = Behavior.bUsesNativeFallback && Host.Kind == EStorageHostKind::MassFragments;
	const bool bCanUseSimd = bCanUseNativeScalar && Behavior.Tier == EVexTier::Literal && Behavior.SimdPlan.IsValid();
	const bool bCanUseGpu = Host.Kind == EStorageHostKind::GpuBuffer;

	if (const TOptional<EVexBackendKind> SelectedBackend = ResolveSelectedBackendKind(Behavior))
	{
		switch (SelectedBackend.GetValue())
		{
		case EVexBackendKind::NativeSimd:
			if (bCanUseSimd)
			{
				return EVexBackendKind::NativeSimd;
			}
			break;
		case EVexBackendKind::NativeScalar:
			if (bCanUseNativeScalar)
			{
				return EVexBackendKind::NativeScalar;
			}
			break;
		case EVexBackendKind::GpuKernel:
			if (bCanUseGpu)
			{
				return EVexBackendKind::GpuKernel;
			}
			break;
		default:
			break;
		}
	}

	if (bCanUseSimd)
	{
		return EVexBackendKind::NativeSimd;
	}

	if (bCanUseNativeScalar)
	{
		return EVexBackendKind::NativeScalar;
	}

	if (bCanUseGpu)
	{
		return EVexBackendKind::GpuKernel;
	}

	return {};
}

FString UFlightVerseSubsystem::DescribeRuntimeExecutionBackend(const TOptional<Flight::Vex::EVexBackendKind>& BackendKind)
{
	return BackendKind.IsSet()
		? Flight::Vex::VexBackendKindToString(BackendKind.GetValue())
		: FString(TEXT("Unknown"));
}

FString UFlightVerseSubsystem::ResolveCommittedExecutionBackend(const FVerseBehavior& Behavior, const void* RequestedTypeKey) const
{
	return DescribeRuntimeExecutionBackend(ResolveStructExecutionBackendKind(Behavior, RequestedTypeKey));
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
				const Flight::Vex::FVexSymbolRecord* SymbolRecord = Flight::Vex::FVexSymbolRegistry::Get().FindSymbolRecord(Subsystem->ActiveVmTypeKey, SymbolName);

				if (!SymbolRecord)
				{
					UE_LOG(LogFlightVerseSubsystem, Warning, TEXT("VEX VM: Accessor not found for symbol '%s'"), *SymbolName);
					return Verse::FOpResult(Verse::FOpResult::Error);
				}

				if (bIsSetter)
				{
					if (Arguments.Num() < 2 || !Arguments[1].IsFloat()) return Verse::FOpResult(Verse::FOpResult::Error);
					if (!SymbolRecord->bWritable) return Verse::FOpResult(Verse::FOpResult::Error);

					if (!SymbolRecord->WriteRuntimeValue(Subsystem->ActiveVmStatePtr, Flight::Vex::FVexRuntimeValue::FromFloat(Arguments[1].AsFloat())))
					{
						return Verse::FOpResult(Verse::FOpResult::Error);
					}
					return Verse::FOpResult(Verse::FOpResult::Return, Verse::VValue::FromBool(true));
				}
				else
				{
					if (!SymbolRecord->bReadable) return Verse::FOpResult(Verse::FOpResult::Error);

					float Result = SymbolRecord->ReadRuntimeValue(Subsystem->ActiveVmStatePtr).AsFloat();
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
	 * WaitOnGpu_Thunk: VEX/Verse native function to suspend and wait for a GPU bridge submission.
	 */
	Verse::FOpResult WaitOnGpu_Thunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments)
	{
		using namespace Verse;
		UFlightVerseSubsystem* Subsystem = Cast<UFlightVerseSubsystem>(Self.AsUObject());
		if (!Subsystem) return FOpResult(FOpResult::Error);

		if (Arguments.Num() < 1 || !Arguments[0].IsInt32())
		{
			return FOpResult(FOpResult::Error);
		}

		UWorld* World = Subsystem->GetWorld();
		UFlightGpuScriptBridgeSubsystem* GpuBridge = World ? World->GetSubsystem<UFlightGpuScriptBridgeSubsystem>() : nullptr;
		if (!GpuBridge)
		{
			return FOpResult(FOpResult::Error);
		}

		const int64 TrackingId = static_cast<int64>(Arguments[0].AsInt32());
		FFlightGpuInvocation Invocation;
		Invocation.ProgramId = TEXT("Verse.WaitOnGpu");
		Invocation.LatencyClass = EFlightGpuLatencyClass::CpuObserved;
		Invocation.ExternalTrackingId = TrackingId;
		Invocation.bAwaitRequested = true;

		FString SubmissionDetail;
		const FFlightGpuSubmissionHandle Handle = GpuBridge->Submit(Invocation, SubmissionDetail);
		if (!Handle.IsValid())
		{
			return FOpResult(FOpResult::Error);
		}

		VPlaceholder& Placeholder = VPlaceholder::New(Context, 0);
		Subsystem->PendingReadbacks.Add(static_cast<int64>(Handle.Value), TWriteBarrier<VPlaceholder>(Context, Placeholder));

		FString AwaitReason;
		const TWeakObjectPtr<UFlightVerseSubsystem> WeakSubsystem(Subsystem);
		const bool bAwaitRegistered = GpuBridge->Await(
			Handle,
			[WeakSubsystem, Handle](EFlightGpuSubmissionStatus Status, const FString&)
			{
				if (UFlightVerseSubsystem* ResolvedSubsystem = WeakSubsystem.Get())
				{
					ResolvedSubsystem->CompleteGpuWait(
						static_cast<int64>(Handle.Value),
						Status == EFlightGpuSubmissionStatus::Completed);
				}
			},
			AwaitReason);
		if (!bAwaitRegistered)
		{
			Subsystem->PendingReadbacks.Remove(static_cast<int64>(Handle.Value));
			return FOpResult(FOpResult::Error);
		}

		return FOpResult(FOpResult::Return, VValue::Placeholder(Placeholder));
	}
}

void UFlightVerseSubsystem::CompleteGpuWait(int64 RequestId, const bool bSuccess)
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
			Placeholder->Fill(VerseContext, Verse::VValue::FromBool(bSuccess));
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
		VNativeFunction& WaitFn = VNativeFunction::New(AllocContext, 1, &WaitOnGpu_Thunk, WaitName, GlobalFalse());

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
