// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightCompileArtifacts.h"

#include "Vex/FlightVexIr.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Flight::Vex
{
namespace
{

bool IsIntrinsicOp(const EVexIrOp Op)
{
	switch (Op)
	{
	case EVexIrOp::Normalize:
	case EVexIrOp::Sin:
	case EVexIrOp::Cos:
	case EVexIrOp::Log:
	case EVexIrOp::Exp:
	case EVexIrOp::Pow:
	case EVexIrOp::VectorCompose:
		return true;
	default:
		return false;
	}
}

int32 GetJumpTarget(const FVexIrInstruction& Instruction)
{
	if (Instruction.Op == EVexIrOp::Jump && Instruction.Args.Num() > 0)
	{
		return Instruction.Args[0].Index;
	}

	if (Instruction.Op == EVexIrOp::JumpIf && Instruction.Args.Num() > 1)
	{
		return Instruction.Args[1].Index;
	}

	return INDEX_NONE;
}

int32 ResolveRegisterCount(const FVexIrProgram& Program)
{
	int32 MaxRegisterIndex = Program.MaxRegisters - 1;

	for (const FVexIrInstruction& Instruction : Program.Instructions)
	{
		if (Instruction.Dest.Kind == FVexIrValue::EKind::Register)
		{
			MaxRegisterIndex = FMath::Max(MaxRegisterIndex, Instruction.Dest.Index);
		}

		for (const FVexIrValue& Arg : Instruction.Args)
		{
			if (Arg.Kind == FVexIrValue::EKind::Register)
			{
				MaxRegisterIndex = FMath::Max(MaxRegisterIndex, Arg.Index);
			}
		}
	}

	return FMath::Max(0, MaxRegisterIndex + 1);
}

EFlightPressureClass ResolvePressureClass(const int32 PeakLiveTemps)
{
	if (PeakLiveTemps <= 0)
	{
		return EFlightPressureClass::Unknown;
	}
	if (PeakLiveTemps <= 4)
	{
		return EFlightPressureClass::Low;
	}
	if (PeakLiveTemps <= 12)
	{
		return EFlightPressureClass::Medium;
	}
	if (PeakLiveTemps <= 24)
	{
		return EFlightPressureClass::High;
	}
	return EFlightPressureClass::Critical;
}

EFlightWorkingSetClass ResolveWorkingSetClass(const int32 WorkingSetFootprint)
{
	if (WorkingSetFootprint <= 0)
	{
		return EFlightWorkingSetClass::Unknown;
	}
	if (WorkingSetFootprint <= 8)
	{
		return EFlightWorkingSetClass::Tiny;
	}
	if (WorkingSetFootprint <= 16)
	{
		return EFlightWorkingSetClass::Small;
	}
	if (WorkingSetFootprint <= 32)
	{
		return EFlightWorkingSetClass::Medium;
	}
	return EFlightWorkingSetClass::Large;
}

EFlightLocalityClass ResolveLocalityClass(const FFlightCodeShapeMetrics& Metrics)
{
	if (Metrics.GatherScatterCount > 0)
	{
		return EFlightLocalityClass::GatherScatter;
	}
	if (Metrics.LoadCount == 0 && Metrics.StoreCount == 0)
	{
		return EFlightLocalityClass::HotScalar;
	}
	if (Metrics.StoreCount == 0)
	{
		return EFlightLocalityClass::Streaming;
	}
	if (Metrics.LoadCount > 0 && Metrics.StoreCount > 0 && Metrics.SymbolCount <= 2)
	{
		return EFlightLocalityClass::Strided;
	}
	return EFlightLocalityClass::Mixed;
}

void AddArtifactKindArrayToJson(const TArray<EFlightCompileArtifactKind>& Kinds, const TCHAR* FieldName, TSharedRef<FJsonObject> Object)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const EFlightCompileArtifactKind Kind : Kinds)
	{
		Values.Add(MakeShared<FJsonValueString>(CompileArtifactKindToString(Kind)));
	}
	Object->SetArrayField(FieldName, Values);
}

