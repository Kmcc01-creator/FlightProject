#include "FlightGameMode.h"

#include "FlightGameState.h"
#include "FlightHUD.h"
#include "FlightPlayerController.h"
#include "FlightVehiclePawn.h"
#include "MassSimulationSubsystem.h"
#include "FlightAIPawn.h"
#include "FlightSpatialLayoutDirector.h"
#include "FlightDataSubsystem.h"
#include "FlightDataTypes.h"
#include "FlightWaypointPath.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightGameMode, Log, All);

AFlightGameMode::AFlightGameMode()
{
    DefaultPawnClass = AFlightVehiclePawn::StaticClass();
    PlayerControllerClass = AFlightPlayerController::StaticClass();
    HUDClass = AFlightHUD::StaticClass();
    GameStateClass = AFlightGameState::StaticClass();
}

void AFlightGameMode::StartPlay()
{
    Super::StartPlay();
    const UWorld* World = GetWorld();
    UE_LOG(LogFlightGameMode, Log, TEXT("StartPlay for map '%s'"), World ? *World->GetMapName() : TEXT("<unknown>"));

    InitializeMassRuntime();
    SetupNightEnvironment();
    BuildSpatialTestRange();
    SpawnAutonomousFlights();
    UE_LOG(LogFlightGameMode, Log, TEXT("FlightGameMode StartPlay bootstrap complete"));
}

void AFlightGameMode::InitializeMassRuntime()
{
    if (!GetWorld())
    {
        UE_LOG(LogFlightGameMode, Warning, TEXT("InitializeMassRuntime skipped: world not available"));
        return;
    }

    if (UMassSimulationSubsystem* SimulationSubsystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld()))
    {
        if (SimulationSubsystem->IsSimulationPaused())
        {
            SimulationSubsystem->ResumeSimulation();
            UE_LOG(LogFlightGameMode, Log, TEXT("Resumed MassSimulationSubsystem simulation"));
        }
        else
        {
            UE_LOG(LogFlightGameMode, Verbose, TEXT("Mass simulation already active; no action taken"));
        }
    }
    else
    {
        UE_LOG(LogFlightGameMode, Warning, TEXT("MassSimulationSubsystem not found; Mass agents will remain inactive"));
    }
}

void AFlightGameMode::SetupNightEnvironment()
{
    if (!GetWorld())
    {
        UE_LOG(LogFlightGameMode, Warning, TEXT("SetupNightEnvironment skipped: world not available"));
        return;
    }

    const UFlightDataSubsystem* DataSubsystem = nullptr;
    if (UGameInstance* GameInstance = GetWorld()->GetGameInstance())
    {
        DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    }

    const FFlightLightingConfigRow* LightingConfig = DataSubsystem ? DataSubsystem->GetLightingConfig() : nullptr;

    const float DirectionalIntensity = LightingConfig ? LightingConfig->DirectionalIntensity : 1500.f;
    const FLinearColor DirectionalColor = LightingConfig ? LightingConfig->GetDirectionalColor() : FLinearColor(0.05f, 0.08f, 0.18f, 1.f);
    const FRotator DirectionalRotation = LightingConfig ? LightingConfig->GetDirectionalRotation() : FRotator(-10.f, 130.f, 0.f);

    const float SkyIntensity = LightingConfig ? LightingConfig->SkyIntensity : 0.25f;
    const FLinearColor SkyColor = LightingConfig ? LightingConfig->GetSkyColor() : FLinearColor(0.02f, 0.06f, 0.12f, 1.f);
    const FLinearColor LowerHemisphereColor = LightingConfig ? LightingConfig->GetLowerHemisphereColor() : FLinearColor::Black;

    UE_LOG(LogFlightGameMode, Log, TEXT("Applying lighting config: DirectionalIntensity=%.1f, SkyIntensity=%.2f (Source=%s)"),
        DirectionalIntensity,
        SkyIntensity,
        LightingConfig ? TEXT("DataTable") : TEXT("Defaults"));

    ADirectionalLight* DirectionalLight = nullptr;
    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        DirectionalLight = *It;
        break;
    }

    if (!DirectionalLight)
    {
        DirectionalLight = GetWorld()->SpawnActor<ADirectionalLight>();
    }
    
    if (DirectionalLight)
    {
        if (UDirectionalLightComponent* DirectionalComponent = DirectionalLight->FindComponentByClass<UDirectionalLightComponent>())
        {
            DirectionalComponent->SetMobility(EComponentMobility::Movable);
            DirectionalComponent->SetIntensity(DirectionalIntensity);
            DirectionalComponent->SetLightColor(DirectionalColor);
            DirectionalComponent->SetAtmosphereSunLightIndex(0);
            DirectionalComponent->SetEnableLightShaftOcclusion(true);
            DirectionalComponent->MarkRenderStateDirty();
        }

        DirectionalLight->SetActorRotation(DirectionalRotation);
    }

    ASkyLight* SkyLight = nullptr;
    for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
    {
        SkyLight = *It;
        break;
    }

    if (!SkyLight)
    {
        SkyLight = GetWorld()->SpawnActor<ASkyLight>();
    }

    if (SkyLight)
    {
        if (USkyLightComponent* SkyComponent = SkyLight->FindComponentByClass<USkyLightComponent>())
        {
            SkyComponent->SetMobility(EComponentMobility::Movable);
            SkyComponent->SetIntensity(SkyIntensity);
            SkyComponent->SetLightColor(SkyColor);
            SkyComponent->SetLowerHemisphereColor(LowerHemisphereColor);
            SkyComponent->RecaptureSky();
        }
    }
}

