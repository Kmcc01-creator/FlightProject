// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/Frontend/VexAst.h"
#include "Vex/FlightVexSchema.h"

namespace Flight::Vex
{

struct FVexSchemaBindingResult;
enum class EVexSchemaAccessKind : uint8;
enum class EVexTier : uint8;

enum class EVexBackendKind : uint8
{
	NativeScalar,
	NativeSimd,
	VerseVm,
	GpuKernel
};

enum class EVexAbiLane : uint8
{
	Scalar,
	FloatVector,
	GeneralTyped
};

enum class EVexBackendDecision : uint8
{
	Supported,
	SupportedWithFallback,
	Rejected
};

struct FLIGHTPROJECT_API FVexValueTypeCapability
{
	EVexValueType Type = EVexValueType::Unknown;
	bool bReadable = false;
	bool bWritable = false;
	bool bArithmetic = false;
	bool bComparison = false;
	bool bDirectLoadStore = false;
	bool bAccessorFallback = false;
	bool bPreferredFastLane = false;
};

struct FLIGHTPROJECT_API FVexBackendCapabilityProfile
{
	EVexBackendKind Backend = EVexBackendKind::NativeScalar;
	EVexAbiLane PreferredLane = EVexAbiLane::Scalar;
	TMap<EVexValueType, FVexValueTypeCapability> ValueCapabilities;
	bool bSupportsAsync = false;
	bool bSupportsDirectSchemaExecution = false;
	bool bSupportsAccessorFallback = true;
	bool bSupportsSimdBatching = false;
	TSet<EFlightVexSymbolResidency> AllowedResidencies;
	TSet<EFlightVexSymbolAffinity> AllowedAffinities;
	TSet<EVexStorageKind> SupportedStorageKinds;
};

struct FLIGHTPROJECT_API FVexSymbolBackendCompatibility
{
	FString SymbolName;
	EVexSchemaAccessKind AccessKind;
	EVexBackendKind Backend = EVexBackendKind::NativeScalar;
	EVexBackendDecision Decision = EVexBackendDecision::Rejected;
	bool bPreferredFastLane = false;
	FString Reason;
};

struct FLIGHTPROJECT_API FVexBackendCompatibilityReport
{
	EVexBackendKind Backend = EVexBackendKind::NativeScalar;
	EVexBackendDecision Decision = EVexBackendDecision::Rejected;
	bool bAllReadsSupported = false;
	bool bAllWritesSupported = false;
	bool bSupportsProgramTier = false;
	bool bSupportsAsync = false;
	TArray<FVexSymbolBackendCompatibility> SymbolResults;
	TArray<FString> Reasons;
};

struct FLIGHTPROJECT_API FVexBackendSelection
{
	EVexBackendKind ChosenBackend = EVexBackendKind::NativeScalar;
	TArray<EVexBackendKind> LegalBackends;
	TMap<EVexBackendKind, FVexBackendCompatibilityReport> Reports;
	FString SelectionReason;
};

FLIGHTPROJECT_API const FVexBackendCapabilityProfile& GetNativeScalarBackendProfile();
FLIGHTPROJECT_API const FVexBackendCapabilityProfile& GetNativeSimdBackendProfile();
FLIGHTPROJECT_API const FVexBackendCapabilityProfile& GetVerseVmBackendProfile();
FLIGHTPROJECT_API const FVexBackendCapabilityProfile& GetGpuKernelBackendProfile();

FLIGHTPROJECT_API FVexSymbolBackendCompatibility EvaluateSymbolForBackend(
	const FVexSymbolRecord& Symbol,
	EVexSchemaAccessKind AccessKind,
	const FVexBackendCapabilityProfile& Profile);

FLIGHTPROJECT_API FVexBackendCompatibilityReport EvaluateBindingForBackend(
	const FVexSchemaBindingResult& Binding,
	const FVexTypeSchema& Schema,
	EVexTier Tier,
	bool bAsync,
	const FVexBackendCapabilityProfile& Profile);

FLIGHTPROJECT_API FVexBackendSelection SelectBackendForProgram(
	const FVexSchemaBindingResult& Binding,
	const FVexTypeSchema& Schema,
	EVexTier Tier,
	bool bAsync,
	bool bVerseVmAvailable,
	bool bPreferSimd);

FLIGHTPROJECT_API FString VexBackendKindToString(EVexBackendKind Backend);
FLIGHTPROJECT_API FString VexAbiLaneToString(EVexAbiLane Lane);
FLIGHTPROJECT_API FString VexBackendDecisionToString(EVexBackendDecision Decision);

} // namespace Flight::Vex
