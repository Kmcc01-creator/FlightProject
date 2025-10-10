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
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"

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
    InitializeMassRuntime();
    SetupNightEnvironment();
    BuildSpatialTestRange();
    SpawnAutonomousFlights();
}

void AFlightGameMode::InitializeMassRuntime()
{
    if (!GetWorld())
    {
        return;
    }

    if (UMassSimulationSubsystem* SimulationSubsystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld()))
    {
        if (SimulationSubsystem->IsSimulationPaused())
        {
            SimulationSubsystem->ResumeSimulation();
        }
    }
}

void AFlightGameMode::SetupNightEnvironment()
{
    if (!GetWorld())
    {
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
            DirectionalComponent->SetIntensity(DirectionalIntensity);
            DirectionalComponent->SetLightColor(DirectionalColor);
            DirectionalComponent->SetAtmosphereSunLightIndex(0);
            DirectionalComponent->SetEnableLightShaftOcclusion(true);
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
            SkyComponent->SetIntensity(SkyIntensity);
            SkyComponent->SetLightColor(SkyColor);
            SkyComponent->SetLowerHemisphereColor(LowerHemisphereColor);
        }
    }
}

void AFlightGameMode::BuildSpatialTestRange()
{
    if (!GetWorld())
    {
        return;
    }

    for (TActorIterator<AFlightSpatialLayoutDirector> It(GetWorld()); It; ++It)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    GetWorld()->SpawnActor<AFlightSpatialLayoutDirector>(AFlightSpatialLayoutDirector::StaticClass(), FTransform::Identity, SpawnParams);
}

void AFlightGameMode::SpawnAutonomousFlights()
{
    if (!GetWorld())
    {
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
        return;
    }

    const float PathLength = WaypointPath->GetPathLength();
    if (PathLength <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const int32 DroneCount = FMath::Max(AutopilotConfig ? AutopilotConfig->DroneCount : 6, 1);
    const bool bLoopPath = AutopilotConfig ? AutopilotConfig->ShouldLoopPath() : true;

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
        }
    }
}

AFlightWaypointPath* AFlightGameMode::FindOrCreateWaypointPath(const FFlightAutopilotConfigRow* AutopilotConfig)
{
    if (!GetWorld())
    {
        return nullptr;
    }

    for (TActorIterator<AFlightWaypointPath> It(GetWorld()); It; ++It)
    {
        AFlightWaypointPath* ExistingPath = *It;
        if (AutopilotConfig)
        {
            ExistingPath->ConfigureFromAutopilotConfig(*AutopilotConfig);
        }
        else
        {
            ExistingPath->EnsureDefaultLoop();
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
        }
        else
        {
            Path->EnsureDefaultLoop();
        }
    }

    return Path;
}
