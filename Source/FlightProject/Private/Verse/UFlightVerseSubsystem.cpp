// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/UFlightVerseSubsystem.h"
#include "Verse/FlightVerseMassHostBundle.h"
#include "Verse/FlightVerseRuntimeValueAccess.h"
#include "Verse/FlightVerseSubsystemLog.h"
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

DEFINE_LOG_CATEGORY(LogFlightVerseSubsystem);

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

	FString VerseBehaviorKindToString(const EFlightVerseBehaviorKind Kind)
	{
		switch (Kind)
		{
		case EFlightVerseBehaviorKind::Atomic:
			return TEXT("Atomic");
		case EFlightVerseBehaviorKind::Sequence:
			return TEXT("Sequence");
		case EFlightVerseBehaviorKind::Selector:
			return TEXT("Selector");
		default:
			return TEXT("Unknown");
		}
	}

	FString CompositeBehaviorBackendLabel(const EFlightVerseBehaviorKind Kind)
	{
		switch (Kind)
		{
		case EFlightVerseBehaviorKind::Sequence:
			return TEXT("CompositeSequence");
		case EFlightVerseBehaviorKind::Selector:
			return TEXT("CompositeSelector");
		default:
			return TEXT("CompositeUnknown");
		}
	}

	bool IsHardwareSimdBackendKind(const Flight::Vex::EVexBackendKind Backend)
	{
		return Backend == Flight::Vex::EVexBackendKind::NativeAvx256x8;
	}

	bool IsVectorShapeBackendKind(const Flight::Vex::EVexBackendKind Backend)
	{
		return Backend == Flight::Vex::EVexBackendKind::NativeSimd
			|| Backend == Flight::Vex::EVexBackendKind::NativeAvx256x8;
	}

	FString DeriveVectorLegalityClass(
		const bool bHasVectorShape,
		const bool bHasHardwareSimd,
		const bool bHasGpu)
	{
		if (bHasGpu)
		{
			return TEXT("GpuLegal");
		}
		if (bHasHardwareSimd)
		{
			return TEXT("HardwareSimdLegal");
		}
		if (bHasVectorShape)
		{
			return TEXT("VectorShapeLegal");
		}
		return TEXT("ScalarOnly");
	}

	TArray<Flight::Vex::FFlightCompileVectorSymbolReport> BuildVectorSymbolReports(
		const Flight::Vex::FVexSchemaBindingResult& SchemaBinding)
	{
		struct FSymbolVectorAggregation
		{
			Flight::Vex::FFlightCompileVectorSymbolReport Report;
			TSet<FString> LegalBackends;
			TSet<FString> Reasons;
			bool bHasVectorShape = false;
			bool bHasHardwareSimd = false;
			bool bHasGpu = false;
		};

		TMap<FString, FSymbolVectorAggregation> Aggregations;
		for (const Flight::Vex::FVexSchemaBoundSymbol& BoundSymbol : SchemaBinding.BoundSymbols)
		{
			FSymbolVectorAggregation& Aggregation = Aggregations.FindOrAdd(BoundSymbol.SymbolName);
			const Flight::Vex::FVexVectorPackContract EffectiveVectorPack = BoundSymbol.LogicalSymbol.VectorPack.StorageClass == Flight::Vex::EVexVectorStorageClass::None
				? Flight::Vex::BuildDefaultVectorPackContract(
					BoundSymbol.LogicalSymbol.Storage,
					BoundSymbol.LogicalSymbol.ValueType,
					BoundSymbol.LogicalSymbol.AlignmentRequirement,
					BoundSymbol.LogicalSymbol.bSimdReadAllowed,
					BoundSymbol.LogicalSymbol.bSimdWriteAllowed,
					BoundSymbol.LogicalSymbol.bGpuTier1Allowed)
				: BoundSymbol.LogicalSymbol.VectorPack;
			Aggregation.Report.SymbolName = BoundSymbol.SymbolName;
			Aggregation.Report.ValueType = Flight::Vex::ToTypeString(BoundSymbol.LogicalSymbol.ValueType);
			Aggregation.Report.StorageKind = Flight::Vex::VexStorageKindToString(BoundSymbol.LogicalSymbol.Storage.Kind);
			Aggregation.Report.VectorStorageClass = Flight::Vex::VexVectorStorageClassToString(EffectiveVectorPack.StorageClass);
			Aggregation.Report.HighestLegalClass = TEXT("ScalarOnly");
		}

		for (const Flight::Vex::FVexBackendBindingDiagnostic& Diagnostic : SchemaBinding.BackendDiagnostics)
		{
			if (!IsVectorShapeBackendKind(Diagnostic.Backend)
				&& Diagnostic.Backend != Flight::Vex::EVexBackendKind::GpuKernel)
			{
				continue;
			}

			FSymbolVectorAggregation* Aggregation = Aggregations.Find(Diagnostic.SymbolName);
			if (!Aggregation)
			{
				continue;
			}

			const FString BackendName = Flight::Vex::VexBackendKindToString(Diagnostic.Backend);
			if (Diagnostic.Decision != Flight::Vex::EVexBackendDecision::Rejected)
			{
				Aggregation->LegalBackends.Add(BackendName);
				Aggregation->bHasVectorShape |= IsVectorShapeBackendKind(Diagnostic.Backend);
				Aggregation->bHasHardwareSimd |= IsHardwareSimdBackendKind(Diagnostic.Backend);
				Aggregation->bHasGpu |= Diagnostic.Backend == Flight::Vex::EVexBackendKind::GpuKernel;
			}
			else if (!Diagnostic.Reason.IsEmpty())
			{
				Aggregation->Reasons.Add(FString::Printf(TEXT("%s: %s"), *BackendName, *Diagnostic.Reason));
			}
		}

		TArray<Flight::Vex::FFlightCompileVectorSymbolReport> Reports;
		for (TPair<FString, FSymbolVectorAggregation>& Pair : Aggregations)
		{
			Pair.Value.Report.LegalBackends = Pair.Value.LegalBackends.Array();
			Pair.Value.Report.LegalBackends.Sort();
			Pair.Value.Report.Reasons = Pair.Value.Reasons.Array();
			Pair.Value.Report.Reasons.Sort();
			Pair.Value.Report.HighestLegalClass = DeriveVectorLegalityClass(
				Pair.Value.bHasVectorShape,
				Pair.Value.bHasHardwareSimd,
				Pair.Value.bHasGpu);
			Reports.Add(MoveTemp(Pair.Value.Report));
		}

		Reports.Sort([](const Flight::Vex::FFlightCompileVectorSymbolReport& A, const Flight::Vex::FFlightCompileVectorSymbolReport& B)
		{
			return A.SymbolName < B.SymbolName;
		});
		return Reports;
	}

	FString DescribeCurrentDirectProcessorFragmentSupport(
		const Flight::Vex::FVexSchemaFragmentBinding& FragmentBinding,
		bool& bOutSupported)
	{
		bOutSupported = false;
		if (FragmentBinding.StorageKind != Flight::Vex::EVexStorageKind::MassFragmentField)
		{
			return TEXT("Direct Mass processor only supports MassFragmentField storage.");
		}

		if (FragmentBinding.FragmentType == TEXT("FFlightTransformFragment")
			|| FragmentBinding.FragmentType == TEXT("FFlightDroidStateFragment"))
		{
			bOutSupported = true;
			return TEXT("Supported by the current direct Mass processor view resolver.");
		}

		return FString::Printf(
			TEXT("Unsupported by the current direct Mass processor view resolver for fragment '%s'."),
			*FragmentBinding.FragmentType.ToString());
	}

	TArray<Flight::Vex::FFlightCompileFragmentRequirementReport> BuildFragmentRequirementReports(
		const Flight::Vex::FVexSchemaBindingResult& SchemaBinding)
	{
		TArray<Flight::Vex::FFlightCompileFragmentRequirementReport> Reports;
		Reports.Reserve(SchemaBinding.FragmentBindings.Num());

		for (const Flight::Vex::FVexSchemaFragmentBinding& FragmentBinding : SchemaBinding.FragmentBindings)
		{
			Flight::Vex::FFlightCompileFragmentRequirementReport& Report = Reports.AddDefaulted_GetRef();
			Report.FragmentType = FragmentBinding.FragmentType.ToString();
			Report.StorageKind = Flight::Vex::VexStorageKindToString(FragmentBinding.StorageKind);
			Report.ReadSymbols = FragmentBinding.ReadSymbols.Array();
			Report.ReadSymbols.Sort();
			Report.WrittenSymbols = FragmentBinding.WrittenSymbols.Array();
			Report.WrittenSymbols.Sort();
			Report.CurrentDirectProcessorSupportReason = DescribeCurrentDirectProcessorFragmentSupport(
				FragmentBinding,
				Report.bSupportedByCurrentDirectProcessor);
		}

		Reports.Sort([](
			const Flight::Vex::FFlightCompileFragmentRequirementReport& Left,
			const Flight::Vex::FFlightCompileFragmentRequirementReport& Right)
		{
			return Left.FragmentType < Right.FragmentType;
		});
		return Reports;
	}

	FString SelectorGuardComparisonToStringLocal(const EFlightBehaviorGuardComparison Comparison)
	{
		switch (Comparison)
		{
		case EFlightBehaviorGuardComparison::Less:
			return TEXT("Less");
		case EFlightBehaviorGuardComparison::LessEqual:
			return TEXT("LessEqual");
		case EFlightBehaviorGuardComparison::Greater:
			return TEXT("Greater");
		case EFlightBehaviorGuardComparison::GreaterEqual:
			return TEXT("GreaterEqual");
		case EFlightBehaviorGuardComparison::Equal:
			return TEXT("Equal");
		case EFlightBehaviorGuardComparison::NotEqual:
			return TEXT("NotEqual");
		case EFlightBehaviorGuardComparison::IsTrue:
			return TEXT("IsTrue");
		case EFlightBehaviorGuardComparison::IsFalse:
			return TEXT("IsFalse");
		default:
			return TEXT("Unknown");
		}
	}

	FString BehaviorExecutionShapeToString(const EFlightBehaviorExecutionShape Shape)
	{
		switch (Shape)
		{
		case EFlightBehaviorExecutionShape::ScalarOnly:
			return TEXT("ScalarOnly");
		case EFlightBehaviorExecutionShape::VectorCapable:
			return TEXT("VectorCapable");
		case EFlightBehaviorExecutionShape::VectorPreferred:
			return TEXT("VectorPreferred");
		case EFlightBehaviorExecutionShape::ShapeAgnostic:
			return TEXT("ShapeAgnostic");
		default:
			return TEXT("Unknown");
		}
	}

	FString BuildChildBehaviorListString(TConstArrayView<uint32> ChildBehaviorIds)
	{
		TArray<FString> ChildStrings;
		ChildStrings.Reserve(ChildBehaviorIds.Num());
		for (const uint32 ChildBehaviorId : ChildBehaviorIds)
		{
			ChildStrings.Add(FString::FromInt(static_cast<int32>(ChildBehaviorId)));
		}
		return FString::Join(ChildStrings, TEXT(", "));
	}

	constexpr const TCHAR* NativeScalarLane = TEXT("NativeScalar");
	constexpr const TCHAR* NativeSimdLane = TEXT("NativeSimd");
	constexpr const TCHAR* NativeAvx256x8Lane = TEXT("NativeAvx256x8");
	constexpr const TCHAR* VerseVmLane = TEXT("VerseVm");
	constexpr const TCHAR* GpuKernelLane = TEXT("GpuKernel");

	bool IsVectorExecutionLane(const FString& Lane)
	{
		return Lane == NativeSimdLane || Lane == NativeAvx256x8Lane;
	}

	bool IsConcreteExecutionLane(const FString& Lane)
	{
		return Lane == NativeScalarLane
			|| Lane == NativeSimdLane
			|| Lane == NativeAvx256x8Lane
			|| Lane == VerseVmLane
			|| Lane == GpuKernelLane;
	}

	int32 FindLanePriority(const FString& Lane)
	{
		if (Lane == NativeAvx256x8Lane)
		{
			return 0;
		}
		if (Lane == NativeSimdLane)
		{
			return 1;
		}
		if (Lane == NativeScalarLane)
		{
			return 2;
		}
		if (Lane == VerseVmLane)
		{
			return 3;
		}
		if (Lane == GpuKernelLane)
		{
			return 4;
		}
		return 5;
	}

	void SortLanes(TArray<FString>& Lanes)
	{
		Lanes.Sort([](const FString& A, const FString& B)
		{
			const int32 PriorityCompare = FindLanePriority(A) - FindLanePriority(B);
			return PriorityCompare == 0 ? A < B : PriorityCompare < 0;
		});
	}

	EFlightBehaviorExecutionShape DeriveExecutionShapeFromLanes(
		const TArray<FString>& LegalLanes,
		const FString& PreferredLane)
	{
		const bool bHasVectorLane = LegalLanes.ContainsByPredicate([](const FString& Lane)
		{
			return IsVectorExecutionLane(Lane);
		});
		if (bHasVectorLane)
		{
			return IsVectorExecutionLane(PreferredLane)
				? EFlightBehaviorExecutionShape::VectorPreferred
				: EFlightBehaviorExecutionShape::VectorCapable;
		}

		if (LegalLanes.Contains(NativeScalarLane) || LegalLanes.Contains(VerseVmLane))
		{
			return EFlightBehaviorExecutionShape::ScalarOnly;
		}

		if (LegalLanes.Num() > 0)
		{
			return EFlightBehaviorExecutionShape::ShapeAgnostic;
		}

		return EFlightBehaviorExecutionShape::Unknown;
	}

	EFlightBehaviorExecutionShape DeriveCommittedExecutionShape(const FString& CommittedLane)
	{
		if (CommittedLane.IsEmpty())
		{
			return EFlightBehaviorExecutionShape::Unknown;
		}

		TArray<FString> SingleLane;
		SingleLane.Add(CommittedLane);
		return DeriveExecutionShapeFromLanes(SingleLane, CommittedLane);
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

bool UFlightVerseSubsystem::RegisterCompositeSequenceBehavior(
	uint32 BehaviorID,
	TConstArrayView<uint32> ChildBehaviorIds,
	FString& OutErrors,
	const void* TypeKey)
{
	return RegisterCompositeBehavior(BehaviorID, ChildBehaviorIds, OutErrors, TypeKey, EFlightVerseBehaviorKind::Sequence);
}

bool UFlightVerseSubsystem::RegisterCompositeSelectorBehavior(
	uint32 BehaviorID,
	TConstArrayView<uint32> ChildBehaviorIds,
	FString& OutErrors,
	const void* TypeKey)
{
	return RegisterCompositeBehavior(BehaviorID, ChildBehaviorIds, OutErrors, TypeKey, EFlightVerseBehaviorKind::Selector);
}

bool UFlightVerseSubsystem::RegisterGuardedCompositeSelectorBehavior(
	uint32 BehaviorID,
	TConstArrayView<FCompositeSelectorBranchSpec> BranchSpecs,
	FString& OutErrors,
	const void* TypeKey)
{
	OutErrors.Reset();
	if (BranchSpecs.IsEmpty())
	{
		OutErrors = TEXT("Guarded composite selector registration requires at least one branch.");
		return false;
	}

	const void* EffectiveTypeKey = TypeKey;
	TArray<uint32> ChildBehaviorIds;
	ChildBehaviorIds.Reserve(BranchSpecs.Num());
	for (const FCompositeSelectorBranchSpec& BranchSpec : BranchSpecs)
	{
		if (BranchSpec.BehaviorID == 0)
		{
			OutErrors = TEXT("Guarded composite selector registration requires non-zero child behavior ids.");
			return false;
		}

		if (!EffectiveTypeKey)
		{
			if (const FVerseBehavior* ChildBehavior = Behaviors.Find(BranchSpec.BehaviorID))
			{
				EffectiveTypeKey = ResolveEffectiveBehaviorTypeKey(*ChildBehavior);
			}
		}

		if (BranchSpec.bHasGuard)
		{
			FString GuardError;
			if (!ValidateSelectorGuard(BranchSpec.Guard, EffectiveTypeKey, GuardError))
			{
				OutErrors = GuardError.IsEmpty()
					? FString::Printf(TEXT("Selector guard for child %u is invalid."), BranchSpec.BehaviorID)
					: GuardError;
				return false;
			}
		}

		ChildBehaviorIds.Add(BranchSpec.BehaviorID);
	}

	if (!RegisterCompositeBehavior(BehaviorID, ChildBehaviorIds, OutErrors, TypeKey, EFlightVerseBehaviorKind::Selector))
	{
		return false;
	}

	FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		OutErrors = FString::Printf(TEXT("Guarded composite selector behavior %u was not retained after registration."), BehaviorID);
		return false;
	}

	Behavior->SelectorBranches.Reset();
	Behavior->SelectorBranches.Append(BranchSpecs.GetData(), BranchSpecs.Num());
	Behavior->bHasLastExecutionResult = false;
	Behavior->bLastExecutionSucceeded = false;
	Behavior->bLastExecutionCommitted = false;
	Behavior->LastSelectedChildBehaviorId = 0;
	Behavior->LastBranchEvidence.Reset();
	Behavior->LastGuardOutcomes.Reset();
	Behavior->LastExecutionDetail.Reset();

	TArray<FString> GuardDescriptions;
	for (const FCompositeSelectorBranchSpec& BranchSpec : Behavior->SelectorBranches)
	{
		if (BranchSpec.bHasGuard)
		{
			GuardDescriptions.Add(FString::Printf(TEXT("%u: %s"), BranchSpec.BehaviorID, *DescribeSelectorGuard(BranchSpec.Guard)));
		}
	}

	if (GuardDescriptions.Num() > 0)
	{
		Behavior->LastCompileDiagnostics = FString::Printf(
			TEXT("Registered guarded composite selector behavior with branches [%s]."),
			*FString::Join(GuardDescriptions, TEXT(", ")));
		Behavior->CommitDetail += FString::Printf(TEXT(" Guards=[%s]."), *FString::Join(GuardDescriptions, TEXT(", ")));
	}

	RefreshCapabilityEnvelopes();

	if (UWorld* World = GetWorld())
	{
		if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>())
		{
			Orchestration->Rebuild();
		}
	}

	return true;
}