void AddStringArrayToJson(const TArray<FString>& Strings, const TCHAR* FieldName, TSharedRef<FJsonObject> Object)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& Value : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(Value));
	}
	Object->SetArrayField(FieldName, Values);
}

void AddBackendReportsToJson(const TArray<FFlightCompileBackendReport>& Reports, const TCHAR* FieldName, TSharedRef<FJsonObject> Object)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FFlightCompileBackendReport& Report : Reports)
	{
		TSharedRef<FJsonObject> BackendObject = MakeShared<FJsonObject>();
		BackendObject->SetStringField(TEXT("backend"), Report.Backend);
		BackendObject->SetStringField(TEXT("decision"), Report.Decision);

		TArray<TSharedPtr<FJsonValue>> ReasonValues;
		for (const FString& Reason : Report.Reasons)
		{
			ReasonValues.Add(MakeShared<FJsonValueString>(Reason));
		}

		BackendObject->SetArrayField(TEXT("reasons"), ReasonValues);
		Values.Add(MakeShared<FJsonValueObject>(BackendObject));
	}
	Object->SetArrayField(FieldName, Values);
}

} // namespace

FString CompileArtifactKindToString(const EFlightCompileArtifactKind Kind)
{
	switch (Kind)
	{
	case EFlightCompileArtifactKind::CompileTelemetry:
		return TEXT("CompileTelemetry");
	case EFlightCompileArtifactKind::IrProgram:
		return TEXT("IrProgram");
	case EFlightCompileArtifactKind::VvmBytecode:
		return TEXT("VvmBytecode");
	case EFlightCompileArtifactKind::HlslText:
		return TEXT("HlslText");
	case EFlightCompileArtifactKind::SpirvDisassembly:
		return TEXT("SpirvDisassembly");
	case EFlightCompileArtifactKind::NativeAssembly:
		return TEXT("NativeAssembly");
	default:
		return TEXT("Unknown");
	}
}

FString PressureClassToString(const EFlightPressureClass Class)
{
	switch (Class)
	{
	case EFlightPressureClass::Low:
		return TEXT("Low");
	case EFlightPressureClass::Medium:
		return TEXT("Medium");
	case EFlightPressureClass::High:
		return TEXT("High");
	case EFlightPressureClass::Critical:
		return TEXT("Critical");
	default:
		return TEXT("Unknown");
	}
}

FString LocalityClassToString(const EFlightLocalityClass Class)
{
	switch (Class)
	{
	case EFlightLocalityClass::HotScalar:
		return TEXT("HotScalar");
	case EFlightLocalityClass::Streaming:
		return TEXT("Streaming");
	case EFlightLocalityClass::Strided:
		return TEXT("Strided");
	case EFlightLocalityClass::GatherScatter:
		return TEXT("GatherScatter");
	case EFlightLocalityClass::Mixed:
		return TEXT("Mixed");
	default:
		return TEXT("Unknown");
	}
}

FString WorkingSetClassToString(const EFlightWorkingSetClass Class)
{
	switch (Class)
	{
	case EFlightWorkingSetClass::Tiny:
		return TEXT("Tiny");
	case EFlightWorkingSetClass::Small:
		return TEXT("Small");
	case EFlightWorkingSetClass::Medium:
		return TEXT("Medium");
	case EFlightWorkingSetClass::Large:
		return TEXT("Large");
	default:
		return TEXT("Unknown");
	}
}

FString ColdStartClassToString(const EFlightColdStartClass Class)
{
	switch (Class)
	{
	case EFlightColdStartClass::Cheap:
		return TEXT("Cheap");
	case EFlightColdStartClass::Moderate:
		return TEXT("Moderate");
	case EFlightColdStartClass::Expensive:
		return TEXT("Expensive");
	default:
		return TEXT("Unknown");
	}
}

