#include "FlightSpatialTestEntity.h"

#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AFlightSpatialTestEntity::AFlightSpatialTestEntity()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    MeshComponent->SetupAttachment(RootComponent);
    MeshComponent->SetMobility(EComponentMobility::Movable);
    MeshComponent->SetGenerateOverlapEvents(false);
    MeshComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));

    NavigationLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("NavLight"));
    NavigationLight->SetupAttachment(RootComponent);
    NavigationLight->SetMobility(EComponentMobility::Movable);
    NavigationLight->SetRelativeLocation(FVector(0.f, 0.f, 220.f));
    NavigationLight->bUseInverseSquaredFalloff = false;
    NavigationLight->SetCastShadows(false);
    NavigationLight->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMeshFinder.Succeeded())
    {
        NavProbeMesh = SphereMeshFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeMeshFinder.Succeeded())
    {
        ObstacleMesh = CubeMeshFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMeshFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (ConeMeshFinder.Succeeded())
    {
        LandmarkMesh = ConeMeshFinder.Object;
    }

    LayoutSnapshot = FFlightSpatialLayoutRow();
}

void AFlightSpatialTestEntity::BeginPlay()
{
    Super::BeginPlay();
#if WITH_EDITOR
    UpdateDebugLabel();
#endif
}

void AFlightSpatialTestEntity::ApplyLayoutRow(const FFlightSpatialLayoutRow& LayoutRow)
{
    LayoutSnapshot = LayoutRow;

    ConfigureMeshForType(LayoutRow.EntityType);

    SetActorLocation(LayoutRow.GetLocation());
    SetActorRotation(LayoutRow.GetRotation());
    SetActorScale3D(LayoutRow.GetScale());

    NavigationLight->SetVisibility(LayoutRow.ShouldSpawnLight());
    NavigationLight->SetIntensity(LayoutRow.LightIntensity);
    NavigationLight->SetAttenuationRadius(LayoutRow.LightRadius);
    NavigationLight->SetRelativeLocation(FVector(0.f, 0.f, LayoutRow.LightHeightOffset));
    NavigationLight->SetLightColor(LayoutRow.GetLightColor());

#if WITH_EDITOR
    UpdateDebugLabel();
#endif
}

void AFlightSpatialTestEntity::ConfigureMeshForType(EFlightSpatialEntityType EntityType)
{
    switch (EntityType)
    {
        case EFlightSpatialEntityType::NavProbe:
        {
            MeshComponent->SetStaticMesh(NavProbeMesh ? NavProbeMesh : ObstacleMesh);
            MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
            MeshComponent->SetRelativeLocation(FVector::ZeroVector);
            break;
        }
        case EFlightSpatialEntityType::Landmark:
        {
            MeshComponent->SetStaticMesh(LandmarkMesh ? LandmarkMesh : ObstacleMesh);
            MeshComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
            MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
            break;
        }
        case EFlightSpatialEntityType::Obstacle:
        default:
        {
            MeshComponent->SetStaticMesh(ObstacleMesh ? ObstacleMesh : NavProbeMesh);
            MeshComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
            MeshComponent->SetRelativeLocation(FVector::ZeroVector);
            break;
        }
    }
}

void AFlightSpatialTestEntity::UpdateDebugLabel()
{
#if WITH_EDITOR
    if (!bShowDebugLabel)
    {
        return;
    }

    const UEnum* EnumPtr = StaticEnum<EFlightSpatialEntityType>();
    const FString TypeString = EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(LayoutSnapshot.EntityType)) : TEXT("Entity");
    const FString Label = FString::Printf(TEXT("%s_%s"), *TypeString, *LayoutSnapshot.RowName.ToString());
    SetActorLabel(Label);
#endif
}
