#include "FlightWorldBootstrapSubsystem.h"

#include "FlightProject.h"
#include "FlightSpatialLayoutDirector.h"
#include "FlightSpatialLayoutSourceComponent.h"
#include "FlightSpatialTestEntity.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameModeBase.h"
#include "EngineUtils.h"

// Missing includes fixed:
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "MassSimulationSubsystem.h"
#include "FlightDataSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightWorldBootstrap, Log, All);

void UFlightWorldBootstrapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogFlightWorldBootstrap, Verbose, TEXT("FlightWorldBootstrapSubsystem initialized for world %s"), *GetNameSafe(GetWorld()));
}

void UFlightWorldBootstrapSubsystem::RunBootstrap()
{
    ResumeMassSimulation();
    EnsureNightEnvironment();
    EnsureSpatialLayoutDirector();

    BootstrapCompletedEvent.Broadcast();
}

void UFlightWorldBootstrapSubsystem::ResumeMassSimulation()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogFlightWorldBootstrap, Warning, TEXT("ResumeMassSimulation skipped: world not available"));
        return;
    }

    if (UMassSimulationSubsystem* SimulationSubsystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(World))
    {
        if (SimulationSubsystem->IsSimulationPaused())
        {
            SimulationSubsystem->ResumeSimulation();
            UE_LOG(LogFlightWorldBootstrap, Log, TEXT("Resumed MassSimulationSubsystem simulation"));
        }
        else
        {
            UE_LOG(LogFlightWorldBootstrap, Verbose, TEXT("Mass simulation already active; no action taken"));
        }
    }
    else
    {
        UE_LOG(LogFlightWorldBootstrap, Warning, TEXT("MassSimulationSubsystem not found; Mass agents will remain inactive"));
    }
}

void UFlightWorldBootstrapSubsystem::EnsureNightEnvironment()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogFlightWorldBootstrap, Warning, TEXT("EnsureNightEnvironment skipped: world not available"));
        return;
    }

    const UFlightDataSubsystem* DataSubsystem = nullptr;
    if (UGameInstance* GameInstance = World->GetGameInstance())
    {
        DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    }

    const FFlightLightingConfigRow* LightingConfig = DataSubsystem ? DataSubsystem->GetLightingConfig() : nullptr;
    ApplyLightingConfig(LightingConfig);
}

void UFlightWorldBootstrapSubsystem::EnsureSpatialLayoutDirector()
{
    if (AFlightSpatialLayoutDirector* LayoutDirector = FindOrSpawnLayoutDirector())
    {
        LayoutDirector->RebuildLayout();
    }
}

AFlightSpatialLayoutDirector* UFlightWorldBootstrapSubsystem::FindOrSpawnLayoutDirector()
{
    if (CachedSpatialLayoutDirector.IsValid())
    {
        return CachedSpatialLayoutDirector.Get();
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogFlightWorldBootstrap, Warning, TEXT("Unable to ensure spatial layout director: world not available"));
        return nullptr;
    }

    for (TActorIterator<AFlightSpatialLayoutDirector> It(World); It; ++It)
    {
        CachedSpatialLayoutDirector = *It;
        UE_LOG(LogFlightWorldBootstrap, Log, TEXT("Spatial layout director already present: %s"), *GetNameSafe(*It));
        return CachedSpatialLayoutDirector.Get();
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = Cast<AActor>(World->GetAuthGameMode());
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AFlightSpatialLayoutDirector* NewDirector = World->SpawnActor<AFlightSpatialLayoutDirector>(AFlightSpatialLayoutDirector::StaticClass(), FTransform::Identity, SpawnParams))
    {
        UE_LOG(LogFlightWorldBootstrap, Log, TEXT("Spawned spatial layout director: %s"), *GetNameSafe(NewDirector));
        CachedSpatialLayoutDirector = NewDirector;
        return NewDirector;
    }

    UE_LOG(LogFlightWorldBootstrap, Error, TEXT("Failed to spawn spatial layout director"));
    return nullptr;
}

void UFlightWorldBootstrapSubsystem::ApplyLightingConfig(const FFlightLightingConfigRow* LightingConfig)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const float DirectionalIntensity = LightingConfig ? LightingConfig->DirectionalIntensity : 1500.f;
    const FLinearColor DirectionalColor = LightingConfig ? LightingConfig->GetDirectionalColor() : FLinearColor(0.05f, 0.08f, 0.18f, 1.f);
    const FRotator DirectionalRotation = LightingConfig ? LightingConfig->GetDirectionalRotation() : FRotator(-10.f, 130.f, 0.f);

    const float SkyIntensity = LightingConfig ? LightingConfig->SkyIntensity : 0.25f;
    const FLinearColor SkyColor = LightingConfig ? LightingConfig->GetSkyColor() : FLinearColor(0.02f, 0.06f, 0.12f, 1.f);
    const FLinearColor LowerHemisphereColor = LightingConfig ? LightingConfig->GetLowerHemisphereColor() : FLinearColor::Black;

    UE_LOG(LogFlightWorldBootstrap, Log, TEXT("Applying lighting config: DirectionalIntensity=%.1f, SkyIntensity=%.2f (Source=%s)"),
        DirectionalIntensity,
        SkyIntensity,
        LightingConfig ? TEXT("DataTable") : TEXT("Defaults"));

    ADirectionalLight* DirectionalLight = nullptr;
    for (TActorIterator<ADirectionalLight> It(World); It; ++It)
    {
        DirectionalLight = *It;
        break;
    }

    if (!DirectionalLight)
    {
        DirectionalLight = World->SpawnActor<ADirectionalLight>();
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
    for (TActorIterator<ASkyLight> It(World); It; ++It)
    {
        SkyLight = *It;
        break;
    }

    if (!SkyLight)
    {
        SkyLight = World->SpawnActor<ASkyLight>();
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
