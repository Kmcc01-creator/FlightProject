// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexBackendCapabilities.h"

#include "Vex/FlightVexOptics.h"
#include "Vex/FlightVexSchemaIr.h"
#include "Vex/FlightVexSimdExecutor.h"

namespace Flight::Vex
{

namespace
{

FVexValueTypeCapability MakeValueCapability(
	const EVexValueType Type,
	const bool bReadable,
	const bool bWritable,
	const bool bArithmetic,
	const bool bComparison,
	const bool bDirectLoadStore,
	const bool bAccessorFallback,
	const bool bPreferredFastLane)
{
	FVexValueTypeCapability Capability;
	Capability.Type = Type;
	Capability.bReadable = bReadable;
	Capability.bWritable = bWritable;
	Capability.bArithmetic = bArithmetic;
	Capability.bComparison = bComparison;
	Capability.bDirectLoadStore = bDirectLoadStore;
	Capability.bAccessorFallback = bAccessorFallback;
	Capability.bPreferredFastLane = bPreferredFastLane;
	return Capability;
}

void AddCommonCapabilities(FVexBackendCapabilityProfile& Profile, const bool bDirectLoadStore, const bool bAccessorFallback)
{
	Profile.ValueCapabilities.Add(EVexValueType::Float, MakeValueCapability(EVexValueType::Float, true, true, true, true, bDirectLoadStore, bAccessorFallback, true));
	Profile.ValueCapabilities.Add(EVexValueType::Int, MakeValueCapability(EVexValueType::Int, true, true, true, true, bDirectLoadStore, bAccessorFallback, false));
	Profile.ValueCapabilities.Add(EVexValueType::Bool, MakeValueCapability(EVexValueType::Bool, true, true, false, true, bDirectLoadStore, bAccessorFallback, false));
	Profile.ValueCapabilities.Add(EVexValueType::Float2, MakeValueCapability(EVexValueType::Float2, true, true, true, true, bDirectLoadStore, bAccessorFallback, true));
	Profile.ValueCapabilities.Add(EVexValueType::Float3, MakeValueCapability(EVexValueType::Float3, true, true, true, true, bDirectLoadStore, bAccessorFallback, true));
	Profile.ValueCapabilities.Add(EVexValueType::Float4, MakeValueCapability(EVexValueType::Float4, true, true, true, true, bDirectLoadStore, bAccessorFallback, true));
}

FVexBackendCapabilityProfile MakeNativeScalarProfile()
{
	FVexBackendCapabilityProfile Profile;
	Profile.Backend = EVexBackendKind::NativeScalar;
	Profile.PreferredLane = EVexAbiLane::Scalar;
	Profile.bSupportsAsync = false;
	Profile.bSupportsDirectSchemaExecution = true;
	Profile.bSupportsAccessorFallback = true;
	Profile.bSupportsSimdBatching = false;
	Profile.bSupportsBoundaryImports = false;
	Profile.bSupportsBoundaryExports = false;
	Profile.bSupportsBoundaryMirror = false;
	Profile.bSupportsBoundaryAwait = false;
	AddCommonCapabilities(Profile, true, true);
	Profile.AllowedResidencies = {
		EFlightVexSymbolResidency::Shared,
		EFlightVexSymbolResidency::CpuOnly
	};
	Profile.AllowedAffinities = {
		EFlightVexSymbolAffinity::Any,
		EFlightVexSymbolAffinity::GameThread,
		EFlightVexSymbolAffinity::WorkerThread
	};
	Profile.SupportedStorageKinds = {
		EVexStorageKind::None,
		EVexStorageKind::AosOffset,
		EVexStorageKind::Accessor,
		EVexStorageKind::ExternalProvider
	};
	return Profile;
}

FVexBackendCapabilityProfile MakeNativeSimdProfile()
{
	FVexBackendCapabilityProfile Profile;
	Profile.Backend = EVexBackendKind::NativeSimd;
	Profile.PreferredLane = EVexAbiLane::FloatVector;
	Profile.bSupportsAsync = false;
	Profile.bSupportsDirectSchemaExecution = true;
	Profile.bSupportsAccessorFallback = false;
	Profile.bSupportsSimdBatching = true;
	Profile.bSupportsBoundaryImports = false;
	Profile.bSupportsBoundaryExports = false;
	Profile.bSupportsBoundaryMirror = false;
	Profile.bSupportsBoundaryAwait = false;
	AddCommonCapabilities(Profile, true, false);
	Profile.AllowedResidencies = {
		EFlightVexSymbolResidency::Shared,
		EFlightVexSymbolResidency::CpuOnly
	};
	Profile.AllowedAffinities = {
		EFlightVexSymbolAffinity::Any,
		EFlightVexSymbolAffinity::WorkerThread
	};
	Profile.SupportedStorageKinds = {
		EVexStorageKind::AosOffset,
		EVexStorageKind::MassFragmentField
	};
	Profile.SupportedVectorStorageClasses = {
		EVexVectorStorageClass::AosPacked,
		EVexVectorStorageClass::MassFragmentColumn
	};
	return Profile;
}

FVexBackendCapabilityProfile MakeNativeAvx256x8Profile()
{
	FVexBackendCapabilityProfile Profile = MakeNativeSimdProfile();
	Profile.Backend = EVexBackendKind::NativeAvx256x8;
	Profile.PreferredLane = EVexAbiLane::FloatVector;
	return Profile;
}

FVexBackendCapabilityProfile MakeVerseVmProfile()
{
	FVexBackendCapabilityProfile Profile;
	Profile.Backend = EVexBackendKind::VerseVm;
	Profile.PreferredLane = EVexAbiLane::FloatVector;
	Profile.bSupportsAsync = true;
	Profile.bSupportsDirectSchemaExecution = true;
	Profile.bSupportsAccessorFallback = true;
	Profile.bSupportsSimdBatching = false;
	Profile.bSupportsBoundaryImports = false;
	Profile.bSupportsBoundaryExports = false;
	Profile.bSupportsBoundaryMirror = false;
	Profile.bSupportsBoundaryAwait = false;
	AddCommonCapabilities(Profile, true, true);
	Profile.AllowedResidencies = {
		EFlightVexSymbolResidency::Shared,
		EFlightVexSymbolResidency::CpuOnly
	};
	Profile.AllowedAffinities = {
		EFlightVexSymbolAffinity::Any,
		EFlightVexSymbolAffinity::GameThread,
		EFlightVexSymbolAffinity::WorkerThread
	};
	Profile.SupportedStorageKinds = {
		EVexStorageKind::AosOffset,
		EVexStorageKind::Accessor,
		EVexStorageKind::ExternalProvider
	};
	return Profile;
}

FVexBackendCapabilityProfile MakeGpuKernelProfile()
{
	FVexBackendCapabilityProfile Profile;
	Profile.Backend = EVexBackendKind::GpuKernel;
	Profile.PreferredLane = EVexAbiLane::FloatVector;
	Profile.bSupportsAsync = false;
	Profile.bSupportsDirectSchemaExecution = true;
	Profile.bSupportsAccessorFallback = false;
	Profile.bSupportsSimdBatching = true;
	Profile.bSupportsBoundaryImports = true;
	Profile.bSupportsBoundaryExports = false;
	Profile.bSupportsBoundaryMirror = false;
	Profile.bSupportsBoundaryAwait = false;
	AddCommonCapabilities(Profile, true, false);
	Profile.ValueCapabilities.FindChecked(EVexValueType::Bool).bWritable = false;
	Profile.AllowedResidencies = {
		EFlightVexSymbolResidency::Shared,
		EFlightVexSymbolResidency::GpuOnly
	};
	Profile.AllowedAffinities = {
		EFlightVexSymbolAffinity::Any,
		EFlightVexSymbolAffinity::WorkerThread
	};
	Profile.SupportedStorageKinds = {
		EVexStorageKind::GpuBufferElement,
		EVexStorageKind::SoaColumn
	};
	Profile.SupportedVectorStorageClasses = {
		EVexVectorStorageClass::SoaContiguous,
		EVexVectorStorageClass::GpuBufferColumn
	};
	return Profile;
}

bool SupportsTier(const FVexBackendCapabilityProfile& Profile, const EVexTier Tier)
{
	switch (Profile.Backend)
	{
	case EVexBackendKind::NativeAvx256x8:
	case EVexBackendKind::NativeSimd:
	case EVexBackendKind::GpuKernel:
		return Tier == EVexTier::Literal;
	case EVexBackendKind::NativeScalar:
	case EVexBackendKind::VerseVm:
	default:
		return true;
	}
}

int32 ScoreBackendSelection(
	const FVexBackendCompatibilityReport& Report,
	const bool bPreferSimd)
{
	if (Report.Decision == EVexBackendDecision::Rejected)
	{
		return MIN_int32;
	}

	int32 Score = 0;
	switch (Report.Backend)
	{
	case EVexBackendKind::NativeAvx256x8:
		Score = bPreferSimd ? 230 : 185;
		break;
	case EVexBackendKind::NativeSimd:
		Score = bPreferSimd ? 220 : 180;
		break;
	case EVexBackendKind::VerseVm:
		Score = 300;
		break;
	case EVexBackendKind::NativeScalar:
		Score = 240;
		break;
	case EVexBackendKind::GpuKernel:
		Score = 100;
		break;
	default:
		break;
	}

	if (Report.Decision == EVexBackendDecision::SupportedWithFallback)
	{
		Score -= 25;
	}
	if (Report.bUsesBoundarySemantics && Report.bAllBoundaryOperatorsSupported)
	{
		Score += Report.Backend == EVexBackendKind::GpuKernel ? 180 : 20;
	}
	if (Report.bSupportsProgramTier)
	{
		Score += 10;
	}
	if (Report.bAllReadsSupported && Report.bAllWritesSupported)
	{
		Score += 10;
	}

	return Score;
}

FString BuildMissingCapabilityReason(const FVexSymbolRecord& Symbol, const EVexSchemaAccessKind AccessKind)
{
	return FString::Printf(
		TEXT("Symbol '%s' is not %sable."),
		*Symbol.SymbolName,
		AccessKind == EVexSchemaAccessKind::Read ? TEXT("read") : TEXT("writ"));
}

FVexSymbolRecord MakeSyntheticSymbolRecord(const FVexSchemaBoundSymbol& BoundSymbol)
{
	FVexSymbolRecord Record;
	Record.SymbolName = BoundSymbol.SymbolName;
	Record.ValueType = BoundSymbol.LogicalSymbol.ValueType;
	Record.bReadable = BoundSymbol.LogicalSymbol.bReadable;
	Record.bWritable = BoundSymbol.LogicalSymbol.bWritable;
	Record.bRequired = BoundSymbol.LogicalSymbol.bRequired;
	Record.bSimdReadAllowed = BoundSymbol.LogicalSymbol.bSimdReadAllowed;
	Record.bSimdWriteAllowed = BoundSymbol.LogicalSymbol.bSimdWriteAllowed;
	Record.bGpuTier1Allowed = BoundSymbol.LogicalSymbol.bGpuTier1Allowed;
	Record.Residency = BoundSymbol.LogicalSymbol.Residency;
	Record.Affinity = BoundSymbol.LogicalSymbol.Affinity;
	Record.AlignmentRequirement = BoundSymbol.LogicalSymbol.AlignmentRequirement;
	Record.MathDeterminismProfile = BoundSymbol.LogicalSymbol.MathDeterminismProfile;
	Record.BackendBinding = BoundSymbol.LogicalSymbol.BackendBinding;
	Record.Storage = BoundSymbol.LogicalSymbol.Storage;
	Record.VectorPack = BoundSymbol.LogicalSymbol.VectorPack;
	return Record;
}

} // namespace

const FVexBackendCapabilityProfile& GetNativeScalarBackendProfile()
{
	static const FVexBackendCapabilityProfile Profile = MakeNativeScalarProfile();
	return Profile;
}

const FVexBackendCapabilityProfile& GetNativeSimdBackendProfile()
{
	static const FVexBackendCapabilityProfile Profile = MakeNativeSimdProfile();
	return Profile;
}

const FVexBackendCapabilityProfile& GetNativeAvx256x8BackendProfile()
{
	static const FVexBackendCapabilityProfile Profile = MakeNativeAvx256x8Profile();
	return Profile;
}

const FVexBackendCapabilityProfile& GetVerseVmBackendProfile()
{
	static const FVexBackendCapabilityProfile Profile = MakeVerseVmProfile();
	return Profile;
}

const FVexBackendCapabilityProfile& GetGpuKernelBackendProfile()
{
	static const FVexBackendCapabilityProfile Profile = MakeGpuKernelProfile();
	return Profile;
}

FVexSymbolBackendCompatibility EvaluateSymbolForBackend(
	const FVexSymbolRecord& Symbol,
	const EVexSchemaAccessKind AccessKind,
	const FVexBackendCapabilityProfile& Profile)
{
	FVexSymbolBackendCompatibility Result;
	Result.SymbolName = Symbol.SymbolName;
	Result.AccessKind = AccessKind;
	Result.Backend = Profile.Backend;
	Result.VectorStorageClass = Symbol.VectorPack.StorageClass;

	const FVexVectorPackContract EffectiveVectorPack = Symbol.VectorPack.StorageClass == EVexVectorStorageClass::None
		? BuildDefaultVectorPackContract(
			Symbol.Storage,
			Symbol.ValueType,
			Symbol.AlignmentRequirement,
			Symbol.bSimdReadAllowed,
			Symbol.bSimdWriteAllowed,
			Symbol.bGpuTier1Allowed)
		: Symbol.VectorPack;
	Result.VectorStorageClass = EffectiveVectorPack.StorageClass;

	const bool bReadable = AccessKind == EVexSchemaAccessKind::Read;
	if ((bReadable && !Symbol.bReadable) || (!bReadable && !Symbol.bWritable))
	{
		Result.Reason = BuildMissingCapabilityReason(Symbol, AccessKind);
		return Result;
	}

	if (!Profile.AllowedResidencies.IsEmpty() && !Profile.AllowedResidencies.Contains(Symbol.Residency))
	{
		Result.Reason = FString::Printf(TEXT("Residency '%d' is not supported."), static_cast<int32>(Symbol.Residency));
		return Result;
	}

	if (!Profile.AllowedAffinities.IsEmpty() && !Profile.AllowedAffinities.Contains(Symbol.Affinity))
	{
		Result.Reason = FString::Printf(TEXT("Affinity '%d' is not supported."), static_cast<int32>(Symbol.Affinity));
		return Result;
	}

	if (!Profile.SupportedStorageKinds.IsEmpty() && !Profile.SupportedStorageKinds.Contains(Symbol.Storage.Kind))
	{
		Result.Reason = FString::Printf(TEXT("Storage kind '%d' is not supported."), static_cast<int32>(Symbol.Storage.Kind));
		return Result;
	}

	if (!Profile.SupportedVectorStorageClasses.IsEmpty() && !Profile.SupportedVectorStorageClasses.Contains(EffectiveVectorPack.StorageClass))
	{
		Result.Reason = FString::Printf(
			TEXT("Vector storage class '%s' is not supported."),
			*VexVectorStorageClassToString(EffectiveVectorPack.StorageClass));
		return Result;
	}

	const FVexValueTypeCapability* Capability = Profile.ValueCapabilities.Find(Symbol.ValueType);
	if (!Capability)
	{
		Result.Reason = FString::Printf(TEXT("Value type '%d' is not supported."), static_cast<int32>(Symbol.ValueType));
		return Result;
	}

	if ((bReadable && !Capability->bReadable) || (!bReadable && !Capability->bWritable))
	{
		Result.Reason = FString::Printf(TEXT("Value type '%d' does not support %s access."), static_cast<int32>(Symbol.ValueType), bReadable ? TEXT("read") : TEXT("write"));
		return Result;
	}

	if ((Profile.Backend == EVexBackendKind::NativeSimd || Profile.Backend == EVexBackendKind::NativeAvx256x8)
		&& ((bReadable && !Symbol.bSimdReadAllowed) || (!bReadable && !Symbol.bSimdWriteAllowed)))
	{
		Result.Reason = TEXT("Symbol contract disallows SIMD access.");
		return Result;
	}

	if (Profile.Backend == EVexBackendKind::GpuKernel && !Symbol.bGpuTier1Allowed)
	{
		Result.Reason = TEXT("Symbol contract disallows GPU Tier 1 lowering.");
		return Result;
	}

	const bool bSupportsDirectSimdAccess = bReadable
		? EffectiveVectorPack.bSupportsDirectSimdRead
		: EffectiveVectorPack.bSupportsDirectSimdWrite;
	const bool bSupportsDirectGpuAccess = bReadable
		? EffectiveVectorPack.bSupportsGpuColumnRead
		: EffectiveVectorPack.bSupportsGpuColumnWrite;

	if ((Profile.Backend == EVexBackendKind::NativeSimd || Profile.Backend == EVexBackendKind::NativeAvx256x8)
		&& !bSupportsDirectSimdAccess)
	{
		Result.Reason = TEXT("Vector pack contract does not provide a direct SIMD load/store path.");
		return Result;
	}

	if (Profile.Backend == EVexBackendKind::GpuKernel && !bSupportsDirectGpuAccess)
	{
		Result.Reason = TEXT("Vector pack contract does not provide a direct GPU column path.");
		return Result;
	}

	Result.bPreferredFastLane =
		Capability->bPreferredFastLane
		&& (EffectiveVectorPack.StorageClass == EVexVectorStorageClass::AosPacked
			|| EffectiveVectorPack.StorageClass == EVexVectorStorageClass::MassFragmentColumn
			|| EffectiveVectorPack.StorageClass == EVexVectorStorageClass::GpuBufferColumn
			|| EffectiveVectorPack.StorageClass == EVexVectorStorageClass::SoaContiguous);

	if ((Profile.Backend == EVexBackendKind::NativeSimd || Profile.Backend == EVexBackendKind::NativeAvx256x8)
		&& bSupportsDirectSimdAccess)
	{
		Result.Decision = Result.bPreferredFastLane
			? EVexBackendDecision::Supported
			: EVexBackendDecision::SupportedWithFallback;
		Result.Reason = Result.bPreferredFastLane
			? TEXT("Direct SIMD load/store supported on the preferred vector storage class.")
			: TEXT("Direct SIMD load/store supported.");
		return Result;
	}

	if (Profile.Backend == EVexBackendKind::GpuKernel && bSupportsDirectGpuAccess)
	{
		Result.Decision = Result.bPreferredFastLane
			? EVexBackendDecision::Supported
			: EVexBackendDecision::SupportedWithFallback;
		Result.Reason = Result.bPreferredFastLane
			? TEXT("Direct GPU column access supported on the preferred vector storage class.")
			: TEXT("Direct GPU column access supported.");
		return Result;
	}

	if (Symbol.Storage.HasDirectOffset() && Capability->bDirectLoadStore)
	{
		Result.Decision = Result.bPreferredFastLane
			? EVexBackendDecision::Supported
			: EVexBackendDecision::SupportedWithFallback;
		Result.Reason = Result.bPreferredFastLane
			? TEXT("Direct load/store supported on preferred lane.")
			: TEXT("Direct load/store supported.");
		return Result;
	}

	if (Profile.bSupportsAccessorFallback
		&& Capability->bAccessorFallback
		&& (Symbol.Storage.Kind == EVexStorageKind::Accessor || Symbol.Storage.Kind == EVexStorageKind::ExternalProvider))
	{
		Result.Decision = EVexBackendDecision::SupportedWithFallback;
		Result.Reason = TEXT("Supported through typed accessor fallback.");
		return Result;
	}

	if (Symbol.Storage.Kind == EVexStorageKind::None && Profile.bSupportsAccessorFallback)
	{
		Result.Decision = EVexBackendDecision::SupportedWithFallback;
		Result.Reason = TEXT("Supported through legacy schema access.");
		return Result;
	}

	Result.Reason = TEXT("No compatible direct or fallback storage path is available.");
	return Result;
}

bool EvaluateBoundaryUseForBackend(
	const FVexSchemaBoundaryUse& BoundaryUse,
	const FVexBackendCapabilityProfile& Profile,
	FString& OutReason)
{
	const FString SymbolLabel = !BoundaryUse.DestinationSymbolName.IsEmpty()
		? BoundaryUse.DestinationSymbolName
		: (!BoundaryUse.SourceSymbolName.IsEmpty() ? BoundaryUse.SourceSymbolName : TEXT("<unnamed>"));

	if (BoundaryUse.Metadata.Direction == EVexBoundaryDirection::Import && !Profile.bSupportsBoundaryImports)
	{
		OutReason = FString::Printf(
			TEXT("Boundary import '%s' is not supported on backend '%s'."),
			*SymbolLabel,
			*VexBackendKindToString(Profile.Backend));
		return false;
	}

	if (BoundaryUse.Metadata.Direction == EVexBoundaryDirection::Export && !Profile.bSupportsBoundaryExports)
	{
		OutReason = FString::Printf(
			TEXT("Boundary export '%s' is not supported on backend '%s'."),
			*SymbolLabel,
			*VexBackendKindToString(Profile.Backend));
		return false;
	}

	if (BoundaryUse.Metadata.bMirrorRequested && !Profile.bSupportsBoundaryMirror)
	{
		OutReason = FString::Printf(
			TEXT("Boundary mirror requests for '%s' are not supported on backend '%s'."),
			*SymbolLabel,
			*VexBackendKindToString(Profile.Backend));
		return false;
	}

	if (BoundaryUse.Metadata.bAwaitable && !Profile.bSupportsBoundaryAwait)
	{
		OutReason = FString::Printf(
			TEXT("Awaitable boundary semantics for '%s' are not supported on backend '%s'."),
			*SymbolLabel,
			*VexBackendKindToString(Profile.Backend));
		return false;
	}

	OutReason = FString::Printf(
		TEXT("Boundary semantics for '%s' are supported on backend '%s'."),
		*SymbolLabel,
		*VexBackendKindToString(Profile.Backend));
	return true;
}

FVexBackendCompatibilityReport EvaluateBindingForBackend(
	const FVexSchemaBindingResult& Binding,
	const FVexTypeSchema& Schema,
	const EVexTier Tier,
	const bool bAsync,
	const FVexBackendCapabilityProfile& Profile)
{
	FVexBackendCompatibilityReport Report;
	Report.Backend = Profile.Backend;
	Report.bAllReadsSupported = true;
	Report.bAllWritesSupported = true;
	Report.bUsesBoundarySemantics = Binding.bHasBoundaryOperators;
	Report.bAllBoundaryOperatorsSupported = true;
	Report.bSupportsProgramTier = SupportsTier(Profile, Tier);
	Report.bSupportsAsync = !bAsync || Profile.bSupportsAsync;
	Report.bSupportsBoundaryImports = Profile.bSupportsBoundaryImports;
	Report.bSupportsBoundaryExports = Profile.bSupportsBoundaryExports;
	Report.bSupportsBoundaryMirror = Profile.bSupportsBoundaryMirror;
	Report.bSupportsBoundaryAwait = Profile.bSupportsBoundaryAwait;

	if (!Report.bSupportsProgramTier)
	{
		Report.Reasons.Add(TEXT("Program tier is not supported by this backend."));
	}
	if (!Report.bSupportsAsync)
	{
		Report.Reasons.Add(TEXT("Async execution is not supported by this backend."));
	}

	TSet<FString> EvaluatedUses;
	for (const FVexSchemaSymbolUse& Use : Binding.SymbolUses)
	{
		if (!Binding.BoundSymbols.IsValidIndex(Use.BoundSymbolIndex))
		{
			continue;
		}

		const FString& SymbolName = Binding.BoundSymbols[Use.BoundSymbolIndex].SymbolName;
		const FString UseKey = FString::Printf(TEXT("%s|%d"), *SymbolName, static_cast<int32>(Use.AccessKind));
		if (EvaluatedUses.Contains(UseKey))
		{
			continue;
		}
		EvaluatedUses.Add(UseKey);

		FVexSymbolRecord SyntheticSymbol;
		const FVexSymbolRecord* Symbol = Schema.FindSymbolRecord(SymbolName);
		if (!Symbol)
		{
			SyntheticSymbol = MakeSyntheticSymbolRecord(Binding.BoundSymbols[Use.BoundSymbolIndex]);
			Symbol = &SyntheticSymbol;
		}

		FVexSymbolBackendCompatibility Compatibility = EvaluateSymbolForBackend(*Symbol, Use.AccessKind, Profile);
		Report.SymbolResults.Add(Compatibility);
		if (Compatibility.Decision == EVexBackendDecision::Rejected)
		{
			Report.Reasons.Add(FString::Printf(TEXT("%s (%s)"), *SymbolName, *Compatibility.Reason));
			if (Use.AccessKind == EVexSchemaAccessKind::Read)
			{
				Report.bAllReadsSupported = false;
			}
			else
			{
				Report.bAllWritesSupported = false;
			}
		}
		else if (Compatibility.Decision == EVexBackendDecision::SupportedWithFallback)
		{
			Report.Reasons.Add(FString::Printf(TEXT("%s (%s)"), *SymbolName, *Compatibility.Reason));
		}
	}

	for (const FVexSchemaBoundaryUse& BoundaryUse : Binding.BoundaryUses)
	{
		FString BoundaryReason;
		if (!EvaluateBoundaryUseForBackend(BoundaryUse, Profile, BoundaryReason))
		{
			Report.bAllBoundaryOperatorsSupported = false;
			Report.Reasons.Add(BoundaryReason);
		}
		else
		{
			Report.Reasons.Add(BoundaryReason);
		}
	}

	if (!Report.bSupportsProgramTier || !Report.bSupportsAsync || !Report.bAllReadsSupported || !Report.bAllWritesSupported)
	{
		Report.Decision = EVexBackendDecision::Rejected;
	}
	else if (!Report.bAllBoundaryOperatorsSupported)
	{
		Report.Decision = EVexBackendDecision::Rejected;
	}
	else if (Report.SymbolResults.ContainsByPredicate([](const FVexSymbolBackendCompatibility& SymbolResult)
	{
		return SymbolResult.Decision == EVexBackendDecision::SupportedWithFallback;
	}))
	{
		Report.Decision = EVexBackendDecision::SupportedWithFallback;
	}
	else
	{
		Report.Decision = EVexBackendDecision::Supported;
	}

	if (Report.Reasons.Num() == 0)
	{
		Report.Reasons.Add(TEXT("All symbol uses are supported on this backend."));
	}

	return Report;
}

FVexBackendSelection SelectBackendForProgram(
	const FVexSchemaBindingResult& Binding,
	const FVexTypeSchema& Schema,
	const EVexTier Tier,
	const bool bAsync,
	const bool bVerseVmAvailable,
	const bool bPreferSimd)
{
	FVexBackendSelection Selection;

	const TArray<FVexBackendCapabilityProfile> Profiles = {
		GetNativeScalarBackendProfile(),
		GetNativeSimdBackendProfile(),
		GetNativeAvx256x8BackendProfile(),
		GetVerseVmBackendProfile(),
		GetGpuKernelBackendProfile()
	};

	int32 BestScore = MIN_int32;
	for (const FVexBackendCapabilityProfile& Profile : Profiles)
	{
		FVexBackendCompatibilityReport Report = EvaluateBindingForBackend(Binding, Schema, Tier, bAsync, Profile);
		if (Profile.Backend == EVexBackendKind::NativeAvx256x8
			&& !FVexSimdExecutor::HasExplicitAvx256x8KernelSupport())
		{
			Report.Decision = EVexBackendDecision::Rejected;
			Report.Reasons.Add(TEXT("Explicit AVX2/FMA kernels are not compiled into this build."));
		}
		if (Profile.Backend == EVexBackendKind::VerseVm && !bVerseVmAvailable)
		{
			Report.Decision = EVexBackendDecision::Rejected;
			Report.Reasons.Add(TEXT("Verse VM is unavailable in this build."));
		}

		if (Report.Decision != EVexBackendDecision::Rejected)
		{
			Selection.LegalBackends.Add(Profile.Backend);
		}

		const int32 Score = ScoreBackendSelection(Report, bPreferSimd);
		Selection.Reports.Add(Profile.Backend, Report);
		if (Score > BestScore)
		{
			BestScore = Score;
			Selection.ChosenBackend = Profile.Backend;
		}
	}

	if (const FVexBackendCompatibilityReport* ChosenReport = Selection.Reports.Find(Selection.ChosenBackend))
	{
		Selection.SelectionReason = ChosenReport->Reasons.Num() > 0
			? ChosenReport->Reasons[0]
			: TEXT("Selected highest scoring legal backend.");
	}

	return Selection;
}

FString VexBackendKindToString(const EVexBackendKind Backend)
{
	switch (Backend)
	{
	case EVexBackendKind::NativeScalar: return TEXT("NativeScalar");
	case EVexBackendKind::NativeSimd: return TEXT("NativeSimd");
	case EVexBackendKind::NativeAvx256x8: return TEXT("NativeAvx256x8");
	case EVexBackendKind::VerseVm: return TEXT("VerseVm");
	case EVexBackendKind::GpuKernel: return TEXT("GpuKernel");
	default: return TEXT("Unknown");
	}
}

FString VexAbiLaneToString(const EVexAbiLane Lane)
{
	switch (Lane)
	{
	case EVexAbiLane::Scalar: return TEXT("Scalar");
	case EVexAbiLane::FloatVector: return TEXT("FloatVector");
	case EVexAbiLane::GeneralTyped: return TEXT("GeneralTyped");
	default: return TEXT("Unknown");
	}
}

FString VexBackendDecisionToString(const EVexBackendDecision Decision)
{
	switch (Decision)
	{
	case EVexBackendDecision::Supported: return TEXT("Supported");
	case EVexBackendDecision::SupportedWithFallback: return TEXT("SupportedWithFallback");
	case EVexBackendDecision::Rejected: return TEXT("Rejected");
	default: return TEXT("Unknown");
	}
}

} // namespace Flight::Vex