void AFlightGameMode::BuildSpatialTestRange()
{
    if (!GetWorld())
    {
        UE_LOG(LogFlightGameMode, Warning, TEXT("BuildSpatialTestRange skipped: world not available"));
        return;
    }

    for (TActorIterator<AFlightSpatialLayoutDirector> It(GetWorld()); It; ++It)
    {
        UE_LOG(LogFlightGameMode, Log, TEXT("Spatial layout director already present: %s"), *GetNameSafe(*It));
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AFlightSpatialLayoutDirector* NewDirector = GetWorld()->SpawnActor<AFlightSpatialLayoutDirector>(AFlightSpatialLayoutDirector::StaticClass(), FTransform::Identity, SpawnParams))
    {
        UE_LOG(LogFlightGameMode, Log, TEXT("Spawned spatial layout director: %s"), *GetNameSafe(NewDirector));
    }
    else
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("Failed to spawn spatial layout director"));
    }
}

void AFlightGameMode::SpawnAutonomousFlights()
{
    if (!GetWorld())
    {
        UE_LOG(LogFlightGameMode, Warning, TEXT("SpawnAutonomousFlights skipped: world not available"));
        return;
    }

    const UFlightDataSubsystem* DataSubsystem = nullptr;
    if (UGameInstance* GameInstance = GetWorld()->GetGameInstance())
    {
        DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    }

    const FFlightAutopilotConfigRow* AutopilotConfig = DataSubsystem ? DataSubsystem->GetAutopilotConfig() : nullptr;

    AFlightWaypointPath* WaypointPath = FindOrCreateWaypointPath(AutopilotConfig);
    if (!WaypointPath)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("Unable to spawn drones: waypoint path missing"));
        return;
    }

    const float PathLength = WaypointPath->GetPathLength();
    if (PathLength <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("Waypoint path %s has insufficient length (%.2f)"),
            *GetNameSafe(WaypointPath),
            PathLength);
        return;
    }

    const int32 DroneCount = FMath::Max(AutopilotConfig ? AutopilotConfig->DroneCount : 6, 1);
    const bool bLoopPath = AutopilotConfig ? AutopilotConfig->ShouldLoopPath() : true;
    UE_LOG(LogFlightGameMode, Log, TEXT("Spawning %d autonomous drones on path %s (Length=%.2f, Loop=%s)"),
        DroneCount,
        *GetNameSafe(WaypointPath),
        PathLength,
        bLoopPath ? TEXT("true") : TEXT("false"));

    for (int32 DroneIndex = 0; DroneIndex < DroneCount; ++DroneIndex)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        SpawnParams.Owner = this;

        if (AFlightAIPawn* Drone = GetWorld()->SpawnActor<AFlightAIPawn>(AFlightAIPawn::StaticClass(), FTransform::Identity, SpawnParams))
        {
            const float Distance = (PathLength / DroneCount) * DroneIndex;
            if (AutopilotConfig)
            {
                Drone->ApplyAutopilotConfig(*AutopilotConfig);
            }
            Drone->SetFlightPath(WaypointPath, Distance, bLoopPath);
            UE_LOG(LogFlightGameMode, Log, TEXT("Spawned drone %s [%d/%d] at distance %.2f"),
                *GetNameSafe(Drone),
                DroneIndex + 1,
                DroneCount,
                Distance);
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("Failed to spawn drone %d of %d"), DroneIndex + 1, DroneCount);
        }
    }
}

AFlightWaypointPath* AFlightGameMode::FindOrCreateWaypointPath(const FFlightAutopilotConfigRow* AutopilotConfig)
{
    if (!GetWorld())
    {
        UE_LOG(LogFlightGameMode, Warning, TEXT("FindOrCreateWaypointPath failed: world not available"));
        return nullptr;
    }

    for (TActorIterator<AFlightWaypointPath> It(GetWorld()); It; ++It)
    {
        AFlightWaypointPath* ExistingPath = *It;
        if (AutopilotConfig)
        {
            ExistingPath->ConfigureFromAutopilotConfig(*AutopilotConfig);
            UE_LOG(LogFlightGameMode, Log, TEXT("Reusing waypoint path %s with autopilot config (Radius=%.1f, Altitude=%.1f)"),
                *GetNameSafe(ExistingPath),
                AutopilotConfig->PathRadius,
                AutopilotConfig->BaseAltitude);
        }
        else
        {
            ExistingPath->EnsureDefaultLoop();
            UE_LOG(LogFlightGameMode, Log, TEXT("Reusing waypoint path %s with default loop settings"), *GetNameSafe(ExistingPath));
        }
        return ExistingPath;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AFlightWaypointPath* Path = GetWorld()->SpawnActor<AFlightWaypointPath>(AFlightWaypointPath::StaticClass(), FTransform::Identity, SpawnParams);
    if (Path)
    {
        if (AutopilotConfig)
        {
            Path->ConfigureFromAutopilotConfig(*AutopilotConfig);
            UE_LOG(LogFlightGameMode, Log, TEXT("Created waypoint path %s from autopilot config (Radius=%.1f, Altitude=%.1f)"),
                *GetNameSafe(Path),
                AutopilotConfig->PathRadius,
                AutopilotConfig->BaseAltitude);
        }
        else
        {
            Path->EnsureDefaultLoop();
            UE_LOG(LogFlightGameMode, Log, TEXT("Created waypoint path %s with default loop settings"), *GetNameSafe(Path));
        }
    }
    else
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("Failed to spawn waypoint path actor"));
    }

    return Path;
}
