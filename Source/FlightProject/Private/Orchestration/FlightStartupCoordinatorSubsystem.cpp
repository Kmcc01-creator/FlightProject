// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Orchestration/FlightStartupCoordinatorSubsystem.h"

#include "FlightDataSubsystem.h"
#include "FlightGameMode.h"
#include "FlightProject.h"
#include "FlightScriptingLibrary.h"
#include "FlightStartupProfile.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Orchestration/FlightOrchestrationReport.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"

namespace Flight::Startup
{

namespace
{

bool ShouldUseLegacyGauntletPath(const UWorld* World)
{
	if (!World)
	{
		return false;
	}

	const FString MapName = World->GetMapName();
	if (MapName.Contains(TEXT("PersistentFlightTest")))
	{
		return true;
	}

	if (const AWorldSettings* WorldSettings = World->GetWorldSettings())
	{
		return WorldSettings->Tags.Contains(TEXT("GauntletTest"));
	}

	return false;
}

const FName StageDataReadiness(TEXT("DataReadiness"));
const FName StageWorldBootstrap(TEXT("WorldBootstrap"));
const FName StagePreSpawnOrchestrationRebuild(TEXT("PreSpawnOrchestrationRebuild"));
const FName StageInitialSwarmSpawn(TEXT("InitialSwarmSpawn"));
const FName StagePostSpawnOrchestrationRebuild(TEXT("PostSpawnOrchestrationRebuild"));
const FName StageGpuSwarmInitialize(TEXT("GpuSwarmInitialize"));
const FName StageUnsupportedProfile(TEXT("UnsupportedProfile"));
const FName StageWorld(TEXT("World"));

} // namespace

} // namespace Flight::Startup

void UFlightStartupCoordinatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LastRequest = Flight::Startup::FFlightStartupRequest();
	LastResult = Flight::Startup::FFlightStartupResult();
}

void UFlightStartupCoordinatorSubsystem::BuildStartupRequest(
	const AFlightGameMode& GameMode,
	Flight::Startup::FFlightStartupRequest& OutRequest) const
{
	OutRequest = Flight::Startup::FFlightStartupRequest();

	const UWorld* World = GameMode.GetWorld();
	OutRequest.ActiveProfile = ResolveStartupProfile(GameMode, World);
	OutRequest.ActiveProfileName = StartupProfileToString(OutRequest.ActiveProfile);

	const TSoftObjectPtr<UFlightStartupProfile>& StartupProfileAsset = GameMode.GetStartupProfileAsset();
	OutRequest.ProfileAssetPath = StartupProfileAsset.IsNull()
		? FString()
		: StartupProfileAsset.ToSoftObjectPath().ToString();
	OutRequest.bProfileAssetConfigured = !StartupProfileAsset.IsNull();
	OutRequest.GauntletGpuSwarmEntityCount = ResolveGauntletGpuSwarmEntityCount(GameMode);

	if (!StartupProfileAsset.IsNull())
	{
		if (const UFlightStartupProfile* StartupProfileObject = StartupProfileAsset.LoadSynchronous())
		{
			OutRequest.bProfileAssetLoaded = true;
			OutRequest.ActiveProfile = StartupProfileObject->StartupProfile;
			OutRequest.ActiveProfileName = StartupProfileToString(StartupProfileObject->StartupProfile);
			OutRequest.GauntletGpuSwarmEntityCount = FMath::Max(1, StartupProfileObject->GauntletGpuSwarmEntityCount);
			OutRequest.ResolutionSource = TEXT("StartupProfileAsset");
			OutRequest.Detail = StartupProfileObject->Description;
			return;
		}

		OutRequest.Detail = FString::Printf(
			TEXT("Failed to load StartupProfileAsset '%s'; falling back to non-asset startup resolution."),
			*OutRequest.ProfileAssetPath);
	}

	if (GameMode.GetConfiguredStartupProfile() != EFlightStartupProfile::LegacyAuto)
	{
		OutRequest.ResolutionSource = TEXT("Config");
		if (OutRequest.Detail.IsEmpty())
		{
			OutRequest.Detail = TEXT("Resolved from GameMode config.");
		}
		return;
	}

	OutRequest.ResolutionSource = TEXT("LegacyAutoMapInference");
	OutRequest.bResolvedFromLegacyAuto = true;
	if (OutRequest.Detail.IsEmpty())
	{
		OutRequest.Detail = TEXT("Resolved from legacy map/tag inference because StartupProfile=LegacyAuto.");
	}
}