FString VexTierToString(const EVexTier Tier)
{
	switch (Tier)
	{
	case EVexTier::Literal:
		return TEXT("Literal");
	case EVexTier::DFA:
		return TEXT("DFA");
	case EVexTier::Full:
		return TEXT("Full");
	default:
		return TEXT("Unknown");
	}
}

FString VexStorageKindToString(const EVexStorageKind Kind)
{
	switch (Kind)
	{
	case EVexStorageKind::None:
		return TEXT("None");
	case EVexStorageKind::AosOffset:
		return TEXT("AosOffset");
	case EVexStorageKind::Accessor:
		return TEXT("Accessor");
	case EVexStorageKind::MassFragmentField:
		return TEXT("MassFragmentField");
	case EVexStorageKind::SoaColumn:
		return TEXT("SoaColumn");
	case EVexStorageKind::GpuBufferElement:
		return TEXT("GpuBufferElement");
	case EVexStorageKind::ExternalProvider:
		return TEXT("ExternalProvider");
	default:
		return TEXT("Unknown");
	}
}

FFlightCodeShapeMetrics AnalyzeIrCodeShape(const FVexIrProgram& Program, const bool bAsync)
{
	FFlightCodeShapeMetrics Metrics;
	Metrics.InstructionCount = Program.Instructions.Num();
	Metrics.ConstantCount = Program.Constants.Num();
	Metrics.SymbolCount = Program.SymbolNames.Num();
	Metrics.SuspendCapableOpCount = bAsync ? 1 : 0;

	const int32 RegisterCount = ResolveRegisterCount(Program);
	TArray<int32> FirstDef;
	TArray<int32> LastUse;
	FirstDef.Init(INDEX_NONE, RegisterCount);
	LastUse.Init(INDEX_NONE, RegisterCount);

	TSet<int32> BlockLeaders;
	if (Program.Instructions.Num() > 0)
	{
		BlockLeaders.Add(0);
	}

	for (int32 InstructionIndex = 0; InstructionIndex < Program.Instructions.Num(); ++InstructionIndex)
	{
		const FVexIrInstruction& Instruction = Program.Instructions[InstructionIndex];

		switch (Instruction.Op)
		{
		case EVexIrOp::LoadSymbol:
			++Metrics.LoadCount;
			++Metrics.HelperThunkCount;
			break;
		case EVexIrOp::StoreSymbol:
			++Metrics.StoreCount;
			++Metrics.HelperThunkCount;
			break;
		case EVexIrOp::Jump:
		case EVexIrOp::JumpIf:
			++Metrics.BranchCount;
			break;
		default:
			break;
		}

		if (IsIntrinsicOp(Instruction.Op))
		{
			++Metrics.IntrinsicOpCount;
		}

		const int32 JumpTarget = GetJumpTarget(Instruction);
		if (JumpTarget != INDEX_NONE)
		{
			BlockLeaders.Add(JumpTarget);
			if (JumpTarget < InstructionIndex)
			{
				++Metrics.LoopBackedgeCount;
			}
		}

		if ((Instruction.Op == EVexIrOp::Jump || Instruction.Op == EVexIrOp::JumpIf || Instruction.Op == EVexIrOp::Return)
			&& InstructionIndex + 1 < Program.Instructions.Num())
		{
			BlockLeaders.Add(InstructionIndex + 1);
		}

		if (Instruction.Dest.Kind == FVexIrValue::EKind::Register
			&& Instruction.Dest.Index >= 0
			&& Instruction.Dest.Index < RegisterCount)
		{
			if (FirstDef[Instruction.Dest.Index] == INDEX_NONE)
			{
				FirstDef[Instruction.Dest.Index] = InstructionIndex;
			}
			LastUse[Instruction.Dest.Index] = FMath::Max(LastUse[Instruction.Dest.Index], InstructionIndex);
		}

		for (const FVexIrValue& Arg : Instruction.Args)
		{
			if (Arg.Kind == FVexIrValue::EKind::Register
				&& Arg.Index >= 0
				&& Arg.Index < RegisterCount)
			{
				if (FirstDef[Arg.Index] == INDEX_NONE)
				{
					FirstDef[Arg.Index] = InstructionIndex;
				}
				LastUse[Arg.Index] = FMath::Max(LastUse[Arg.Index], InstructionIndex);
			}
		}
	}

	Metrics.BlockCount = BlockLeaders.Num();

	int32 TotalLiveTemps = 0;
	for (int32 InstructionIndex = 0; InstructionIndex < Program.Instructions.Num(); ++InstructionIndex)
	{
		int32 LiveTempsAtInstruction = 0;
		for (int32 RegisterIndex = 0; RegisterIndex < RegisterCount; ++RegisterIndex)
		{
			if (FirstDef[RegisterIndex] != INDEX_NONE
				&& FirstDef[RegisterIndex] <= InstructionIndex
				&& LastUse[RegisterIndex] >= InstructionIndex)
			{
				++LiveTempsAtInstruction;
			}
		}

		TotalLiveTemps += LiveTempsAtInstruction;
		Metrics.PeakLiveTemps = FMath::Max(Metrics.PeakLiveTemps, LiveTempsAtInstruction);
	}

	if (Program.Instructions.Num() > 0)
	{
		Metrics.AverageLiveTemps = static_cast<float>(TotalLiveTemps) / static_cast<float>(Program.Instructions.Num());
	}

	Metrics.PressureClass = ResolvePressureClass(Metrics.PeakLiveTemps);
	Metrics.WorkingSetClass = ResolveWorkingSetClass(RegisterCount + Metrics.ConstantCount + Metrics.SymbolCount);
	Metrics.LocalityClass = ResolveLocalityClass(Metrics);
	return Metrics;
}

