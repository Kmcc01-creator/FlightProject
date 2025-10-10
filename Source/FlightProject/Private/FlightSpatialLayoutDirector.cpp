#include "FlightSpatialLayoutDirector.h"

#include "FlightSpatialTestEntity.h"
#include "FlightDataSubsystem.h"
#include "FlightDataTypes.h"
#include "FlightProject.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

AFlightSpatialLayoutDirector::AFlightSpatialLayoutDirector()
{
    PrimaryActorTick.bCanEverTick = false;
    NavProbeClass = AFlightSpatialTestEntity::StaticClass();
    ObstacleClass = AFlightSpatialTestEntity::StaticClass();
    LandmarkClass = AFlightSpatialTestEntity::StaticClass();
}

void AFlightSpatialLayoutDirector::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

#if WITH_EDITOR
    if (bRespawnOnConstruction)
    {
        RebuildLayout();
    }
#endif
}

void AFlightSpatialLayoutDirector::BeginPlay()
{
    Super::BeginPlay();
    RebuildLayout();
}

#if WITH_EDITOR
void AFlightSpatialLayoutDirector::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (PropertyChangedEvent.Property)
    {
        const FName PropertyName = PropertyChangedEvent.Property->GetFName();
        if (PropertyName == GET_MEMBER_NAME_CHECKED(AFlightSpatialLayoutDirector, bRespawnOnConstruction) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AFlightSpatialLayoutDirector, NavProbeClass) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AFlightSpatialLayoutDirector, ObstacleClass) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AFlightSpatialLayoutDirector, LandmarkClass))
        {
            RebuildLayout();
        }
    }
}
#endif

void AFlightSpatialLayoutDirector::RebuildLayout()
{
    DestroySpawnedActors();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UGameInstance* GameInstance = World->GetGameInstance();
    if (!GameInstance)
    {
#if !WITH_EDITOR
        UE_LOG(LogFlightProject, Warning, TEXT("FlightSpatialLayoutDirector: No game instance available; cannot read spatial layout."));
#endif
        return;
    }

    UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();

    if (!DataSubsystem)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightSpatialLayoutDirector: FlightDataSubsystem not available."));
        return;
    }

    const TArray<FFlightSpatialLayoutRow>& LayoutRows = DataSubsystem->GetSpatialLayout();
    if (LayoutRows.Num() == 0)
    {
        if (!DataSubsystem->HasSpatialLayout())
        {
            UE_LOG(LogFlightProject, Warning, TEXT("FlightSpatialLayoutDirector: Spatial layout data failed to load; nothing spawned."));
        }
        return;
    }

    for (const FFlightSpatialLayoutRow& Row : LayoutRows)
    {
        SpawnLayoutRow(Row);
    }
}

void AFlightSpatialLayoutDirector::DestroySpawnedActors()
{
    for (TWeakObjectPtr<AActor>& ActorPtr : SpawnedActors)
    {
        if (AActor* Actor = ActorPtr.Get())
        {
            Actor->Destroy();
        }
    }
    SpawnedActors.Reset();
}

void AFlightSpatialLayoutDirector::SpawnLayoutRow(const FFlightSpatialLayoutRow& Row)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    TSubclassOf<AFlightSpatialTestEntity> SpawnClass = ResolveClassForType(Row);
    if (!SpawnClass)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AFlightSpatialTestEntity* Spawned = World->SpawnActor<AFlightSpatialTestEntity>(SpawnClass, FTransform::Identity, SpawnParams);
    if (Spawned)
    {
        Spawned->ApplyLayoutRow(Row);
        SpawnedActors.Add(Spawned);
    }
}

TSubclassOf<AFlightSpatialTestEntity> AFlightSpatialLayoutDirector::ResolveClassForType(const FFlightSpatialLayoutRow& Row) const
{
    switch (Row.EntityType)
    {
        case EFlightSpatialEntityType::Obstacle:
            if (ObstacleClass)
            {
                return ObstacleClass;
            }
            return AFlightSpatialTestEntity::StaticClass();

        case EFlightSpatialEntityType::Landmark:
            if (LandmarkClass)
            {
                return LandmarkClass;
            }
            return AFlightSpatialTestEntity::StaticClass();

        case EFlightSpatialEntityType::NavProbe:
        default:
            if (NavProbeClass)
            {
                return NavProbeClass;
            }
            return AFlightSpatialTestEntity::StaticClass();
    }
}