bool UFlightVerseSubsystem::RegisterCompositeBehavior(
	uint32 BehaviorID,
	TConstArrayView<uint32> ChildBehaviorIds,
	FString& OutErrors,
	const void* TypeKey,
	const EFlightVerseBehaviorKind Kind)
{
	OutErrors.Reset();

	const FString OperatorName = VerseBehaviorKindToString(Kind);
	const FString BackendLabel = CompositeBehaviorBackendLabel(Kind);
	const FString OperatorNameLower = OperatorName.ToLower();
	if (ChildBehaviorIds.IsEmpty())
	{
		OutErrors = FString::Printf(TEXT("Composite %s registration requires at least one child behavior."), *OperatorNameLower);
		return false;
	}

	const void* EffectiveTypeKey = TypeKey;
	uint32 SharedFrameInterval = 1;
	float SharedExecutionRateHz = 0.0f;
	bool bInitializedTiming = false;
	TArray<FName> AggregatedContracts;
	TArray<FString> AggregatedImportedSymbols;
	TArray<FString> AggregatedExportedSymbols;

	for (const uint32 ChildBehaviorId : ChildBehaviorIds)
	{
		if (ChildBehaviorId == BehaviorID)
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u cannot reference itself as a child."),
				*OperatorNameLower,
				BehaviorID);
			return false;
		}

		const FVerseBehavior* ChildBehavior = Behaviors.Find(ChildBehaviorId);
		if (!ChildBehavior)
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u is missing child behavior %u."),
				*OperatorNameLower,
				BehaviorID,
				ChildBehaviorId);
			return false;
		}

		if (IsCompositeBehavior(*ChildBehavior))
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u does not yet support composite child behavior %u."),
				*OperatorNameLower,
				BehaviorID,
				ChildBehaviorId);
			return false;
		}

		if (!ChildBehavior->bHasExecutableProcedure
			&& !ChildBehavior->bUsesNativeFallback
			&& !ChildBehavior->SimdPlan.IsValid())
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u requires executable child behavior %u."),
				*OperatorNameLower,
				BehaviorID,
				ChildBehaviorId);
			return false;
		}

		if (ChildBehavior->bIsAsync)
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u does not yet support async child behavior %u."),
				*OperatorNameLower,
				BehaviorID,
				ChildBehaviorId);
			return false;
		}

		if (ChildBehavior->bHasBoundarySemantics)
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u does not yet support boundary-aware child behavior %u."),
				*OperatorNameLower,
				BehaviorID,
				ChildBehaviorId);
			return false;
		}

		const void* ChildTypeKey = ResolveEffectiveBehaviorTypeKey(*ChildBehavior);
		if (!EffectiveTypeKey)
		{
			EffectiveTypeKey = ChildTypeKey;
		}
		else if (ChildTypeKey && EffectiveTypeKey != ChildTypeKey)
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u requires children to share one type key; child %u is incompatible."),
				*OperatorNameLower,
				BehaviorID,
				ChildBehaviorId);
			return false;
		}

		if (!bInitializedTiming)
		{
			SharedFrameInterval = FMath::Max(1u, ChildBehavior->FrameInterval);
			SharedExecutionRateHz = ChildBehavior->ExecutionRateHz;
			bInitializedTiming = true;
		}
		else if (SharedFrameInterval != ChildBehavior->FrameInterval
			|| !FMath::IsNearlyEqual(SharedExecutionRateHz, ChildBehavior->ExecutionRateHz))
		{
			OutErrors = FString::Printf(
				TEXT("Composite %s behavior %u currently requires children to share one execution interval; child %u does not match."),
				*OperatorNameLower,
				BehaviorID,
				ChildBehaviorId);
			return false;
		}

		for (const FName Contract : ChildBehavior->RequiredContracts)
		{
			AggregatedContracts.AddUnique(Contract);
		}

		for (const FString& ImportedSymbol : ChildBehavior->ImportedSymbols)
		{
			AggregatedImportedSymbols.AddUnique(ImportedSymbol);
		}

		for (const FString& ExportedSymbol : ChildBehavior->ExportedSymbols)
		{
			AggregatedExportedSymbols.AddUnique(ExportedSymbol);
		}
	}

	Behaviors.Remove(BehaviorID);
	FVerseBehavior& Behavior = Behaviors.FindOrAdd(BehaviorID);
	Behavior.Kind = Kind;
	Behavior.ChildBehaviorIds.Reset();
	Behavior.ChildBehaviorIds.Append(ChildBehaviorIds.GetData(), ChildBehaviorIds.Num());
	Behavior.SelectorBranches.Reset();
	Behavior.CompileState = EFlightVerseCompileState::VmCompiled;
	Behavior.Tier = Flight::Vex::EVexTier::Full;
	Behavior.ExecutionRateHz = SharedExecutionRateHz;
	Behavior.FrameInterval = SharedFrameInterval;
	Behavior.bHasExecutableProcedure = true;
	Behavior.bUsesNativeFallback = false;
	Behavior.bUsesVmEntryPoint = false;
	Behavior.NativeProgram = Flight::Vex::FVexProgramAst();
	Behavior.BoundTypeKey = EffectiveTypeKey;
	Behavior.SchemaBinding.Reset();
	Behavior.SelectedBackend = BackendLabel;
	Behavior.CommittedBackend = BackendLabel;
	Behavior.CommitDetail = FString::Printf(
		TEXT("Composite %s behavior registered over child behaviors [%s]."),
		*OperatorNameLower,
		*BuildChildBehaviorListString(Behavior.ChildBehaviorIds));
	Behavior.bHasLastExecutionResult = false;
	Behavior.bLastExecutionSucceeded = false;
	Behavior.bLastExecutionCommitted = false;
	Behavior.LastSelectedChildBehaviorId = 0;
	Behavior.LastBranchEvidence.Reset();
	Behavior.LastGuardOutcomes.Reset();
	Behavior.LastExecutionDetail.Reset();
	Behavior.RequiredContracts = MoveTemp(AggregatedContracts);
	Behavior.ImportedSymbols = MoveTemp(AggregatedImportedSymbols);
	Behavior.ExportedSymbols = MoveTemp(AggregatedExportedSymbols);
	Behavior.BoundaryOperatorCount = 0;
	Behavior.bHasBoundarySemantics = false;
	Behavior.bBoundarySemanticsExecutable = true;
	Behavior.bHasAwaitableBoundary = false;
	Behavior.bHasMirrorRequest = false;
	Behavior.BoundaryExecutionDetail.Reset();
	Behavior.CapabilityEnvelope = FBehaviorCapabilityEnvelope();
	Behavior.bHasCompileArtifactReport = false;
	Behavior.CompileArtifactReport = Flight::Vex::FFlightCompileArtifactReport();
	Behavior.LastCompileDiagnostics = FString::Printf(
		TEXT("Registered composite %s behavior with child behaviors [%s]."),
		*OperatorNameLower,
		*BuildChildBehaviorListString(Behavior.ChildBehaviorIds));

	if (const Flight::Vex::FVexTypeSchema* Schema = EffectiveTypeKey
		? Flight::Vex::FVexSymbolRegistry::Get().GetSchema(EffectiveTypeKey)
		: nullptr)
	{
		Behavior.BoundTypeStableName = Schema->TypeId.StableName.IsNone()
			? FName(*Schema->TypeName)
			: Schema->TypeId.StableName;
		Behavior.BoundSchemaLayoutHash = Schema->LayoutHash;
	}
	else
	{
		Behavior.BoundTypeStableName = EffectiveTypeKey == GetDroidStateTypeKey()
			? FName(TEXT("FDroidState"))
			: NAME_None;
		Behavior.BoundSchemaLayoutHash = 0;
	}

	RefreshCapabilityEnvelopes();

	if (UWorld* World = GetWorld())
	{
		if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>())
		{
			Orchestration->Rebuild();
		}
	}

	return true;
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
	Behavior.Kind = EFlightVerseBehaviorKind::Atomic;
	Behavior.ChildBehaviorIds.Reset();
	Behavior.SelectorBranches.Reset();
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
	Behavior.bHasLastExecutionResult = false;
	Behavior.bLastExecutionSucceeded = false;
	Behavior.bLastExecutionCommitted = false;
	Behavior.LastSelectedChildBehaviorId = 0;
	Behavior.LastBranchEvidence.Reset();
	Behavior.LastGuardOutcomes.Reset();
	Behavior.LastExecutionDetail.Reset();
	Behavior.LastGpuSubmissionHandle = 0;
	Behavior.LastGpuSubmissionDetail.Reset();
	Behavior.bGpuExecutionPending = false;
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
	Behavior.CapabilityEnvelope = FBehaviorCapabilityEnvelope();
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
		ArtifactReport.VectorSymbolReports.Reset();
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
			ArtifactReport.FragmentRequirementReports = BuildFragmentRequirementReports(Behavior.SchemaBinding.GetValue());
			ArtifactReport.VectorSymbolReports = BuildVectorSymbolReports(Behavior.SchemaBinding.GetValue());
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
		RefreshCapabilityEnvelopes();

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
			Behavior.SimdPlan = Flight::Vex::FVexSimdExecutor::Compile(
				IrProgram,
				Behavior.SchemaBinding.IsSet() ? &Behavior.SchemaBinding.GetValue() : nullptr);
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
		case Flight::Vex::EVexBackendKind::NativeAvx256x8:
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
	const FBehaviorExecutionResult Result = ExecuteBehaviorWithResult(BehaviorID, StructPtr, TypeKey);
	CommitBehaviorExecutionResult(BehaviorID, Result);
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

	if (IsCompositeBehavior(*Behavior))
	{
		FBehaviorExecutionResult LastResult;
		for (Flight::Swarm::FDroidState& Droid : DroidStates)
		{
			LastResult = ExecuteBehaviorWithResult(BehaviorID, &Droid, GetDroidStateTypeKey());
		}
		if (DroidStates.Num() > 0)
		{
			CommitBehaviorExecutionResult(BehaviorID, LastResult);
		}
		return;
	}

	const TOptional<Flight::Vex::EVexBackendKind> RuntimeBackend = ResolveBulkExecutionBackendKind(*Behavior);
	const FResolvedStorageHost DroidHost = ResolveStorageHost(*Behavior, GetDroidStateTypeKey());

	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeAvx256x8)
	{
		check(Behavior->SimdPlan.IsValid());
		if (Behavior->SimdPlan->ExecuteExplicitAvx256x8(DroidStates))
		{
			const FString RuntimeBackendLabel = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeAvx256x8);
			const FString CommitDetail = Behavior->SelectedBackend == RuntimeBackendLabel
				? FString::Printf(TEXT("Bulk execution ratified explicit AVX2/FMA backend '%s' on %d droid states."), *RuntimeBackendLabel, DroidStates.Num())
				: FString::Printf(
					TEXT("Bulk execution selected '%s' after runtime fallback from '%s' and ratified it on %d droid states."),
					*RuntimeBackendLabel,
					*Behavior->SelectedBackend,
					DroidStates.Num());
			UpdateBehaviorCommitState(BehaviorID, RuntimeBackendLabel, CommitDetail);
			return;
		}
	}

	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeSimd)
	{
		check(Behavior->SimdPlan.IsValid());
		Behavior->SimdPlan->Execute(DroidStates);
		const FString RuntimeBackendLabel = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeSimd);
		const FString CommitDetail = Behavior->SelectedBackend == RuntimeBackendLabel
			? FString::Printf(TEXT("Bulk execution committed vector-shaped SIMD backend '%s' on %d droid states."), *RuntimeBackendLabel, DroidStates.Num())
			: FString::Printf(
				TEXT("Bulk execution selected '%s' after runtime fallback from '%s' on %d droid states."),
				*RuntimeBackendLabel,
				*Behavior->SelectedBackend,
				DroidStates.Num());
		UpdateBehaviorCommitState(BehaviorID, RuntimeBackendLabel, CommitDetail);
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
		const FString RuntimeBackendLabel = Flight::Vex::VexBackendKindToString(Flight::Vex::EVexBackendKind::NativeScalar);
		const FString CommitDetail = Behavior->SelectedBackend == RuntimeBackendLabel
			? FString::Printf(TEXT("Bulk execution committed native scalar backend '%s' on %d droid states."), *RuntimeBackendLabel, DroidStates.Num())
			: FString::Printf(
				TEXT("Bulk execution selected '%s' after runtime fallback from '%s' on %d droid states."),
				*RuntimeBackendLabel,
				*Behavior->SelectedBackend,
				DroidStates.Num());
		UpdateBehaviorCommitState(BehaviorID, RuntimeBackendLabel, CommitDetail);
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

			if (IsCompositeBehavior(*Behavior))
			{
				const FBehaviorExecutionResult Result = ExecuteBehaviorWithResult(Deferred.BehaviorID, Deferred.StatePtr, Deferred.TypeKey);
				CommitBehaviorExecutionResult(Deferred.BehaviorID, Result);
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
	TArray<FMassFragmentHostView> FragmentViews;
	FragmentViews.Reserve(2);

	if (Transforms.Num() > 0)
	{
		FMassFragmentHostView& TransformView = FragmentViews.AddDefaulted_GetRef();
		TransformView.FragmentType = TEXT("FFlightTransformFragment");
		TransformView.BasePtr = reinterpret_cast<uint8*>(Transforms.GetData());
		TransformView.FragmentStride = sizeof(FFlightTransformFragment);
		TransformView.EntityCount = Transforms.Num();
		TransformView.bWritable = true;
	}

	if (DroidStates.Num() > 0)
	{
		FMassFragmentHostView& DroidView = FragmentViews.AddDefaulted_GetRef();
		DroidView.FragmentType = TEXT("FFlightDroidStateFragment");
		DroidView.BasePtr = reinterpret_cast<uint8*>(DroidStates.GetData());
		DroidView.FragmentStride = sizeof(FFlightDroidStateFragment);
		DroidView.EntityCount = DroidStates.Num();
		DroidView.bWritable = true;
		DroidView.PostWrite = [](uint8* FragmentPtr)
		{
			if (FFlightDroidStateFragment* DroidState = reinterpret_cast<FFlightDroidStateFragment*>(FragmentPtr))
			{
				DroidState->bIsDirty = true;
			}
		};
	}

	ExecuteBehaviorDirect(BehaviorID, FragmentViews);
}

