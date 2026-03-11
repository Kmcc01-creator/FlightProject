// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexOptics.h"
#include "Vex/FlightVexSchemaTypes.h"

namespace Flight::Vex
{

struct FVexIrProgram;

enum class EFlightCompileArtifactKind : uint8
{
	CompileTelemetry,
	IrProgram,
	VvmBytecode,
	HlslText,
	SpirvDisassembly,
	NativeAssembly
};

enum class EFlightPressureClass : uint8
{
	Unknown,
	Low,
	Medium,
	High,
	Critical
};

enum class EFlightLocalityClass : uint8
{
	Unknown,
	HotScalar,
	Streaming,
	Strided,
	GatherScatter,
	Mixed
};

enum class EFlightWorkingSetClass : uint8
{
	Unknown,
	Tiny,
	Small,
	Medium,
	Large
};

enum class EFlightColdStartClass : uint8
{
	Unknown,
	Cheap,
	Moderate,
	Expensive
};

struct FLIGHTPROJECT_API FFlightCodeShapeMetrics
{
	int32 InstructionCount = 0;
	int32 BlockCount = 0;
	int32 BranchCount = 0;
	int32 LoopBackedgeCount = 0;
	int32 CallCount = 0;
	int32 IndirectCallCount = 0;
	int32 HelperThunkCount = 0;
	int32 IntrinsicOpCount = 0;
	int32 PeakLiveTemps = 0;
	float AverageLiveTemps = 0.0f;
	int32 LoadCount = 0;
	int32 StoreCount = 0;
	int32 GatherScatterCount = 0;
	int32 SuspendCapableOpCount = 0;
	int32 ConstantCount = 0;
	int32 SymbolCount = 0;
	EFlightPressureClass PressureClass = EFlightPressureClass::Unknown;
	EFlightLocalityClass LocalityClass = EFlightLocalityClass::Unknown;
	EFlightWorkingSetClass WorkingSetClass = EFlightWorkingSetClass::Unknown;
};

struct FLIGHTPROJECT_API FFlightWarmupMetrics
{
	double ParseTimeMs = 0.0;
	double IrCompileTimeMs = 0.0;
	double VmAssembleTimeMs = 0.0;
	double TotalCompileTimeMs = 0.0;
	double FirstInvokeTimeMs = 0.0;
	double WarmInvokeTimeMs = 0.0;
	double FirstResourceInitTimeMs = 0.0;
	int32 RegistryMissCount = 0;
	int32 CacheFillCount = 0;
	EFlightColdStartClass ColdStartClass = EFlightColdStartClass::Unknown;
};

struct FLIGHTPROJECT_API FFlightCompressionSummary
{
	bool bBaselineAvailable = false;
	double InstructionCompressionRatio = 1.0;
	double CallCompressionRatio = 1.0;
	double LiveRangeCompressionRatio = 1.0;
	double MemoryTrafficCompressionRatio = 1.0;
	double StateTransitionCompressionRatio = 1.0;
};

struct FLIGHTPROJECT_API FFlightCompileBackendReport
{
	FString Backend;
	FString Decision;
	TArray<FString> Reasons;
};

struct FLIGHTPROJECT_API FFlightCompileArtifactReport
{
	FString SchemaVersion = TEXT("0.1");
	FDateTime GeneratedAtUtc;
	uint32 BehaviorID = 0;
	uint32 SourceHash = 0;
	FString TargetFingerprint;
	FString BoundTypeName;
	uint32 SchemaLayoutHash = 0;
	FString CompileOutcome;
	FString BackendPath;
	FString SelectedBackend;
	FString CommittedBackend;
	FString Diagnostics;
	FString GeneratedVerseCode;
	FString IrCompileErrors;
	EVexTier Tier = EVexTier::Full;
	bool bAsync = false;
	bool bHasIr = false;
	bool bUsesNativeFallback = false;
	bool bUsesVmEntryPoint = false;
	TArray<EFlightCompileArtifactKind> AvailableArtifacts;
	TArray<FFlightCompileBackendReport> BackendReports;
	TArray<FString> ReadSymbols;
	TArray<FString> WrittenSymbols;
	TArray<FString> ImportedSymbols;
	TArray<FString> ExportedSymbols;
	TArray<FString> ReferencedStorageKinds;
	int32 BoundaryOperatorCount = 0;
	bool bHasBoundaryOperators = false;
	bool bHasAwaitableBoundary = false;
	bool bHasMirrorRequest = false;
	FFlightCodeShapeMetrics CodeShapeMetrics;
	FFlightWarmupMetrics WarmupMetrics;
	FFlightCompressionSummary CompressionSummary;
};

FLIGHTPROJECT_API FString CompileArtifactKindToString(EFlightCompileArtifactKind Kind);
FLIGHTPROJECT_API FString PressureClassToString(EFlightPressureClass Class);
FLIGHTPROJECT_API FString LocalityClassToString(EFlightLocalityClass Class);
FLIGHTPROJECT_API FString WorkingSetClassToString(EFlightWorkingSetClass Class);
FLIGHTPROJECT_API FString ColdStartClassToString(EFlightColdStartClass Class);
FLIGHTPROJECT_API FString VexTierToString(EVexTier Tier);
FLIGHTPROJECT_API FString VexStorageKindToString(EVexStorageKind Kind);

FLIGHTPROJECT_API FFlightCodeShapeMetrics AnalyzeIrCodeShape(const FVexIrProgram& Program, bool bAsync = false);
FLIGHTPROJECT_API void FinalizeWarmupMetrics(FFlightWarmupMetrics& Metrics);
FLIGHTPROJECT_API FString BuildCompileArtifactReportJson(const FFlightCompileArtifactReport& Report);

} // namespace Flight::Vex