void UFlightStartupCoordinatorSubsystem::BuildStartupReport(
	Flight::Orchestration::FFlightStartupProfileReport& OutReport) const
{
	OutReport = Flight::Orchestration::FFlightStartupProfileReport();

	const AFlightGameMode* FlightGameMode = FindFlightGameMode();
	if (!FlightGameMode)
	{
		OutReport.ResolutionSource = TEXT("Unavailable");
		OutReport.Detail = TEXT("FlightGameMode is not present in the current world.");
		return;
	}

	OutReport.bGameModePresent = true;
	OutReport.GameModeClass = FlightGameMode->GetClass() ? FlightGameMode->GetClass()->GetPathName() : FString();

	Flight::Startup::FFlightStartupRequest StartupRequest;
	BuildStartupRequest(*FlightGameMode, StartupRequest);
	OutReport.ActiveProfile = StartupRequest.ActiveProfileName;
	OutReport.ResolutionSource = StartupRequest.ResolutionSource;
	OutReport.ProfileAssetPath = StartupRequest.ProfileAssetPath;
	OutReport.bProfileAssetConfigured = StartupRequest.bProfileAssetConfigured;
	OutReport.bProfileAssetLoaded = StartupRequest.bProfileAssetLoaded;
	OutReport.bResolvedFromLegacyAuto = StartupRequest.bResolvedFromLegacyAuto;
	OutReport.GauntletGpuSwarmEntityCount = StartupRequest.GauntletGpuSwarmEntityCount;
	OutReport.Detail = StartupRequest.Detail;

	if (!HasCompletedStartup())
	{
		return;
	}

	OutReport.bStartupRunCompleted = LastResult.bCompleted;
	OutReport.bStartupRunSucceeded = LastResult.bSucceeded;
	OutReport.StartupStartedAtUtc = LastResult.StartedAtUtc;
	OutReport.StartupCompletedAtUtc = LastResult.CompletedAtUtc;
	OutReport.FailureStage = LastResult.FailureStage.ToString();
	OutReport.SpawnedSwarmEntities = LastResult.SpawnedSwarmEntities;
	OutReport.Summary = LastResult.Summary;
	OutReport.Stages = LastResult.Stages;
}

bool UFlightStartupCoordinatorSubsystem::RunStartup(const Flight::Startup::FFlightStartupRequest& Request)
{
	ResetLastStartup(Request);

	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteFailure(Flight::Startup::StageWorld, TEXT("Startup coordinator has no world context."));
		return false;
	}

	switch (Request.ActiveProfile)
	{
	case EFlightStartupProfile::DefaultSandbox:
		return RunDefaultSandboxStartup(Request);

	case EFlightStartupProfile::GauntletGpuSwarm:
		return RunGauntletGpuSwarmStartup(Request);

	case EFlightStartupProfile::LegacyAuto:
	default:
		RecordStage(
			Flight::Startup::StageUnsupportedProfile,
			false,
			false,
			FString::Printf(TEXT("Unsupported startup profile '%s' reached coordinator."), *Request.ActiveProfileName));
		CompleteFailure(
			Flight::Startup::StageUnsupportedProfile,
			FString::Printf(TEXT("Startup coordinator cannot execute unresolved profile '%s'."), *Request.ActiveProfileName));
		return false;
	}
}

const TCHAR* UFlightStartupCoordinatorSubsystem::StartupProfileToString(const EFlightStartupProfile Profile)
{
	switch (Profile)
	{
	case EFlightStartupProfile::DefaultSandbox:
		return TEXT("DefaultSandbox");
	case EFlightStartupProfile::GauntletGpuSwarm:
		return TEXT("GauntletGpuSwarm");
	case EFlightStartupProfile::LegacyAuto:
		return TEXT("LegacyAuto");
	default:
		return TEXT("Unknown");
	}
}

const AFlightGameMode* UFlightStartupCoordinatorSubsystem::FindFlightGameMode() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (const AFlightGameMode* AuthGameMode = Cast<AFlightGameMode>(World->GetAuthGameMode()))
	{
		return AuthGameMode;
	}

	for (TActorIterator<AFlightGameMode> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (const AFlightGameMode* GameMode = *It)
		{
			return GameMode;
		}
	}

	return nullptr;
}

