#include "FlightNavBuoyRegion.h"

#include "FlightSpatialLayoutSourceComponent.h"
#include "FlightDataSubsystem.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace FlightNavBuoyRegionConstants
{
    static constexpr float DegreesToRadians = PI / 180.f;
}

AFlightNavBuoyRegion::AFlightNavBuoyRegion()
{
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    LayoutSource = CreateDefaultSubobject<UFlightSpatialLayoutSourceComponent>(TEXT("LayoutSource"));
    LayoutSource->SetupAttachment(Root);
}

void AFlightNavBuoyRegion::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshLayout(/*bApplyOverrides=*/false);
}

void AFlightNavBuoyRegion::BeginPlay()
{
    Super::BeginPlay();
    RefreshLayout(/*bApplyOverrides=*/true);
}

FName AFlightNavBuoyRegion::GetAnchorId() const
{
    return AnchorId.IsNone() ? GetFName() : AnchorId;
}

void AFlightNavBuoyRegion::RefreshLayout(bool bApplyOverrides)
{
    const FFlightProceduralAnchorRow* OverrideRow = nullptr;

    if (bApplyOverrides)
    {
        if (UWorld* World = GetWorld())
        {
            if (UGameInstance* GameInstance = World->GetGameInstance())
            {
                if (UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>())
                {
                    OverrideRow = DataSubsystem->FindProceduralAnchorConfig(GetAnchorId(), EFlightProceduralAnchorType::NavBuoyRegion);
                }
            }
        }
    }

    TArray<FFlightSpatialLayoutRow> Rows;
    BuildBuoyRows(OverrideRow, Rows);
    if (LayoutSource)
    {
        LayoutSource->SetGeneratedRows(Rows);
    }
}

void AFlightNavBuoyRegion::BuildBuoyRows(const FFlightProceduralAnchorRow* OverrideRow, TArray<FFlightSpatialLayoutRow>& OutRows) const
{
    int32 EffectiveCount = BuoyCount;
    float EffectiveRadius = Radius;
    float EffectiveHeight = HeightOffset;
    float EffectiveAzimuth = AzimuthOffsetDeg;
    float EffectiveLightIntensity = LightIntensity;
    float EffectiveLightRadius = LightRadius;
    float EffectiveLightHeight = LightHeightOffset;
    FLinearColor EffectiveLightColor = LightColor;
    float EffectivePulseSpeed = PulseSpeed;
    float EffectiveEmissiveScale = EmissiveScale;
    float EffectiveMinMultiplier = LightMinMultiplier;
    float EffectiveMaxMultiplier = LightMaxMultiplier;

    ApplyOverride(OverrideRow, EffectiveCount, EffectiveRadius, EffectiveHeight, EffectiveAzimuth,
        EffectiveLightIntensity, EffectiveLightRadius, EffectiveLightHeight, EffectiveLightColor,
        EffectivePulseSpeed, EffectiveEmissiveScale, EffectiveMinMultiplier, EffectiveMaxMultiplier);

    EffectiveCount = FMath::Max(EffectiveCount, 1);
    EffectiveRadius = FMath::Max(EffectiveRadius, 0.f);

    OutRows.Reset(EffectiveCount);
    const FTransform ActorTransform = GetActorTransform();

    for (int32 Index = 0; Index < EffectiveCount; ++Index)
    {
        const float AngleDeg = EffectiveAzimuth + (360.f / EffectiveCount) * Index;
        const float AngleRad = AngleDeg * FlightNavBuoyRegionConstants::DegreesToRadians;

        const float CosAngle = FMath::Cos(AngleRad);
        const float SinAngle = FMath::Sin(AngleRad);

        FVector LocalPosition(EffectiveRadius * CosAngle, EffectiveRadius * SinAngle, EffectiveHeight);
        const FVector WorldPosition = ActorTransform.TransformPosition(LocalPosition);

        FRotator WorldRotation = ActorTransform.Rotator();
        WorldRotation.Yaw += AngleDeg;

        FFlightSpatialLayoutRow Row;
        Row.RowName = FName(*FString::Printf(TEXT("%s_Buoy_%02d"), *GetName(), Index));
        Row.Scenario = NAME_None;
        Row.EntityType = EFlightSpatialEntityType::NavProbe;
        Row.PositionX = WorldPosition.X;
        Row.PositionY = WorldPosition.Y;
        Row.PositionZ = WorldPosition.Z;
        Row.RotationPitch = WorldRotation.Pitch;
        Row.RotationYaw = WorldRotation.Yaw;
        Row.RotationRoll = WorldRotation.Roll;
        Row.ScaleX = 1.0f;
        Row.ScaleY = 1.0f;
        Row.ScaleZ = 1.0f;
        Row.LightIntensity = EffectiveLightIntensity;
        Row.LightRadius = EffectiveLightRadius;
        Row.LightHeightOffset = EffectiveLightHeight;
        Row.LightColorR = EffectiveLightColor.R;
        Row.LightColorG = EffectiveLightColor.G;
        Row.LightColorB = EffectiveLightColor.B;
        Row.LightColorA = EffectiveLightColor.A;
        Row.bSpawnLight = 1;
        Row.NavProbePulseSpeed = EffectivePulseSpeed;
        Row.NavProbeEmissiveScale = EffectiveEmissiveScale;
        Row.NavProbeLightMinMultiplier = EffectiveMinMultiplier;
        Row.NavProbeLightMaxMultiplier = EffectiveMaxMultiplier;
        OutRows.Add(Row);
    }
}

