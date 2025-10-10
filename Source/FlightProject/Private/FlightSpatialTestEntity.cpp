#include "FlightSpatialTestEntity.h"

#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AFlightSpatialTestEntity::AFlightSpatialTestEntity()
{
    PrimaryActorTick.bCanEverTick = true;
    SetActorTickEnabled(false);

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

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (CylinderMeshFinder.Succeeded())
    {
        NavProbeMesh = CylinderMeshFinder.Object;
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

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> NavProbeMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (NavProbeMaterialFinder.Succeeded())
    {
        NavProbeBaseMaterial = NavProbeMaterialFinder.Object;
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

void AFlightSpatialTestEntity::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bIsNavProbe)
    {
        UpdateNavProbePulse(DeltaSeconds);
    }
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

    bIsNavProbe = (LayoutRow.EntityType == EFlightSpatialEntityType::NavProbe);
    if (bIsNavProbe)
    {
        SetupNavProbeVisuals();
    }
    else
    {
        SetActorTickEnabled(false);
        NavProbeMID = nullptr;
    }

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

void AFlightSpatialTestEntity::SetupNavProbeVisuals()
{
    if (!NavProbeBaseMaterial)
    {
        return;
    }

    if (!NavProbeMID)
    {
        NavProbeMID = UMaterialInstanceDynamic::Create(NavProbeBaseMaterial, this);
    }

    if (NavProbeMID)
    {
        MeshComponent->SetMaterial(0, NavProbeMID);
        NavProbeMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.35f, 0.02f, 0.02f, 1.f));
    }

    if (LayoutSnapshot.NavProbePulseSpeed >= 0.f)
    {
        NavProbePulseSpeed = FMath::Max(0.01f, LayoutSnapshot.NavProbePulseSpeed);
    }

    if (LayoutSnapshot.NavProbeEmissiveScale >= 0.f)
    {
        NavProbeMeshEmissiveScale = FMath::Max(0.01f, LayoutSnapshot.NavProbeEmissiveScale);
    }

    if (LayoutSnapshot.NavProbeLightMinMultiplier >= 0.f)
    {
        NavProbeMinIntensityMultiplier = LayoutSnapshot.NavProbeLightMinMultiplier;
    }

    if (LayoutSnapshot.NavProbeLightMaxMultiplier >= 0.f)
    {
        NavProbeMaxIntensityMultiplier = LayoutSnapshot.NavProbeLightMaxMultiplier;
    }

    NavigationLight->SetLightColor(FLinearColor(1.f, 0.15f, 0.05f));
    NavProbeBaseLightIntensity = FMath::Max(LayoutSnapshot.LightIntensity, 100.f);
    const float MinMultiplier = FMath::Max(NavProbeMinIntensityMultiplier, 0.f);
    const float MaxMultiplier = FMath::Max(NavProbeMaxIntensityMultiplier, MinMultiplier + KINDA_SMALL_NUMBER);
    NavProbeLightMinIntensity = FMath::Max(NavProbeBaseLightIntensity * MinMultiplier, 50.f);
    NavProbeLightMaxIntensity = FMath::Max(NavProbeBaseLightIntensity * MaxMultiplier, NavProbeLightMinIntensity + 25.f);
    PulseTime = FMath::FRandRange(0.f, 2.f * PI);
    SetActorTickEnabled(true);
}

void AFlightSpatialTestEntity::UpdateNavProbePulse(float DeltaSeconds)
{
    PulseTime += DeltaSeconds * NavProbePulseSpeed * 2.f * PI;
    const float PulseAlpha = 0.5f + 0.5f * FMath::Sin(PulseTime);

    const float TargetIntensity = FMath::Lerp(NavProbeLightMinIntensity, NavProbeLightMaxIntensity, PulseAlpha);
    NavigationLight->SetIntensity(TargetIntensity);

    if (NavProbeMID)
    {
        const float EmissiveStrength = FMath::Lerp(0.5f, NavProbeMeshEmissiveScale, PulseAlpha);
        const FLinearColor EmissiveColor(EmissiveStrength, 0.05f * EmissiveStrength, 0.05f * EmissiveStrength, 1.f);
        NavProbeMID->SetVectorParameterValue(TEXT("Color"), EmissiveColor);
    }
}