void FinalizeWarmupMetrics(FFlightWarmupMetrics& Metrics)
{
	if (Metrics.TotalCompileTimeMs <= 0.0)
	{
		Metrics.ColdStartClass = EFlightColdStartClass::Unknown;
		return;
	}

	if (Metrics.TotalCompileTimeMs <= 1.0)
	{
		Metrics.ColdStartClass = EFlightColdStartClass::Cheap;
		return;
	}

	if (Metrics.TotalCompileTimeMs <= 5.0)
	{
		Metrics.ColdStartClass = EFlightColdStartClass::Moderate;
		return;
	}

	Metrics.ColdStartClass = EFlightColdStartClass::Expensive;
}

FString BuildCompileArtifactReportJson(const FFlightCompileArtifactReport& Report)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schemaVersion"), Report.SchemaVersion);
	Root->SetStringField(TEXT("generatedAtUtc"), Report.GeneratedAtUtc.ToIso8601());
	Root->SetNumberField(TEXT("behaviorId"), static_cast<double>(Report.BehaviorID));
	Root->SetNumberField(TEXT("sourceHash"), static_cast<double>(Report.SourceHash));
	Root->SetStringField(TEXT("targetFingerprint"), Report.TargetFingerprint);
	Root->SetStringField(TEXT("boundTypeName"), Report.BoundTypeName);
	Root->SetNumberField(TEXT("schemaLayoutHash"), static_cast<double>(Report.SchemaLayoutHash));
	Root->SetStringField(TEXT("compileOutcome"), Report.CompileOutcome);
	Root->SetStringField(TEXT("backendPath"), Report.BackendPath);
	Root->SetStringField(TEXT("selectedBackend"), Report.SelectedBackend);
	Root->SetStringField(TEXT("committedBackend"), Report.CommittedBackend);
	Root->SetStringField(TEXT("commitDetail"), Report.CommitDetail);
	Root->SetStringField(TEXT("selectedPolicyRow"), Report.SelectedPolicyRow);
	Root->SetStringField(TEXT("policyPreferredDomain"), Report.PolicyPreferredDomain);
	Root->SetBoolField(TEXT("policyAllowsNativeFallback"), Report.bPolicyAllowsNativeFallback);
	Root->SetBoolField(TEXT("policyAllowsGeneratedOnly"), Report.bPolicyAllowsGeneratedOnly);
	Root->SetBoolField(TEXT("policyPrefersAsync"), Report.bPolicyPrefersAsync);
	Root->SetStringField(TEXT("tier"), VexTierToString(Report.Tier));
	Root->SetBoolField(TEXT("async"), Report.bAsync);
	Root->SetBoolField(TEXT("hasIr"), Report.bHasIr);
	Root->SetBoolField(TEXT("usesNativeFallback"), Report.bUsesNativeFallback);
	Root->SetBoolField(TEXT("usesVmEntryPoint"), Report.bUsesVmEntryPoint);
	Root->SetStringField(TEXT("diagnostics"), Report.Diagnostics);
	Root->SetStringField(TEXT("generatedVerseCode"), Report.GeneratedVerseCode);
	Root->SetStringField(TEXT("irCompileErrors"), Report.IrCompileErrors);
	AddArtifactKindArrayToJson(Report.AvailableArtifacts, TEXT("availableArtifacts"), Root);
	AddBackendReportsToJson(Report.BackendReports, TEXT("backendReports"), Root);
	AddStringArrayToJson(Report.ReadSymbols, TEXT("readSymbols"), Root);
	AddStringArrayToJson(Report.WrittenSymbols, TEXT("writtenSymbols"), Root);
	AddStringArrayToJson(Report.ImportedSymbols, TEXT("importedSymbols"), Root);
	AddStringArrayToJson(Report.ExportedSymbols, TEXT("exportedSymbols"), Root);
	AddStringArrayToJson(Report.ReferencedStorageKinds, TEXT("referencedStorageKinds"), Root);
	AddStringArrayToJson(Report.PolicyRequiredContracts, TEXT("policyRequiredContracts"), Root);
	AddStringArrayToJson(Report.PolicyRequiredSymbols, TEXT("policyRequiredSymbols"), Root);
	Root->SetNumberField(TEXT("boundaryOperatorCount"), static_cast<double>(Report.BoundaryOperatorCount));
	Root->SetBoolField(TEXT("hasBoundaryOperators"), Report.bHasBoundaryOperators);
	Root->SetBoolField(TEXT("hasAwaitableBoundary"), Report.bHasAwaitableBoundary);
	Root->SetBoolField(TEXT("hasMirrorRequest"), Report.bHasMirrorRequest);

	TSharedRef<FJsonObject> CodeShapeObject = MakeShared<FJsonObject>();
	CodeShapeObject->SetNumberField(TEXT("instructionCount"), Report.CodeShapeMetrics.InstructionCount);
	CodeShapeObject->SetNumberField(TEXT("blockCount"), Report.CodeShapeMetrics.BlockCount);
	CodeShapeObject->SetNumberField(TEXT("branchCount"), Report.CodeShapeMetrics.BranchCount);
	CodeShapeObject->SetNumberField(TEXT("loopBackedgeCount"), Report.CodeShapeMetrics.LoopBackedgeCount);
	CodeShapeObject->SetNumberField(TEXT("callCount"), Report.CodeShapeMetrics.CallCount);
	CodeShapeObject->SetNumberField(TEXT("indirectCallCount"), Report.CodeShapeMetrics.IndirectCallCount);
	CodeShapeObject->SetNumberField(TEXT("helperThunkCount"), Report.CodeShapeMetrics.HelperThunkCount);
	CodeShapeObject->SetNumberField(TEXT("intrinsicOpCount"), Report.CodeShapeMetrics.IntrinsicOpCount);
	CodeShapeObject->SetNumberField(TEXT("peakLiveTemps"), Report.CodeShapeMetrics.PeakLiveTemps);
	CodeShapeObject->SetNumberField(TEXT("averageLiveTemps"), Report.CodeShapeMetrics.AverageLiveTemps);
	CodeShapeObject->SetNumberField(TEXT("loadCount"), Report.CodeShapeMetrics.LoadCount);
	CodeShapeObject->SetNumberField(TEXT("storeCount"), Report.CodeShapeMetrics.StoreCount);
	CodeShapeObject->SetNumberField(TEXT("gatherScatterCount"), Report.CodeShapeMetrics.GatherScatterCount);
	CodeShapeObject->SetNumberField(TEXT("suspendCapableOpCount"), Report.CodeShapeMetrics.SuspendCapableOpCount);
	CodeShapeObject->SetNumberField(TEXT("constantCount"), Report.CodeShapeMetrics.ConstantCount);
	CodeShapeObject->SetNumberField(TEXT("symbolCount"), Report.CodeShapeMetrics.SymbolCount);
	CodeShapeObject->SetStringField(TEXT("pressureClass"), PressureClassToString(Report.CodeShapeMetrics.PressureClass));
	CodeShapeObject->SetStringField(TEXT("localityClass"), LocalityClassToString(Report.CodeShapeMetrics.LocalityClass));
	CodeShapeObject->SetStringField(TEXT("workingSetClass"), WorkingSetClassToString(Report.CodeShapeMetrics.WorkingSetClass));
	Root->SetObjectField(TEXT("codeShapeMetrics"), CodeShapeObject);

	TSharedRef<FJsonObject> WarmupObject = MakeShared<FJsonObject>();
	WarmupObject->SetNumberField(TEXT("parseTimeMs"), Report.WarmupMetrics.ParseTimeMs);
	WarmupObject->SetNumberField(TEXT("irCompileTimeMs"), Report.WarmupMetrics.IrCompileTimeMs);
	WarmupObject->SetNumberField(TEXT("vmAssembleTimeMs"), Report.WarmupMetrics.VmAssembleTimeMs);
	WarmupObject->SetNumberField(TEXT("totalCompileTimeMs"), Report.WarmupMetrics.TotalCompileTimeMs);
	WarmupObject->SetNumberField(TEXT("firstInvokeTimeMs"), Report.WarmupMetrics.FirstInvokeTimeMs);
	WarmupObject->SetNumberField(TEXT("warmInvokeTimeMs"), Report.WarmupMetrics.WarmInvokeTimeMs);
	WarmupObject->SetNumberField(TEXT("firstResourceInitTimeMs"), Report.WarmupMetrics.FirstResourceInitTimeMs);
	WarmupObject->SetNumberField(TEXT("registryMissCount"), Report.WarmupMetrics.RegistryMissCount);
	WarmupObject->SetNumberField(TEXT("cacheFillCount"), Report.WarmupMetrics.CacheFillCount);
	WarmupObject->SetStringField(TEXT("coldStartClass"), ColdStartClassToString(Report.WarmupMetrics.ColdStartClass));
	Root->SetObjectField(TEXT("warmupMetrics"), WarmupObject);

	TSharedRef<FJsonObject> CompressionObject = MakeShared<FJsonObject>();
	CompressionObject->SetBoolField(TEXT("baselineAvailable"), Report.CompressionSummary.bBaselineAvailable);
	CompressionObject->SetNumberField(TEXT("instructionCompressionRatio"), Report.CompressionSummary.InstructionCompressionRatio);
	CompressionObject->SetNumberField(TEXT("callCompressionRatio"), Report.CompressionSummary.CallCompressionRatio);
	CompressionObject->SetNumberField(TEXT("liveRangeCompressionRatio"), Report.CompressionSummary.LiveRangeCompressionRatio);
	CompressionObject->SetNumberField(TEXT("memoryTrafficCompressionRatio"), Report.CompressionSummary.MemoryTrafficCompressionRatio);
	CompressionObject->SetNumberField(TEXT("stateTransitionCompressionRatio"), Report.CompressionSummary.StateTransitionCompressionRatio);
	Root->SetObjectField(TEXT("compressionSummary"), CompressionObject);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

} // namespace Flight::Vex