void UFlightVerseSubsystem::ExecuteBehaviorDirect(
	uint32 BehaviorID,
	TConstArrayView<FMassFragmentHostView> FragmentViews)
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return;
	}

	const FResolvedStorageHost Host = ResolveStorageHost(*Behavior, Behavior->BoundTypeKey);
	const TOptional<Flight::Vex::EVexBackendKind> RuntimeBackend = ResolveDirectExecutionBackendKind(*Behavior);
	FMassFragmentHostBundle Bundle = BuildMassFragmentHostBundle(FragmentViews);
	if (!RuntimeBackend.IsSet()
		|| !ExecuteResolvedDirectStorageHost(Host, *Behavior, RuntimeBackend.GetValue(), Bundle))
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
		return;
	}

	if (RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeAvx256x8
		|| RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeSimd
		|| RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeScalar)
	{
		const FString RuntimeBackendLabel = Flight::Vex::VexBackendKindToString(RuntimeBackend.GetValue());
		const int32 NumEntities = Bundle.GetEntityCount();
		const FString CommitDetail = Behavior->SelectedBackend == RuntimeBackendLabel
			? FString::Printf(TEXT("Direct execution committed backend '%s' on %d entities."), *RuntimeBackendLabel, NumEntities)
			: FString::Printf(
				TEXT("Direct execution selected '%s' after runtime fallback from '%s' on %d entities."),
				*RuntimeBackendLabel,
				*Behavior->SelectedBackend,
				NumEntities);
		UpdateBehaviorCommitState(BehaviorID, RuntimeBackendLabel, CommitDetail);
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
	if (!Behavior)
	{
		return FString(TEXT("Unknown"));
	}

	return IsCompositeBehavior(*Behavior)
		? (Behavior->CommittedBackend.IsEmpty() ? CompositeBehaviorBackendLabel(Behavior->Kind) : Behavior->CommittedBackend)
		: ResolveCommittedExecutionBackend(*Behavior, TypeKey);
}