void AFlightNavBuoyRegion::ApplyOverride(const FFlightProceduralAnchorRow* OverrideRow,
    int32& OutCount, float& OutRadius, float& OutHeight, float& OutAzimuth,
    float& OutLightIntensity, float& OutLightRadiusValue, float& OutLightHeight, FLinearColor& OutLightColor,
    float& OutPulseSpeed, float& OutEmissiveScale, float& OutMinMultiplier, float& OutMaxMultiplier) const
{
    if (!OverrideRow || OverrideRow->AnchorType != EFlightProceduralAnchorType::NavBuoyRegion)
    {
        return;
    }

    if (OverrideRow->NavBuoyCount >= 0)
    {
        OutCount = OverrideRow->NavBuoyCount;
    }
    if (OverrideRow->NavBuoyRadius >= 0.f)
    {
        OutRadius = OverrideRow->NavBuoyRadius;
    }
    if (!FMath::IsNaN(OverrideRow->NavBuoyHeightOffset))
    {
        OutHeight = OverrideRow->NavBuoyHeightOffset;
    }
    if (OverrideRow->NavBuoyLightIntensity >= 0.f)
    {
        OutLightIntensity = OverrideRow->NavBuoyLightIntensity;
    }
    if (OverrideRow->NavBuoyLightRadius >= 0.f)
    {
        OutLightRadiusValue = OverrideRow->NavBuoyLightRadius;
    }
    if (!FMath::IsNaN(OverrideRow->NavBuoyLightHeightOffset))
    {
        OutLightHeight = OverrideRow->NavBuoyLightHeightOffset;
    }
    if (!FMath::IsNaN(OverrideRow->NavBuoyAzimuthOffsetDeg))
    {
        OutAzimuth = OverrideRow->NavBuoyAzimuthOffsetDeg;
    }
    if (OverrideRow->NavBuoyLightColorR >= 0.f)
    {
        OutLightColor.R = OverrideRow->NavBuoyLightColorR;
    }
    if (OverrideRow->NavBuoyLightColorG >= 0.f)
    {
        OutLightColor.G = OverrideRow->NavBuoyLightColorG;
    }
    if (OverrideRow->NavBuoyLightColorB >= 0.f)
    {
        OutLightColor.B = OverrideRow->NavBuoyLightColorB;
    }
    if (OverrideRow->NavBuoyLightColorA >= 0.f)
    {
        OutLightColor.A = OverrideRow->NavBuoyLightColorA;
    }
    if (OverrideRow->NavBuoyPulseSpeed >= 0.f)
    {
        OutPulseSpeed = OverrideRow->NavBuoyPulseSpeed;
    }
    if (OverrideRow->NavBuoyEmissiveScale >= 0.f)
    {
        OutEmissiveScale = OverrideRow->NavBuoyEmissiveScale;
    }
    if (OverrideRow->NavBuoyLightMinMultiplier >= 0.f)
    {
        OutMinMultiplier = OverrideRow->NavBuoyLightMinMultiplier;
    }
    if (OverrideRow->NavBuoyLightMaxMultiplier >= 0.f)
    {
        OutMaxMultiplier = OverrideRow->NavBuoyLightMaxMultiplier;
    }

    OutMinMultiplier = FMath::Max(OutMinMultiplier, 0.f);
    OutMaxMultiplier = FMath::Max(OutMaxMultiplier, OutMinMultiplier + KINDA_SMALL_NUMBER);
}
