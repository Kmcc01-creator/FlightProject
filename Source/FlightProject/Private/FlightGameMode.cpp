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
#include "FlightSpawnSwarmAnchor.h"

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

    struct FSwarmAnchorGroup
    {
        AFlightSpawnSwarmAnchor* Anchor = nullptr;
        int32 DroneCount = 0;
        float PhaseOffsetDeg = 0.f;
        float PhaseSpreadDeg = 360.f;
        float SpeedOverride = -1.f;
    };

    TArray<FSwarmAnchorGroup> SwarmGroups;
    int32 TotalAnchorDrones = 0;

    for (TActorIterator<AFlightSpawnSwarmAnchor> AnchorIt(GetWorld()); AnchorIt; ++AnchorIt)
    {
        AFlightSpawnSwarmAnchor* Anchor = *AnchorIt;
        if (!Anchor)
        {
            continue;
        }

        const int32 AnchorDroneCount = Anchor->GetDroneCount();
        if (AnchorDroneCount <= 0)
        {
            continue;
        }

        FSwarmAnchorGroup Group;
        Group.Anchor = Anchor;
        Group.DroneCount = AnchorDroneCount;
        Group.PhaseOffsetDeg = Anchor->GetPhaseOffsetDeg();
        Group.PhaseSpreadDeg = Anchor->GetPhaseSpreadDeg();
        Group.SpeedOverride = Anchor->GetAutopilotSpeedOverride();
        SwarmGroups.Add(Group);
        TotalAnchorDrones += AnchorDroneCount;
    }

    const bool bAnchorsProvideSpawns = TotalAnchorDrones > 0;
    const int32 DroneCount = bAnchorsProvideSpawns
        ? TotalAnchorDrones
        : FMath::Max(AutopilotConfig ? AutopilotConfig->DroneCount : 6, 1);
    const bool bLoopPath = AutopilotConfig ? AutopilotConfig->ShouldLoopPath() : true;

    if (DroneCount <= 0)
    {
        UE_LOG(LogFlightGameMode, Warning, TEXT("SpawnAutonomousFlights: No drones requested; skipping spawn."));
        return;
    }

    UE_LOG(LogFlightGameMode, Log, TEXT("Spawning %d autonomous drones on path %s (Length=%.2f, Loop=%s)%s"),
        DroneCount,
        *GetNameSafe(WaypointPath),
        PathLength,
        bLoopPath ? TEXT("true") : TEXT("false"),
        bAnchorsProvideSpawns ? TEXT(" using anchor overrides") : TEXT(""));

    struct FSpawnOrder
    {
        float Distance = 0.f;
        float SpeedOverride = -1.f;
        AFlightSpawnSwarmAnchor* SourceAnchor = nullptr;
        int32 GlobalIndex = 0;
    };

    TArray<FSpawnOrder> SpawnOrders;
    SpawnOrders.Reserve(DroneCount);

    if (!bAnchorsProvideSpawns)
    {
        for (int32 DroneIndex = 0; DroneIndex < DroneCount; ++DroneIndex)
        {
            FSpawnOrder Order;
            Order.Distance = (PathLength / DroneCount) * DroneIndex;
            Order.GlobalIndex = DroneIndex;
            SpawnOrders.Add(Order);
        }
    }
    else
    {
        const float DefaultIncrement = PathLength / FMath::Max(DroneCount, 1);
        int32 GlobalIndex = 0;

        for (const FSwarmAnchorGroup& Group : SwarmGroups)
        {
            const float BaseFraction = FMath::Fmod(Group.PhaseOffsetDeg, 360.f) / 360.f;
            float NormalizedBase = BaseFraction;
            if (NormalizedBase < 0.f)
            {
                NormalizedBase += 1.f;
            }

            const float SpreadFraction = Group.PhaseSpreadDeg / 360.f;
            const bool bUsePhaseSpread = SpreadFraction > KINDA_SMALL_NUMBER;
            const float StepFraction = bUsePhaseSpread && Group.DroneCount > 0
                ? SpreadFraction / Group.DroneCount
                : 0.f;

            for (int32 LocalIndex = 0; LocalIndex < Group.DroneCount; ++LocalIndex)
            {
                FSpawnOrder Order;
                if (bUsePhaseSpread)
                {
                    float Fraction = NormalizedBase + StepFraction * LocalIndex;
                    Fraction = FMath::Fmod(Fraction, 1.f);
                    if (Fraction < 0.f)
                    {
                        Fraction += 1.f;
                    }
                    Order.Distance = Fraction * PathLength;
                }
                else
                {
                    Order.Distance = DefaultIncrement * GlobalIndex;
                }

                Order.SpeedOverride = Group.SpeedOverride;
                Order.SourceAnchor = Group.Anchor;
                Order.GlobalIndex = GlobalIndex;
                SpawnOrders.Add(Order);
                ++GlobalIndex;
            }
        }
    }

    for (int32 OrderIndex = 0; OrderIndex < SpawnOrders.Num(); ++OrderIndex)
    {
        const FSpawnOrder& Order = SpawnOrders[OrderIndex];

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        SpawnParams.Owner = this;

        if (AFlightAIPawn* Drone = GetWorld()->SpawnActor<AFlightAIPawn>(AFlightAIPawn::StaticClass(), FTransform::Identity, SpawnParams))
        {
            if (AutopilotConfig)
            {
                Drone->ApplyAutopilotConfig(*AutopilotConfig);
            }

            Drone->SetFlightPath(WaypointPath, Order.Distance, bLoopPath);

            if (Order.SpeedOverride > 0.f)
            {
                Drone->SetAutopilotSpeed(Order.SpeedOverride);
            }

            if (Order.SourceAnchor)
            {
                UE_LOG(LogFlightGameMode, Log, TEXT("Spawned drone %s [%d/%d] at distance %.2f via anchor %s"),
                    *GetNameSafe(Drone),
                    Order.GlobalIndex + 1,
                    DroneCount,
                    Order.Distance,
                    *GetNameSafe(Order.SourceAnchor));
            }
            else
            {
                UE_LOG(LogFlightGameMode, Log, TEXT("Spawned drone %s [%d/%d] at distance %.2f"),
                    *GetNameSafe(Drone),
                    Order.GlobalIndex + 1,
                    DroneCount,
                    Order.Distance);
            }
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("Failed to spawn drone %d of %d"), Order.GlobalIndex + 1, DroneCount);
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