FString UFlightVerseSubsystem::DescribeBulkExecutionBackend(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return FString(TEXT("Unknown"));
	}

	return IsCompositeBehavior(*Behavior)
		? CompositeBehaviorBackendLabel(Behavior->Kind)
		: DescribeRuntimeExecutionBackend(ResolveBulkExecutionBackendKind(*Behavior));
}

FString UFlightVerseSubsystem::DescribeDirectExecutionBackend(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return FString(TEXT("Unknown"));
	}

	return IsCompositeBehavior(*Behavior)
		? FString(TEXT("Unknown"))
		: DescribeRuntimeExecutionBackend(ResolveDirectExecutionBackendKind(*Behavior));
}

int64 UFlightVerseSubsystem::GetLastGpuSubmissionHandle(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? Behavior->LastGpuSubmissionHandle : int64(0);
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

void UFlightVerseSubsystem::UpdateBehaviorCommitState(
	const uint32 BehaviorID,
	const FString& CommittedBackend,
	const FString& CommitDetail)
{
	FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return;
	}

	Behavior->CommittedBackend = CommittedBackend;
	Behavior->CommitDetail = CommitDetail;
	if (Behavior->bHasCompileArtifactReport)
	{
		Behavior->CompileArtifactReport.CommittedBackend = CommittedBackend;
		Behavior->CompileArtifactReport.CommitDetail = CommitDetail;
	}
	RefreshCapabilityEnvelopes();

	if (UWorld* World = GetWorld())
	{
		if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>())
		{
			Orchestration->Rebuild();
		}
	}
}

void UFlightVerseSubsystem::CommitBehaviorExecutionResult(const uint32 BehaviorID, const FBehaviorExecutionResult& Result)
{
	FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return;
	}

	Behavior->bHasLastExecutionResult = true;
	Behavior->bLastExecutionSucceeded = Result.bSucceeded;
	Behavior->bLastExecutionCommitted = Result.bCommitted;
	Behavior->LastSelectedChildBehaviorId = Result.SelectedChildBehaviorId;
	Behavior->LastBranchEvidence = Result.BranchEvidence;
	Behavior->LastGuardOutcomes = Result.GuardOutcomes;
	Behavior->LastExecutionDetail = Result.Detail;

	FString ExecutionCommitDetail = Result.bCommitted
		? Result.Detail
		: FString::Printf(TEXT("Last execution did not commit: %s"), *Result.Detail);
	if (Result.BranchEvidence.Num() > 0)
	{
		ExecutionCommitDetail += FString::Printf(TEXT(" BranchEvidence=[%s]."), *FString::Join(Result.BranchEvidence, TEXT(" | ")));
	}
	if (Result.GuardOutcomes.Num() > 0)
	{
		ExecutionCommitDetail += FString::Printf(TEXT(" GuardOutcomes=[%s]."), *FString::Join(Result.GuardOutcomes, TEXT(" | ")));
	}

	Behavior->CommitDetail = ExecutionCommitDetail;
	if (Result.bCommitted)
	{
		if (!Result.Backend.IsEmpty())
		{
			Behavior->CommittedBackend = Result.Backend;
		}
		else if (!Result.CommittedLane.IsEmpty())
		{
			Behavior->CommittedBackend = Result.CommittedLane;
		}
	}

	if (Behavior->bHasCompileArtifactReport)
	{
		if (Result.bCommitted && !Behavior->CommittedBackend.IsEmpty())
		{
			Behavior->CompileArtifactReport.CommittedBackend = Behavior->CommittedBackend;
		}
		Behavior->CompileArtifactReport.CommitDetail = ExecutionCommitDetail;
	}

	RefreshCapabilityEnvelopes();

	if (UWorld* World = GetWorld())
	{
		if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>())
		{
			Orchestration->Rebuild();
		}
	}
}

void UFlightVerseSubsystem::ResolveGpuExecutionCommit(
	const uint32 BehaviorID,
	const int64 SubmissionHandle,
	const bool bSuccess,
	const FString& Detail)
{
	FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return;
	}

	Behavior->LastGpuSubmissionHandle = SubmissionHandle;
	Behavior->LastGpuSubmissionDetail = Detail;
	Behavior->bGpuExecutionPending = false;

	UpdateBehaviorCommitState(
		BehaviorID,
		bSuccess ? TEXT("GpuKernel") : TEXT("Unknown"),
		bSuccess
			? FString::Printf(
				TEXT("GPU submission %llu reached a terminal completed state and is now the committed runtime backend. %s"),
				static_cast<unsigned long long>(SubmissionHandle),
				*Detail)
			: FString::Printf(
				TEXT("GPU submission %llu failed before proving a committed runtime backend. %s"),
				static_cast<unsigned long long>(SubmissionHandle),
				*Detail));
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

FString UFlightVerseSubsystem::SelectorGuardComparisonToString(const EFlightBehaviorGuardComparison Comparison)
{
	return SelectorGuardComparisonToStringLocal(Comparison);
}

FString UFlightVerseSubsystem::DescribeSelectorGuard(const FSelectorBranchGuard& Guard)
{
	switch (Guard.Comparison)
	{
	case EFlightBehaviorGuardComparison::IsTrue:
	case EFlightBehaviorGuardComparison::IsFalse:
		return FString::Printf(TEXT("%s %s"), *Guard.SymbolName, *SelectorGuardComparisonToStringLocal(Guard.Comparison));
	default:
		return FString::Printf(
			TEXT("%s %s %.3f"),
			*Guard.SymbolName,
			*SelectorGuardComparisonToStringLocal(Guard.Comparison),
			Guard.ScalarValue);
	}
}