EFlightStartupProfile UFlightStartupCoordinatorSubsystem::ResolveStartupProfile(
	const AFlightGameMode& GameMode,
	const UWorld* World) const
{
	const TSoftObjectPtr<UFlightStartupProfile>& StartupProfileAsset = GameMode.GetStartupProfileAsset();
	if (!StartupProfileAsset.IsNull())
	{
		if (const UFlightStartupProfile* StartupProfileObject = StartupProfileAsset.LoadSynchronous())
		{
			return StartupProfileObject->StartupProfile;
		}

		UE_LOG(
			LogFlightProject,
			Warning,
			TEXT("FlightStartupCoordinator: Failed to load StartupProfileAsset '%s'; falling back to config."),
			*StartupProfileAsset.ToSoftObjectPath().ToString());
	}

	if (GameMode.GetConfiguredStartupProfile() != EFlightStartupProfile::LegacyAuto)
	{
		return GameMode.GetConfiguredStartupProfile();
	}

	const EFlightStartupProfile ResolvedProfile = Flight::Startup::ShouldUseLegacyGauntletPath(World)
		? EFlightStartupProfile::GauntletGpuSwarm
		: EFlightStartupProfile::DefaultSandbox;

	UE_LOG(
		LogFlightProject,
		Warning,
		TEXT("FlightStartupCoordinator: StartupProfile is LegacyAuto; resolved '%s' from legacy map/tag inference. Prefer setting StartupProfile explicitly."),
		StartupProfileToString(ResolvedProfile));

	return ResolvedProfile;
}

int32 UFlightStartupCoordinatorSubsystem::ResolveGauntletGpuSwarmEntityCount(const AFlightGameMode& GameMode) const
{
	const TSoftObjectPtr<UFlightStartupProfile>& StartupProfileAsset = GameMode.GetStartupProfileAsset();
	if (!StartupProfileAsset.IsNull())
	{
		if (const UFlightStartupProfile* StartupProfileObject = StartupProfileAsset.LoadSynchronous())
		{
			return FMath::Max(1, StartupProfileObject->GauntletGpuSwarmEntityCount);
		}
	}

	return FMath::Max(1, GameMode.GetConfiguredGauntletGpuSwarmEntityCount());
}

bool UFlightStartupCoordinatorSubsystem::RunDefaultSandboxStartup(const Flight::Startup::FFlightStartupRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteFailure(Flight::Startup::StageWorld, TEXT("DefaultSandbox startup has no world context."));
		return false;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		RecordStage(
			Flight::Startup::StageDataReadiness,
			false,
			false,
			TEXT("No GameInstance available for data contract resolution."));
		CompleteFailure(
			Flight::Startup::StageDataReadiness,
			TEXT("DefaultSandbox startup requires a GameInstance-backed world."));
		return false;
	}

	UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
	if (!DataSubsystem)
	{
		RecordStage(
			Flight::Startup::StageDataReadiness,
			false,
			false,
			TEXT("FlightDataSubsystem is not available."));
		CompleteFailure(
			Flight::Startup::StageDataReadiness,
			TEXT("DefaultSandbox startup requires FlightDataSubsystem."));
		return false;
	}

	if (!DataSubsystem->IsFullyLoaded())
	{
		DataSubsystem->ReloadAllConfigs();
	}

	if (!DataSubsystem->IsFullyLoaded())
	{
		RecordStage(
			Flight::Startup::StageDataReadiness,
			false,
			false,
			TEXT("Configured data contracts failed to load fully after reload."));
		CompleteFailure(
			Flight::Startup::StageDataReadiness,
			TEXT("DefaultSandbox startup failed data-readiness preflight."));
		return false;
	}

	RecordStage(
		Flight::Startup::StageDataReadiness,
		true,
		true,
		TEXT("Configured data contracts are loaded and ready."));

	UFlightWorldBootstrapSubsystem* BootstrapSubsystem = World->GetSubsystem<UFlightWorldBootstrapSubsystem>();
	if (!BootstrapSubsystem)
	{
		RecordStage(
			Flight::Startup::StageWorldBootstrap,
			false,
			false,
			TEXT("FlightWorldBootstrapSubsystem is not available."));
		CompleteFailure(
			Flight::Startup::StageWorldBootstrap,
			TEXT("DefaultSandbox startup requires FlightWorldBootstrapSubsystem."));
		return false;
	}

	BootstrapSubsystem->RunBootstrap();
	RecordStage(
		Flight::Startup::StageWorldBootstrap,
		true,
		true,
		TEXT("World bootstrap completed."));

	UFlightOrchestrationSubsystem* OrchestrationSubsystem = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	if (!OrchestrationSubsystem)
	{
		RecordStage(
			Flight::Startup::StagePreSpawnOrchestrationRebuild,
			false,
			false,
			TEXT("UFlightOrchestrationSubsystem is not available."));
		CompleteFailure(
			Flight::Startup::StagePreSpawnOrchestrationRebuild,
			TEXT("DefaultSandbox startup requires UFlightOrchestrationSubsystem."));
		return false;
	}

	OrchestrationSubsystem->Rebuild();
	RecordStage(
		Flight::Startup::StagePreSpawnOrchestrationRebuild,
		true,
		true,
		FString::Printf(
			TEXT("Pre-spawn orchestration rebuild completed (generation=%u, steps=%d)."),
			OrchestrationSubsystem->GetExecutionPlan().Generation,
			OrchestrationSubsystem->GetExecutionPlan().Steps.Num()));

	UFlightScriptingLibrary::SpawnInitialSwarm(World);
	const int32 SwarmEntityCount = UFlightScriptingLibrary::GetSwarmEntityCount(World);
	LastResult.SpawnedSwarmEntities = SwarmEntityCount;
	if (SwarmEntityCount <= 0)
	{
		RecordStage(
			Flight::Startup::StageInitialSwarmSpawn,
			false,
			false,
			TEXT("SpawnInitialSwarm completed without producing swarm entities."));
		CompleteFailure(
			Flight::Startup::StageInitialSwarmSpawn,
			TEXT("DefaultSandbox startup failed to commit an initial swarm spawn."));
		return false;
	}

	RecordStage(
		Flight::Startup::StageInitialSwarmSpawn,
		true,
		true,
		FString::Printf(TEXT("Initial swarm spawn committed %d swarm entities."), SwarmEntityCount));

	OrchestrationSubsystem->Rebuild();
	RecordStage(
		Flight::Startup::StagePostSpawnOrchestrationRebuild,
		true,
		true,
		FString::Printf(
			TEXT("Post-spawn orchestration rebuild completed (generation=%u, steps=%d)."),
			OrchestrationSubsystem->GetExecutionPlan().Generation,
			OrchestrationSubsystem->GetExecutionPlan().Steps.Num()));

	CompleteSuccess(FString::Printf(
		TEXT("DefaultSandbox startup completed with %d spawned swarm entities."),
		SwarmEntityCount));
	return true;
}

