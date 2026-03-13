// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/UFlightVerseSubsystem.h"

#include "Verse/FlightGpuScriptBridge.h"
#include "Verse/FlightVerseMassHostBundle.h"
#include "Verse/FlightVerseSubsystemLog.h"
#include "Engine/World.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Vex/FlightVexSymbolRegistry.h"

namespace
{
	const void* GetDroidStateTypeKey()
	{
		return Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey();
	}
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

bool UFlightVerseSubsystem::ExecuteResolvedDirectStorageHost(
	const FResolvedStorageHost& Host,
	const FVerseBehavior& Behavior,
	const Flight::Vex::EVexBackendKind BackendKind,
	FMassFragmentHostBundle& Bundle)
{
	switch (Host.Kind)
	{
	case EStorageHostKind::MassFragments:
		return ExecuteOnMassSchemaHost(Behavior, BackendKind, Bundle);
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
	if (!Handle.IsValid())
	{
		UpdateBehaviorCommitState(
			Behavior.CompileArtifactReport.BehaviorID,
			TEXT("Unknown"),
			FString::Printf(TEXT("GPU submission failed before a runtime commit could be proven: %s"), *SubmissionDetail));
		return false;
	}

	FString AwaitReason;
	const TWeakObjectPtr<UFlightVerseSubsystem> WeakSubsystem(this);
	const bool bAwaitRegistered = GpuBridge->Await(
		Handle,
		[WeakSubsystem, BehaviorID = Behavior.CompileArtifactReport.BehaviorID, SubmissionHandle = Handle.Value](
			const EFlightGpuSubmissionStatus Status,
			const FString& Detail)
		{
			if (UFlightVerseSubsystem* ResolvedSubsystem = WeakSubsystem.Get())
			{
				ResolvedSubsystem->ResolveGpuExecutionCommit(
					BehaviorID,
					SubmissionHandle,
					Status == EFlightGpuSubmissionStatus::Completed,
					Detail);
			}
		},
		AwaitReason);
	if (!bAwaitRegistered)
	{
		UpdateBehaviorCommitState(
			Behavior.CompileArtifactReport.BehaviorID,
			TEXT("Unknown"),
			FString::Printf(TEXT("GPU submission was accepted, but no terminal await could be registered: %s"), *AwaitReason));
		return false;
	}

	if (FVerseBehavior* MutableBehavior = Behaviors.Find(Behavior.CompileArtifactReport.BehaviorID))
	{
		MutableBehavior->LastGpuSubmissionHandle = Handle.Value;
		MutableBehavior->LastGpuSubmissionDetail = SubmissionDetail;
		MutableBehavior->bGpuExecutionPending = true;
	}
	UpdateBehaviorCommitState(
		Behavior.CompileArtifactReport.BehaviorID,
		TEXT("Unknown"),
		FString::Printf(
			TEXT("GPU submission %llu accepted; waiting for a terminal bridge result before commit. %s"),
			static_cast<unsigned long long>(Handle.Value),
			*SubmissionDetail));
	UE_LOG(
		LogFlightVerseSubsystem,
		Verbose,
		TEXT("Behavior %u resolved to explicit GPU storage hosting. BridgeHandle=%llu Detail=%s"),
		Behavior.CompileArtifactReport.BehaviorID,
		static_cast<unsigned long long>(Handle.Value),
		*SubmissionDetail);
	return true;
}