bool UFlightVerseSubsystem::ValidateSelectorGuard(
	const FSelectorBranchGuard& Guard,
	const void* TypeKey,
	FString& OutError) const
{
	OutError.Reset();
	if (Guard.SymbolName.IsEmpty())
	{
		OutError = TEXT("Selector guard requires a non-empty symbol name.");
		return false;
	}

	if (const Flight::Vex::FVexTypeSchema* Schema = TypeKey
		? Flight::Vex::FVexSymbolRegistry::Get().GetSchema(TypeKey)
		: nullptr)
	{
		const Flight::Vex::FVexSymbolRecord* SymbolRecord = Schema->FindSymbolRecord(Guard.SymbolName);
		if (!SymbolRecord)
		{
			OutError = FString::Printf(TEXT("Selector guard symbol '%s' is not present in the bound schema."), *Guard.SymbolName);
			return false;
		}

		if (!SymbolRecord->bReadable)
		{
			OutError = FString::Printf(TEXT("Selector guard symbol '%s' is not readable."), *Guard.SymbolName);
			return false;
		}

		switch (SymbolRecord->ValueType)
		{
		case Flight::Vex::EVexValueType::Float:
		case Flight::Vex::EVexValueType::Int:
		case Flight::Vex::EVexValueType::Bool:
			return true;
		default:
			OutError = FString::Printf(
				TEXT("Selector guard symbol '%s' must be scalar-readable; current value type=%d."),
				*Guard.SymbolName,
				static_cast<int32>(SymbolRecord->ValueType));
			return false;
		}
	}

	if (TypeKey == GetDroidStateTypeKey())
	{
		const bool bKnownLegacySymbol = Guard.SymbolName == TEXT("@position")
			|| Guard.SymbolName == TEXT("@velocity")
			|| Guard.SymbolName == TEXT("@shield")
			|| Guard.SymbolName == TEXT("@status");
		if (!bKnownLegacySymbol)
		{
			OutError = FString::Printf(TEXT("Selector guard symbol '%s' is not supported on legacy FDroidState access."), *Guard.SymbolName);
		}
		return bKnownLegacySymbol;
	}

	OutError = FString::Printf(
		TEXT("Selector guard symbol '%s' could not be validated because no readable schema is registered for this type key."),
		*Guard.SymbolName);
	return false;
}

bool UFlightVerseSubsystem::TryReadRuntimeSymbolValue(
	const FResolvedStorageHost& Host,
	void* StructPtr,
	const FString& SymbolName,
	Flight::Vex::FVexRuntimeValue& OutValue,
	FString& OutError) const
{
	OutError.Reset();

	if (Host.Kind == EStorageHostKind::LegacyDroidState)
	{
		OutValue = ReadSymbolValue(SymbolName, *static_cast<const Flight::Swarm::FDroidState*>(StructPtr));
		return true;
	}

	if (!Host.Schema)
	{
		OutError = TEXT("Guard evaluation host does not have a readable schema.");
		return false;
	}

	const Flight::Vex::FVexSymbolRecord* SymbolRecord = Host.Schema->FindSymbolRecord(SymbolName);
	if (!SymbolRecord)
	{
		OutError = FString::Printf(TEXT("Guard symbol '%s' is not present in the resolved schema host."), *SymbolName);
		return false;
	}

	if (!SymbolRecord->bReadable)
	{
		OutError = FString::Printf(TEXT("Guard symbol '%s' is not readable on the resolved schema host."), *SymbolName);
		return false;
	}

	if (Host.Kind == EStorageHostKind::SchemaAos
		&& SymbolRecord->Storage.MemberOffset != INDEX_NONE)
	{
		const uint32 ElementStride = Flight::VerseRuntime::ResolveRuntimeValueStride(*SymbolRecord);
		const uint8* Bytes = reinterpret_cast<const uint8*>(StructPtr) + SymbolRecord->Storage.MemberOffset;
		OutValue = Flight::VerseRuntime::ReadRuntimeValueFromAddress(Bytes, SymbolRecord->ValueType, ElementStride);
		return true;
	}

	OutValue = SymbolRecord->ReadRuntimeValue(StructPtr);
	return true;
}