bool UFlightStartupCoordinatorSubsystem::RunGauntletGpuSwarmStartup(const Flight::Startup::FFlightStartupRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteFailure(Flight::Startup::StageWorld, TEXT("GauntletGpuSwarm startup has no world context."));
		return false;
	}

	const int32 EntityCount = FMath::Max(1, Request.GauntletGpuSwarmEntityCount);
	UFlightScriptingLibrary::InitializeGpuSwarm(World, EntityCount);
	RecordStage(
		Flight::Startup::StageGpuSwarmInitialize,
		true,
		true,
		FString::Printf(TEXT("GPU swarm initialization requested with %d entities."), EntityCount));
	CompleteSuccess(FString::Printf(
		TEXT("GauntletGpuSwarm startup initialized %d GPU swarm entities."),
		EntityCount));
	return true;
}

void UFlightStartupCoordinatorSubsystem::ResetLastStartup(const Flight::Startup::FFlightStartupRequest& Request)
{
	LastRequest = Request;
	LastResult = Flight::Startup::FFlightStartupResult();
	LastResult.StartedAtUtc = FDateTime::UtcNow();
}

void UFlightStartupCoordinatorSubsystem::CompleteSuccess(const FString& Summary)
{
	LastResult.bCompleted = true;
	LastResult.bSucceeded = true;
	LastResult.CompletedAtUtc = FDateTime::UtcNow();
	LastResult.Summary = Summary;

	if (UFlightOrchestrationSubsystem* OrchestrationSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UFlightOrchestrationSubsystem>() : nullptr)
	{
		OrchestrationSubsystem->Rebuild();
	}

	UE_LOG(LogFlightProject, Log, TEXT("FlightStartupCoordinator: %s"), *Summary);
}

void UFlightStartupCoordinatorSubsystem::CompleteFailure(const FName FailureStage, const FString& Summary)
{
	LastResult.bCompleted = true;
	LastResult.bSucceeded = false;
	LastResult.CompletedAtUtc = FDateTime::UtcNow();
	LastResult.FailureStage = FailureStage;
	LastResult.Summary = Summary;

	if (UFlightOrchestrationSubsystem* OrchestrationSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UFlightOrchestrationSubsystem>() : nullptr)
	{
		OrchestrationSubsystem->Rebuild();
	}

	UE_LOG(
		LogFlightProject,
		Warning,
		TEXT("FlightStartupCoordinator: failureStage=%s summary=%s"),
		*FailureStage.ToString(),
		*Summary);
}

void UFlightStartupCoordinatorSubsystem::RecordStage(
	const FName StageName,
	const bool bSucceeded,
	const bool bCommitted,
	FString Detail)
{
	Flight::Startup::FFlightStartupStageReport& Stage = LastResult.Stages.AddDefaulted_GetRef();
	Stage.StageName = StageName;
	Stage.bSucceeded = bSucceeded;
	Stage.bCommitted = bCommitted;
	Stage.Detail = MoveTemp(Detail);
}