bool UFlightVerseSubsystem::EvaluateSelectorGuard(
	const FSelectorBranchGuard& Guard,
	const FResolvedStorageHost& Host,
	void* StructPtr,
	bool& bOutPassed,
	FString& OutDetail) const
{
	bOutPassed = false;

	Flight::Vex::FVexRuntimeValue Value;
	FString ReadError;
	if (!TryReadRuntimeSymbolValue(Host, StructPtr, Guard.SymbolName, Value, ReadError))
	{
		OutDetail = FString::Printf(TEXT("Guard '%s' could not be evaluated: %s"), *DescribeSelectorGuard(Guard), *ReadError);
		return false;
	}

	switch (Guard.Comparison)
	{
	case EFlightBehaviorGuardComparison::Less:
		bOutPassed = Value.AsFloat() < Guard.ScalarValue;
		break;
	case EFlightBehaviorGuardComparison::LessEqual:
		bOutPassed = Value.AsFloat() <= Guard.ScalarValue;
		break;
	case EFlightBehaviorGuardComparison::Greater:
		bOutPassed = Value.AsFloat() > Guard.ScalarValue;
		break;
	case EFlightBehaviorGuardComparison::GreaterEqual:
		bOutPassed = Value.AsFloat() >= Guard.ScalarValue;
		break;
	case EFlightBehaviorGuardComparison::Equal:
		bOutPassed = FMath::IsNearlyEqual(Value.AsFloat(), Guard.ScalarValue);
		break;
	case EFlightBehaviorGuardComparison::NotEqual:
		bOutPassed = !FMath::IsNearlyEqual(Value.AsFloat(), Guard.ScalarValue);
		break;
	case EFlightBehaviorGuardComparison::IsTrue:
		bOutPassed = Value.AsBool();
		break;
	case EFlightBehaviorGuardComparison::IsFalse:
		bOutPassed = !Value.AsBool();
		break;
	default:
		OutDetail = FString::Printf(TEXT("Guard '%s' uses an unknown comparison operator."), *DescribeSelectorGuard(Guard));
		return false;
	}

	OutDetail = FString::Printf(
		TEXT("Guard '%s' evaluated %s."),
		*DescribeSelectorGuard(Guard),
		bOutPassed ? TEXT("true") : TEXT("false"));
	return true;
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

	if (Behavior.SelectedBackend == VexBackendKindToString(EVexBackendKind::NativeAvx256x8))
	{
		return EVexBackendKind::NativeAvx256x8;
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
	const bool bCanUseAvx256x8 =
		bCanUseSimd
		&& Behavior.SimdPlan->SupportsExplicitAvx256x8()
		&& Flight::Vex::FVexSimdExecutor::HasExplicitAvx256x8KernelSupport()
		&& Flight::Vex::FVexSimdExecutor::IsExplicitAvx256x8RuntimeSupported();

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	const bool bCanUseVm = Behavior.bUsesVmEntryPoint && Behavior.Procedure.Get();
#else
	const bool bCanUseVm = false;
#endif

	if (const TOptional<EVexBackendKind> SelectedBackend = ResolveSelectedBackendKind(Behavior))
	{
		switch (SelectedBackend.GetValue())
		{
		case EVexBackendKind::NativeAvx256x8:
			if (bCanUseAvx256x8)
			{
				return EVexBackendKind::NativeAvx256x8;
			}
			break;
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

	if (bCanUseAvx256x8)
	{
		return EVexBackendKind::NativeAvx256x8;
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

	const FResolvedStorageHost Host = ResolveStorageHost(Behavior, Behavior.BoundTypeKey);
	const bool bCanUseGpu = Host.Kind == EStorageHostKind::GpuBuffer;
	const TOptional<EVexBackendKind> SelectedBackend = ResolveSelectedBackendKind(Behavior);
	if (!Behavior.bHasExecutableProcedure)
	{
		if (!(bCanUseGpu
			&& SelectedBackend.IsSet()
			&& SelectedBackend.GetValue() == EVexBackendKind::GpuKernel))
		{
			return {};
		}
	}

	const bool bCanUseNativeScalar = Behavior.bUsesNativeFallback && Host.Kind == EStorageHostKind::MassFragments;
	const bool bCanUseSimd = bCanUseNativeScalar && Behavior.Tier == EVexTier::Literal && Behavior.SimdPlan.IsValid();
	const bool bCanUseAvx256x8 =
		bCanUseNativeScalar
		&& Behavior.Tier == EVexTier::Literal
		&& Behavior.SimdPlan.IsValid()
		&& Behavior.SimdPlan->SupportsExplicitAvx256x8Direct()
		&& Flight::Vex::FVexSimdExecutor::HasExplicitAvx256x8KernelSupport()
		&& Flight::Vex::FVexSimdExecutor::IsExplicitAvx256x8RuntimeSupported();

	if (SelectedBackend.IsSet())
	{
		switch (SelectedBackend.GetValue())
		{
		case EVexBackendKind::NativeAvx256x8:
			if (bCanUseAvx256x8)
			{
				return EVexBackendKind::NativeAvx256x8;
			}
			break;
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

	if (bCanUseAvx256x8)
	{
		return EVexBackendKind::NativeAvx256x8;
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
	if (IsCompositeBehavior(Behavior))
	{
		return Behavior.CommittedBackend.IsEmpty() ? CompositeBehaviorBackendLabel(Behavior.Kind) : Behavior.CommittedBackend;
	}

	if (IsConcreteExecutionLane(Behavior.CommittedBackend))
	{
		return Behavior.CommittedBackend;
	}

	return DescribeRuntimeExecutionBackend(ResolveStructExecutionBackendKind(Behavior, RequestedTypeKey));
}

const void* UFlightVerseSubsystem::ResolveEffectiveBehaviorTypeKey(const FVerseBehavior& Behavior) const
{
	return Behavior.BoundTypeKey ? Behavior.BoundTypeKey : GetDroidStateTypeKey();
}

bool UFlightVerseSubsystem::IsCompositeBehavior(const FVerseBehavior& Behavior)
{
	return Behavior.Kind != EFlightVerseBehaviorKind::Atomic;
}

void UFlightVerseSubsystem::RefreshCapabilityEnvelopes()
{
	TSet<uint32> ActiveBehaviorIds;
	for (TPair<uint32, FVerseBehavior>& Pair : Behaviors)
	{
		Pair.Value.CapabilityEnvelope = BuildCapabilityEnvelope(Pair.Key, ActiveBehaviorIds);
	}
}

UFlightVerseSubsystem::FBehaviorCapabilityEnvelope UFlightVerseSubsystem::BuildCapabilityEnvelope(
	const uint32 BehaviorID,
	TSet<uint32>& ActiveBehaviorIds) const
{
	FBehaviorCapabilityEnvelope Envelope;

	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		Envelope.DisallowedLaneReasons.Add(FString::Printf(TEXT("Behavior %u is not registered."), BehaviorID));
		return Envelope;
	}

	if (ActiveBehaviorIds.Contains(BehaviorID))
	{
		Envelope.DisallowedLaneReasons.Add(FString::Printf(TEXT("Behavior %u introduces a composite capability cycle."), BehaviorID));
		return Envelope;
	}

	ActiveBehaviorIds.Add(BehaviorID);
	if (IsCompositeBehavior(*Behavior))
	{
		switch (Behavior->Kind)
		{
		case EFlightVerseBehaviorKind::Sequence:
			Envelope = BuildSequenceCapabilityEnvelope(*Behavior, ActiveBehaviorIds);
			break;
		case EFlightVerseBehaviorKind::Selector:
			Envelope = BuildSelectorCapabilityEnvelope(*Behavior, ActiveBehaviorIds);
			break;
		default:
			Envelope.DisallowedLaneReasons.Add(FString::Printf(TEXT("Behavior %u has an unsupported composite operator."), BehaviorID));
			break;
		}
	}
	else
	{
		Envelope = BuildAtomicCapabilityEnvelope(*Behavior);
	}
	ActiveBehaviorIds.Remove(BehaviorID);
	return Envelope;
}

UFlightVerseSubsystem::FBehaviorCapabilityEnvelope UFlightVerseSubsystem::BuildAtomicCapabilityEnvelope(const FVerseBehavior& Behavior) const
{
	using namespace Flight::Vex;

	FBehaviorCapabilityEnvelope Envelope;
	Envelope.PreferredLane = Behavior.SelectedBackend;
	Envelope.CommittedLane = Behavior.CommittedBackend == TEXT("Unknown")
		? FString()
		: Behavior.CommittedBackend;
	Envelope.bRequiresSharedTypeKey = Behavior.BoundTypeKey != nullptr;

	TSet<FString> LegalLaneSet;
	if (Behavior.SchemaBinding.IsSet() && Behavior.SchemaBinding->BackendLegality.Num() > 0)
	{
		for (const TPair<EVexBackendKind, bool>& Pair : Behavior.SchemaBinding->BackendLegality)
		{
			if (Pair.Value)
			{
				LegalLaneSet.Add(VexBackendKindToString(Pair.Key));
			}
		}
	}
	else
	{
		if (Behavior.bUsesNativeFallback)
		{
			LegalLaneSet.Add(NativeScalarLane);
		}
		if (Behavior.Tier == EVexTier::Literal && Behavior.SimdPlan.IsValid())
		{
			LegalLaneSet.Add(NativeSimdLane);
			if (Behavior.SimdPlan->SupportsExplicitAvx256x8()
				&& Flight::Vex::FVexSimdExecutor::HasExplicitAvx256x8KernelSupport())
			{
				LegalLaneSet.Add(NativeAvx256x8Lane);
			}
		}
		if (Behavior.bUsesVmEntryPoint)
		{
			LegalLaneSet.Add(VerseVmLane);
		}
		if (Behavior.SelectedBackend == GpuKernelLane
			|| Behavior.CommittedBackend == GpuKernelLane
			|| Behavior.bGpuExecutionPending)
		{
			LegalLaneSet.Add(GpuKernelLane);
		}
	}

	Envelope.LegalLanes = LegalLaneSet.Array();
	SortLanes(Envelope.LegalLanes);
	Envelope.ExecutionShape = DeriveExecutionShapeFromLanes(Envelope.LegalLanes, Envelope.PreferredLane);
	Envelope.CommittedExecutionShape = DeriveCommittedExecutionShape(Envelope.CommittedLane);

	if (Behavior.bHasCompileArtifactReport)
	{
		for (const Flight::Vex::FFlightCompileBackendReport& BackendReport : Behavior.CompileArtifactReport.BackendReports)
		{
			if (BackendReport.Decision == TEXT("Rejected"))
			{
				const FString ReasonText = BackendReport.Reasons.Num() > 0
					? FString::Join(BackendReport.Reasons, TEXT(" "))
					: TEXT("No reason recorded.");
				Envelope.DisallowedLaneReasons.Add(FString::Printf(TEXT("%s: %s"), *BackendReport.Backend, *ReasonText));
			}
		}
	}

	return Envelope;
}

UFlightVerseSubsystem::FBehaviorCapabilityEnvelope UFlightVerseSubsystem::BuildSequenceCapabilityEnvelope(
	const FVerseBehavior& Behavior,
	TSet<uint32>& ActiveBehaviorIds) const
{
	FBehaviorCapabilityEnvelope Envelope;
	Envelope.PreferredLane = Behavior.SelectedBackend;
	Envelope.bAllowsMixedLaneExecution = false;
	Envelope.bRequiresSharedTypeKey = true;

	if (Behavior.ChildBehaviorIds.IsEmpty())
	{
		Envelope.DisallowedLaneReasons.Add(TEXT("Sequence behavior has no child behaviors."));
		return Envelope;
	}

	bool bInitializedLegalLanes = false;
	TSet<FString> CommonLegalLanes;
	bool bAnyScalarOnlyChild = false;
	bool bAnyUnknownChildShape = false;
	bool bAllVectorPreferred = true;
	TSet<FString> CommonCommittedLanes;
	bool bInitializedCommittedLanes = false;

	for (const uint32 ChildBehaviorId : Behavior.ChildBehaviorIds)
	{
		const FVerseBehavior* ChildBehavior = Behaviors.Find(ChildBehaviorId);
		if (!ChildBehavior)
		{
			Envelope.DisallowedLaneReasons.Add(FString::Printf(TEXT("Missing child behavior %u."), ChildBehaviorId));
			continue;
		}

		const FBehaviorCapabilityEnvelope ChildEnvelope = BuildCapabilityEnvelope(ChildBehaviorId, ActiveBehaviorIds);
		Envelope.DisallowedLaneReasons.Append(ChildEnvelope.DisallowedLaneReasons);

		TSet<FString> ChildLegalLanes;
		for (const FString& Lane : ChildEnvelope.LegalLanes)
		{
			ChildLegalLanes.Add(Lane);
		}
		if (!bInitializedLegalLanes)
		{
			CommonLegalLanes = ChildLegalLanes;
			bInitializedLegalLanes = true;
		}
		else
		{
			for (auto It = CommonLegalLanes.CreateIterator(); It; ++It)
			{
				if (!ChildLegalLanes.Contains(*It))
				{
					Envelope.DisallowedLaneReasons.Add(FString::Printf(
						TEXT("Sequence lane '%s' dropped because child %u does not support it."),
						**It,
						ChildBehaviorId));
					It.RemoveCurrent();
				}
			}
		}

		switch (ChildEnvelope.ExecutionShape)
		{
		case EFlightBehaviorExecutionShape::ScalarOnly:
			bAnyScalarOnlyChild = true;
			bAllVectorPreferred = false;
			break;
		case EFlightBehaviorExecutionShape::VectorCapable:
			bAllVectorPreferred = false;
			break;
		case EFlightBehaviorExecutionShape::VectorPreferred:
			break;
		case EFlightBehaviorExecutionShape::Unknown:
			bAnyUnknownChildShape = true;
			bAllVectorPreferred = false;
			break;
		default:
			bAllVectorPreferred = false;
			break;
		}

		if (!ChildEnvelope.CommittedLane.IsEmpty())
		{
			TSet<FString> ChildCommitted;
			ChildCommitted.Add(ChildEnvelope.CommittedLane);
			if (!bInitializedCommittedLanes)
			{
				CommonCommittedLanes = ChildCommitted;
				bInitializedCommittedLanes = true;
			}
			else
			{
				for (auto It = CommonCommittedLanes.CreateIterator(); It; ++It)
				{
					if (!ChildCommitted.Contains(*It))
					{
						It.RemoveCurrent();
					}
				}
			}
		}
		else
		{
			CommonCommittedLanes.Reset();
		}
	}

	Envelope.LegalLanes = CommonLegalLanes.Array();
	SortLanes(Envelope.LegalLanes);
	if (Envelope.LegalLanes.Contains(Envelope.PreferredLane))
	{
		// keep compile-selected lane when it survives aggregation
	}
	else if (Envelope.LegalLanes.Num() > 0)
	{
		Envelope.PreferredLane = Envelope.LegalLanes[0];
	}
	else
	{
		Envelope.PreferredLane.Reset();
	}

	if (bInitializedCommittedLanes && CommonCommittedLanes.Num() == 1)
	{
		const TArray<FString> CommittedLanes = CommonCommittedLanes.Array();
		Envelope.CommittedLane = CommittedLanes[0];
	}
	if (IsConcreteExecutionLane(Behavior.CommittedBackend))
	{
		Envelope.CommittedLane = Behavior.CommittedBackend;
	}

	if (bAnyUnknownChildShape && Envelope.LegalLanes.IsEmpty())
	{
		Envelope.ExecutionShape = EFlightBehaviorExecutionShape::Unknown;
	}
	else if (bAnyScalarOnlyChild)
	{
		Envelope.ExecutionShape = EFlightBehaviorExecutionShape::ScalarOnly;
	}
	else if (Envelope.LegalLanes.ContainsByPredicate([](const FString& Lane)
	{
		return IsVectorExecutionLane(Lane);
	}))
	{
		Envelope.ExecutionShape = bAllVectorPreferred
			? EFlightBehaviorExecutionShape::VectorPreferred
			: EFlightBehaviorExecutionShape::VectorCapable;
	}
	else
	{
		Envelope.ExecutionShape = DeriveExecutionShapeFromLanes(Envelope.LegalLanes, Envelope.PreferredLane);
	}
	Envelope.CommittedExecutionShape = DeriveCommittedExecutionShape(Envelope.CommittedLane);

	return Envelope;
}

UFlightVerseSubsystem::FBehaviorCapabilityEnvelope UFlightVerseSubsystem::BuildSelectorCapabilityEnvelope(
	const FVerseBehavior& Behavior,
	TSet<uint32>& ActiveBehaviorIds) const
{
	FBehaviorCapabilityEnvelope Envelope;
	Envelope.PreferredLane = Behavior.SelectedBackend;
	Envelope.bAllowsMixedLaneExecution = true;
	Envelope.bRequiresSharedTypeKey = true;

	if (Behavior.ChildBehaviorIds.IsEmpty())
	{
		Envelope.DisallowedLaneReasons.Add(TEXT("Selector behavior has no child behaviors."));
		return Envelope;
	}

	TSet<FString> UnionLegalLanes;
	TSet<FString> CommonCommittedLanes;
	bool bInitializedCommittedLanes = false;
	EFlightBehaviorExecutionShape FirstChildShape = EFlightBehaviorExecutionShape::Unknown;
	bool bInitializedShape = false;
	bool bMixedShapes = false;

	for (const uint32 ChildBehaviorId : Behavior.ChildBehaviorIds)
	{
		const FVerseBehavior* ChildBehavior = Behaviors.Find(ChildBehaviorId);
		if (!ChildBehavior)
		{
			Envelope.DisallowedLaneReasons.Add(FString::Printf(TEXT("Missing child behavior %u."), ChildBehaviorId));
			continue;
		}

		const FBehaviorCapabilityEnvelope ChildEnvelope = BuildCapabilityEnvelope(ChildBehaviorId, ActiveBehaviorIds);
		Envelope.DisallowedLaneReasons.Append(ChildEnvelope.DisallowedLaneReasons);

		for (const FString& Lane : ChildEnvelope.LegalLanes)
		{
			UnionLegalLanes.Add(Lane);
		}

		if (Envelope.PreferredLane.IsEmpty() || !UnionLegalLanes.Contains(Envelope.PreferredLane))
		{
			if (!ChildEnvelope.PreferredLane.IsEmpty() && ChildEnvelope.LegalLanes.Contains(ChildEnvelope.PreferredLane))
			{
				Envelope.PreferredLane = ChildEnvelope.PreferredLane;
			}
			else if (ChildEnvelope.LegalLanes.Num() > 0 && Envelope.PreferredLane.IsEmpty())
			{
				Envelope.PreferredLane = ChildEnvelope.LegalLanes[0];
			}
		}

		if (!bInitializedShape)
		{
			FirstChildShape = ChildEnvelope.ExecutionShape;
			bInitializedShape = true;
		}
		else if (FirstChildShape != ChildEnvelope.ExecutionShape)
		{
			bMixedShapes = true;
		}

		if (!ChildEnvelope.CommittedLane.IsEmpty())
		{
			TSet<FString> ChildCommitted;
			ChildCommitted.Add(ChildEnvelope.CommittedLane);
			if (!bInitializedCommittedLanes)
			{
				CommonCommittedLanes = ChildCommitted;
				bInitializedCommittedLanes = true;
			}
			else
			{
				for (auto It = CommonCommittedLanes.CreateIterator(); It; ++It)
				{
					if (!ChildCommitted.Contains(*It))
					{
						It.RemoveCurrent();
					}
				}
			}
		}
		else
		{
			CommonCommittedLanes.Reset();
		}
	}

	Envelope.LegalLanes = UnionLegalLanes.Array();
	SortLanes(Envelope.LegalLanes);
	if (!Envelope.LegalLanes.Contains(Envelope.PreferredLane))
	{
		Envelope.PreferredLane = Envelope.LegalLanes.Num() > 0 ? Envelope.LegalLanes[0] : FString();
	}

	if (bInitializedCommittedLanes && CommonCommittedLanes.Num() == 1)
	{
		const TArray<FString> CommittedLanes = CommonCommittedLanes.Array();
		Envelope.CommittedLane = CommittedLanes[0];
	}
	if (IsConcreteExecutionLane(Behavior.CommittedBackend))
	{
		Envelope.CommittedLane = Behavior.CommittedBackend;
	}

	if (!bInitializedShape)
	{
		Envelope.ExecutionShape = EFlightBehaviorExecutionShape::Unknown;
	}
	else if (bMixedShapes)
	{
		Envelope.ExecutionShape = EFlightBehaviorExecutionShape::ShapeAgnostic;
	}
	else
	{
		Envelope.ExecutionShape = FirstChildShape;
	}
	Envelope.CommittedExecutionShape = DeriveCommittedExecutionShape(Envelope.CommittedLane);

	return Envelope;
}

UFlightVerseSubsystem::FBehaviorExecutionResult UFlightVerseSubsystem::ExecuteBehaviorWithResult(
	const uint32 BehaviorID,
	void* StructPtr,
	const void* TypeKey)
{
	FBehaviorExecutionResult Result;

	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		Result.FailureKind = EBehaviorExecutionFailureKind::MissingBehavior;
		Result.Detail = FString::Printf(TEXT("Behavior %u is not registered."), BehaviorID);
		return Result;
	}

	Result.LegalLanes = Behavior->CapabilityEnvelope.LegalLanes;
	Result.SelectedLane = Behavior->CapabilityEnvelope.PreferredLane;
	Result.ExecutionShape = Behavior->CapabilityEnvelope.ExecutionShape;
	Result.CommittedLane = Behavior->CapabilityEnvelope.CommittedLane;
	Result.CommittedExecutionShape = Behavior->CapabilityEnvelope.CommittedExecutionShape;

	if (IsCompositeBehavior(*Behavior))
	{
		return ExecuteCompositeBehaviorWithResult(BehaviorID, *Behavior, StructPtr, TypeKey);
	}

	return ExecuteAtomicBehaviorWithResult(BehaviorID, *Behavior, StructPtr, TypeKey);
}

UFlightVerseSubsystem::FBehaviorExecutionResult UFlightVerseSubsystem::ExecuteAtomicBehaviorWithResult(
	const uint32 BehaviorID,
	const FVerseBehavior& Behavior,
	void* StructPtr,
	const void* TypeKey)
{
	FBehaviorExecutionResult Result;
	Result.LegalLanes = Behavior.CapabilityEnvelope.LegalLanes;
	Result.SelectedLane = Behavior.CapabilityEnvelope.PreferredLane;
	Result.ExecutionShape = Behavior.CapabilityEnvelope.ExecutionShape;
	Result.CommittedLane = Behavior.CapabilityEnvelope.CommittedLane;
	Result.CommittedExecutionShape = Behavior.CapabilityEnvelope.CommittedExecutionShape;

	if (!Behavior.bHasExecutableProcedure)
	{
		Result.FailureKind = EBehaviorExecutionFailureKind::NonExecutable;
		Result.Detail = FString::Printf(TEXT("Behavior %u does not have an executable runtime path."), BehaviorID);
		return Result;
	}

	const FResolvedStorageHost Host = ResolveStorageHost(Behavior, TypeKey);
	const TOptional<Flight::Vex::EVexBackendKind> RuntimeBackend = ResolveStructExecutionBackendKind(Behavior, Host.TypeKey);

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::VerseVm
		&& Behavior.Procedure.Get())
	{
		VerseContext.EnterVM([&]()
		{
			Verse::FAllocationContext AllocContext(VerseContext);
			ExecuteBehaviorInContext(BehaviorID, Behavior, StructPtr, Host.TypeKey, AllocContext);
		});
		Result.bSucceeded = true;
		Result.bCommitted = true;
		Result.Backend = TEXT("VerseVm");
		Result.CommittedLane = TEXT("VerseVm");
		Result.CommittedExecutionShape = DeriveCommittedExecutionShape(Result.CommittedLane);
		Result.Detail = FString::Printf(TEXT("Behavior %u executed on Verse VM."), BehaviorID);
		return Result;
	}
#endif

	if (RuntimeBackend.IsSet()
		&& RuntimeBackend.GetValue() == Flight::Vex::EVexBackendKind::NativeScalar)
	{
		if (!ExecuteResolvedStorageHost(Host, Behavior, StructPtr))
		{
			Result.FailureKind = EBehaviorExecutionFailureKind::UnsupportedStorageHost;
			Result.Detail = FString::Printf(
				TEXT("Behavior %u resolved to NativeScalar, but no compatible storage host was available for type '%s'."),
				BehaviorID,
				Host.TypeName.IsNone() ? TEXT("<unknown>") : *Host.TypeName.ToString());
			UE_LOG(
				LogFlightVerseSubsystem,
				Warning,
				TEXT("%s"),
				*Result.Detail);
			return Result;
		}

		Result.bSucceeded = true;
		Result.bCommitted = true;
		Result.Backend = TEXT("NativeScalar");
		Result.CommittedLane = TEXT("NativeScalar");
		Result.CommittedExecutionShape = DeriveCommittedExecutionShape(Result.CommittedLane);
		Result.Detail = FString::Printf(TEXT("Behavior %u executed on the native scalar host."), BehaviorID);
		return Result;
	}

	Result.FailureKind = EBehaviorExecutionFailureKind::UnsupportedBackend;
	Result.Detail = FString::Printf(TEXT("Behavior %u does not have a supported struct execution backend."), BehaviorID);
	return Result;
}

UFlightVerseSubsystem::FBehaviorExecutionResult UFlightVerseSubsystem::ExecuteCompositeBehaviorWithResult(
	const uint32 BehaviorID,
	const FVerseBehavior& Behavior,
	void* StructPtr,
	const void* TypeKey)
{
	switch (Behavior.Kind)
	{
	case EFlightVerseBehaviorKind::Sequence:
		return ExecuteSequenceBehaviorWithResult(BehaviorID, Behavior, StructPtr, TypeKey);
	case EFlightVerseBehaviorKind::Selector:
		return ExecuteSelectorBehaviorWithResult(BehaviorID, Behavior, StructPtr, TypeKey);
	default:
		break;
	}

	FBehaviorExecutionResult Result;
	Result.FailureKind = EBehaviorExecutionFailureKind::UnknownCompositeOperator;
	Result.Detail = FString::Printf(TEXT("Behavior %u has an unknown composite operator."), BehaviorID);
	return Result;
}

UFlightVerseSubsystem::FBehaviorExecutionResult UFlightVerseSubsystem::ExecuteSequenceBehaviorWithResult(
	const uint32 BehaviorID,
	const FVerseBehavior& Behavior,
	void* StructPtr,
	const void* TypeKey)
{
	FBehaviorExecutionResult Result;
	Result.Backend = CompositeBehaviorBackendLabel(Behavior.Kind);
	Result.LegalLanes = Behavior.CapabilityEnvelope.LegalLanes;
	Result.SelectedLane = Behavior.CapabilityEnvelope.PreferredLane;
	Result.ExecutionShape = Behavior.CapabilityEnvelope.ExecutionShape;
	Result.CommittedLane = Behavior.CapabilityEnvelope.CommittedLane;
	Result.CommittedExecutionShape = Behavior.CapabilityEnvelope.CommittedExecutionShape;

	if (Behavior.ChildBehaviorIds.IsEmpty())
	{
		Result.FailureKind = EBehaviorExecutionFailureKind::NonExecutable;
		Result.Detail = FString::Printf(TEXT("Composite sequence behavior %u has no child behaviors."), BehaviorID);
		return Result;
	}

	const void* EffectiveTypeKey = TypeKey ? TypeKey : ResolveEffectiveBehaviorTypeKey(Behavior);

	for (const uint32 ChildBehaviorId : Behavior.ChildBehaviorIds)
	{
		const FVerseBehavior* ChildBehavior = Behaviors.Find(ChildBehaviorId);
		if (!ChildBehavior)
		{
			Result.FailureKind = EBehaviorExecutionFailureKind::MissingBehavior;
			Result.Detail = FString::Printf(
				TEXT("Composite sequence behavior %u is missing child behavior %u at execution time."),
				BehaviorID,
				ChildBehaviorId);
			return Result;
		}

		if (ResolveEffectiveBehaviorTypeKey(*ChildBehavior) != EffectiveTypeKey)
		{
			Result.FailureKind = EBehaviorExecutionFailureKind::IncompatibleTypeKey;
			Result.Detail = FString::Printf(
				TEXT("Composite sequence behavior %u encountered incompatible child behavior %u at execution time."),
				BehaviorID,
				ChildBehaviorId);
			return Result;
		}

		FBehaviorExecutionResult ChildResult = ExecuteBehaviorWithResult(ChildBehaviorId, StructPtr, EffectiveTypeKey);
		Result.ExecutedChildBehaviorIds.Add(ChildBehaviorId);
		Result.bCommitted = Result.bCommitted || ChildResult.bCommitted;
		Result.BranchEvidence.Append(ChildResult.BranchEvidence);
		Result.GuardOutcomes.Append(ChildResult.GuardOutcomes);
		if (!ChildResult.bSucceeded)
		{
			Result.FailureKind = EBehaviorExecutionFailureKind::ChildExecutionFailed;
			if (!ChildResult.CommittedLane.IsEmpty())
			{
				Result.CommittedLane = ChildResult.CommittedLane;
				Result.CommittedExecutionShape = ChildResult.CommittedExecutionShape;
			}
			Result.Detail = FString::Printf(
				TEXT("Composite sequence behavior %u failed on child %u: %s"),
				BehaviorID,
				ChildBehaviorId,
				*ChildResult.Detail);
			return Result;
		}

		if (Result.CommittedLane.IsEmpty() && !ChildResult.CommittedLane.IsEmpty())
		{
			Result.CommittedLane = ChildResult.CommittedLane;
			Result.CommittedExecutionShape = ChildResult.CommittedExecutionShape;
		}
	}

	Result.bSucceeded = true;
	Result.Detail = FString::Printf(
		TEXT("Composite sequence behavior %u executed child behaviors [%s]."),
		BehaviorID,
		*BuildChildBehaviorListString(Result.ExecutedChildBehaviorIds));
	return Result;
}

UFlightVerseSubsystem::FBehaviorExecutionResult UFlightVerseSubsystem::ExecuteSelectorBehaviorWithResult(
	const uint32 BehaviorID,
	const FVerseBehavior& Behavior,
	void* StructPtr,
	const void* TypeKey)
{
	FBehaviorExecutionResult Result;
	Result.Backend = CompositeBehaviorBackendLabel(Behavior.Kind);
	Result.LegalLanes = Behavior.CapabilityEnvelope.LegalLanes;
	Result.SelectedLane = Behavior.CapabilityEnvelope.PreferredLane;
	Result.ExecutionShape = Behavior.CapabilityEnvelope.ExecutionShape;
	Result.CommittedLane = Behavior.CapabilityEnvelope.CommittedLane;
	Result.CommittedExecutionShape = Behavior.CapabilityEnvelope.CommittedExecutionShape;

	if (Behavior.ChildBehaviorIds.IsEmpty())
	{
		Result.FailureKind = EBehaviorExecutionFailureKind::NonExecutable;
		Result.Detail = FString::Printf(TEXT("Composite selector behavior %u has no child behaviors."), BehaviorID);
		return Result;
	}

	const void* EffectiveTypeKey = TypeKey ? TypeKey : ResolveEffectiveBehaviorTypeKey(Behavior);
	const FResolvedStorageHost Host = ResolveStorageHost(Behavior, EffectiveTypeKey);
	TArray<FString> BranchFailures;
	bool bAnyGuardEvaluationFailure = false;

	for (int32 ChildIndex = 0; ChildIndex < Behavior.ChildBehaviorIds.Num(); ++ChildIndex)
	{
		const uint32 ChildBehaviorId = Behavior.ChildBehaviorIds[ChildIndex];
		const FCompositeSelectorBranchSpec* BranchSpec = Behavior.SelectorBranches.IsValidIndex(ChildIndex)
			? &Behavior.SelectorBranches[ChildIndex]
			: nullptr;
		const FVerseBehavior* ChildBehavior = Behaviors.Find(ChildBehaviorId);
		if (!ChildBehavior)
		{
			BranchFailures.Add(FString::Printf(TEXT("child %u missing at execution time"), ChildBehaviorId));
			continue;
		}

		if (ResolveEffectiveBehaviorTypeKey(*ChildBehavior) != EffectiveTypeKey)
		{
			BranchFailures.Add(FString::Printf(TEXT("child %u has an incompatible type key"), ChildBehaviorId));
			continue;
		}

		if (BranchSpec && BranchSpec->bHasGuard)
		{
			bool bGuardPassed = false;
			FString GuardDetail;
			if (!EvaluateSelectorGuard(BranchSpec->Guard, Host, StructPtr, bGuardPassed, GuardDetail))
			{
				bAnyGuardEvaluationFailure = true;
				Result.bSemanticFailure = true;
				Result.BranchEvidence.Add(GuardDetail);
				Result.GuardOutcomes.Add(GuardDetail);
				BranchFailures.Add(FString::Printf(TEXT("child %u guard evaluation failed: %s"), ChildBehaviorId, *GuardDetail));
				continue;
			}

			Result.BranchEvidence.Add(GuardDetail);
			Result.GuardOutcomes.Add(GuardDetail);
			if (!bGuardPassed)
			{
				Result.bSemanticFailure = true;
				BranchFailures.Add(FString::Printf(TEXT("child %u guard rejected"), ChildBehaviorId));
				continue;
			}
		}

		FBehaviorExecutionResult ChildResult = ExecuteBehaviorWithResult(ChildBehaviorId, StructPtr, EffectiveTypeKey);
		Result.ExecutedChildBehaviorIds.Add(ChildBehaviorId);
		Result.BranchEvidence.Append(ChildResult.BranchEvidence);
		Result.GuardOutcomes.Append(ChildResult.GuardOutcomes);
		if (ChildResult.bSucceeded)
		{
			Result.bSucceeded = true;
			Result.bCommitted = ChildResult.bCommitted;
			Result.bSemanticFailure = ChildResult.bSemanticFailure;
			Result.FailureKind = EBehaviorExecutionFailureKind::None;
			Result.SelectedChildBehaviorId = ChildBehaviorId;
			Result.SelectedLane = ChildResult.SelectedLane;
			Result.CommittedLane = ChildResult.CommittedLane;
			Result.CommittedExecutionShape = ChildResult.CommittedExecutionShape;
			if (!ChildResult.Backend.IsEmpty())
			{
				Result.Backend = ChildResult.Backend;
			}
			Result.Detail = BranchFailures.Num() == 0
				? FString::Printf(TEXT("Composite selector behavior %u chose child behavior %u."), BehaviorID, ChildBehaviorId)
				: FString::Printf(
					TEXT("Composite selector behavior %u chose child behavior %u after fallback over [%s]."),
					BehaviorID,
					ChildBehaviorId,
					*FString::Join(BranchFailures, TEXT("; ")));
			return Result;
		}

		Result.bSemanticFailure = Result.bSemanticFailure || ChildResult.bSemanticFailure;
		BranchFailures.Add(FString::Printf(TEXT("child %u failed: %s"), ChildBehaviorId, *ChildResult.Detail));
	}

	Result.FailureKind = bAnyGuardEvaluationFailure
		? EBehaviorExecutionFailureKind::GuardEvaluationFailed
		: (Result.bSemanticFailure
			? EBehaviorExecutionFailureKind::GuardRejected
			: EBehaviorExecutionFailureKind::ChildExecutionFailed);
	Result.Detail = FString::Printf(
		TEXT("Composite selector behavior %u did not find a successful child branch. Attempts=[%s]"),
		BehaviorID,
		*FString::Join(BranchFailures, TEXT("; ")));
	return Result;
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
