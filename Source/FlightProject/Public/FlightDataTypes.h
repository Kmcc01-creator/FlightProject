#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "FlightDataTypes.generated.h"

/**
 * Data table row describing global lighting parameters.
 */
USTRUCT(BlueprintType)
struct FFlightLightingConfigRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalIntensity = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalPitch = -10.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalYaw = 130.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalRoll = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalColorR = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalColorG = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalColorB = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float DirectionalColorA = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float SkyIntensity = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float SkyColorR = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float SkyColorG = 0.06f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float SkyColorB = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float SkyColorA = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LowerHemisphereColorR = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LowerHemisphereColorG = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LowerHemisphereColorB = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LowerHemisphereColorA = 1.f;

    FLinearColor GetDirectionalColor() const
    {
        return FLinearColor(DirectionalColorR, DirectionalColorG, DirectionalColorB, DirectionalColorA);
    }

    FRotator GetDirectionalRotation() const
    {
        return FRotator(DirectionalPitch, DirectionalYaw, DirectionalRoll);
    }

    FLinearColor GetSkyColor() const
    {
        return FLinearColor(SkyColorR, SkyColorG, SkyColorB, SkyColorA);
    }

    FLinearColor GetLowerHemisphereColor() const
    {
        return FLinearColor(LowerHemisphereColorR, LowerHemisphereColorG, LowerHemisphereColorB, LowerHemisphereColorA);
    }
};

/**
 * Data table row describing autopilot drones and navigation lighting.
 */
USTRUCT(BlueprintType)
struct FFlightAutopilotConfigRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Autopilot")
    float PathRadius = 3500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Autopilot")
    float BaseAltitude = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Autopilot")
    int32 DroneCount = 6;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Autopilot")
    float DroneSpeed = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float NavigationLightIntensity = 8000.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float NavigationLightRadius = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float NavigationLightColorR = 0.588f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float NavigationLightColorG = 0.784f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float NavigationLightColorB = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float NavigationLightColorA = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float NavigationLightHeightOffset = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    int32 bNavigationLightUseInverseSquared = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Autopilot")
    int32 bLoopPath = 1;

    FLinearColor GetNavigationLightColor() const
    {
        return FLinearColor(
            NavigationLightColorR,
            NavigationLightColorG,
            NavigationLightColorB,
            NavigationLightColorA);
    }

    bool UseInverseSquaredFalloff() const
    {
        return bNavigationLightUseInverseSquared != 0;
    }

    bool ShouldLoopPath() const
    {
        return bLoopPath != 0;
    }
};

UENUM(BlueprintType)
enum class EFlightSpatialEntityType : uint8
{
    NavProbe,
    Obstacle,
    Landmark
};

/**
 * Data table row describing authored spatial anchors and obstacles for the test range.
 */
USTRUCT(BlueprintType)
struct FFlightSpatialLayoutRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    FName Scenario = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    EFlightSpatialEntityType EntityType = EFlightSpatialEntityType::NavProbe;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float PositionX = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float PositionY = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float PositionZ = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float RotationPitch = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float RotationYaw = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float RotationRoll = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float ScaleX = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float ScaleY = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    float ScaleZ = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LightIntensity = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LightRadius = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LightHeightOffset = 220.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LightColorR = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LightColorG = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LightColorB = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
    float LightColorA = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
    int32 bSpawnLight = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavProbe")
    float NavProbePulseSpeed = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavProbe")
    float NavProbeEmissiveScale = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavProbe")
    float NavProbeLightMinMultiplier = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavProbe")
    float NavProbeLightMaxMultiplier = -1.f;

    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
    FName RowName = NAME_None;

    FVector GetLocation() const
    {
        return FVector(PositionX, PositionY, PositionZ);
    }

    FRotator GetRotation() const
    {
        return FRotator(RotationPitch, RotationYaw, RotationRoll);
    }

    FVector GetScale() const
    {
        return FVector(
            FMath::Max(ScaleX, KINDA_SMALL_NUMBER),
            FMath::Max(ScaleY, KINDA_SMALL_NUMBER),
            FMath::Max(ScaleZ, KINDA_SMALL_NUMBER));
    }

    FLinearColor GetLightColor() const
    {
        return FLinearColor(LightColorR, LightColorG, LightColorB, LightColorA);
    }

    bool ShouldSpawnLight() const
    {
        return bSpawnLight != 0 && LightIntensity > 0.f && LightRadius > 0.f;
    }
};

UENUM(BlueprintType)
enum class EFlightProceduralAnchorType : uint8
{
    Unknown,
    NavBuoyRegion,
    SpawnSwarm
};

UENUM(BlueprintType)
enum class EFlightBehaviorCompileDomainPreference : uint8
{
    Unspecified,
    NativeCpu,
    TaskGraph,
    VerseVm,
    Simd,
    Gpu
};

/**
 * Data table row describing procedural anchor overrides for editor-placeable layout sources.
 */
USTRUCT(BlueprintType)
struct FFlightProceduralAnchorRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor")
    EFlightProceduralAnchorType AnchorType = EFlightProceduralAnchorType::Unknown;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchor")
    FName AnchorId = NAME_None;

    // Nav buoy overrides
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy", meta = (ClampMin = "-1"))
    int32 NavBuoyCount = -1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyRadius = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyHeightOffset = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightIntensity = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightRadius = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightHeightOffset = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyAzimuthOffsetDeg = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyPulseSpeed = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyEmissiveScale = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightMinMultiplier = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightMaxMultiplier = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyRelativeYaw = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyRelativePitch = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyRelativeRoll = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightColorR = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightColorG = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightColorB = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavBuoy")
    float NavBuoyLightColorA = -1.f;

    // Spawn swarm overrides
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm", meta = (ClampMin = "-1"))
    int32 SwarmDroneCount = -1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    float SwarmPhaseOffsetDeg = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    float SwarmPhaseSpreadDeg = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    float SwarmAltitudeOffset = NAN;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    float SwarmSpawnRadius = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    float SwarmAutopilotSpeed = -1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm", meta = (ClampMin = "-1"))
    int32 SwarmPreferredBehaviorId = -1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    TArray<int32> SwarmAllowedBehaviorIds;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    TArray<int32> SwarmDeniedBehaviorIds;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnSwarm")
    TArray<FName> SwarmRequiredBehaviorContracts;

    bool MatchesAnchor(const FName InAnchorId, const EFlightProceduralAnchorType InType) const
    {
        return AnchorType == InType &&
            (InAnchorId.IsNone() || AnchorId.IsNone() || AnchorId == InAnchorId);
    }
};

/**
 * Data table row describing behavior compile/execution policy inputs.
 * This row expresses authored policy and preference, not runtime compile truth.
 */
USTRUCT(BlueprintType)
struct FFlightBehaviorCompilePolicyRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Optional exact behavior target. -1 means wildcard/default. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy", meta = (ClampMin = "-1"))
    int32 BehaviorId = -1;

    /** Optional cohort selector. Empty means any cohort. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    FName CohortName = NAME_None;

    /** Optional startup/profile selector. Empty means any profile. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    FName ProfileName = NAME_None;

    /** Higher priority wins among equally specific matching rows. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    int32 Priority = 0;

    /** Preferred execution domain if multiple legal backends are available. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    EFlightBehaviorCompileDomainPreference PreferredDomain = EFlightBehaviorCompileDomainPreference::Unspecified;

    /** Whether native fallback is allowed when the preferred backend is unavailable. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    int32 bAllowNativeFallback = 1;

    /** Whether generated-only/non-executable compilation is acceptable for this target. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    int32 bAllowGeneratedOnly = 0;

    /** Hint that async/task-backed execution is preferred when legal. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    int32 bPreferAsync = 0;

    /** Optional required contracts expected from the selected compiled behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    TArray<FName> RequiredContracts;

    /** Optional required symbols expected to be present during compile/bind. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    TArray<FName> RequiredSymbols;

    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "BehaviorPolicy")
    FName RowName = NAME_None;

    bool MatchesContext(const uint32 InBehaviorId, const FName InCohortName, const FName InProfileName) const
    {
        return (BehaviorId < 0 || BehaviorId == static_cast<int32>(InBehaviorId))
            && (CohortName.IsNone() || CohortName == InCohortName)
            && (ProfileName.IsNone() || ProfileName == InProfileName);
    }

    int32 GetSpecificityScore() const
    {
        return (BehaviorId >= 0 ? 4 : 0)
            + (!CohortName.IsNone() ? 2 : 0)
            + (!ProfileName.IsNone() ? 1 : 0);
    }

    bool AllowsNativeFallback() const
    {
        return bAllowNativeFallback != 0;
    }

    bool AllowsGeneratedOnly() const
    {
        return bAllowGeneratedOnly != 0;
    }

    bool PrefersAsync() const
    {
        return bPreferAsync != 0;
    }
};

inline const FFlightBehaviorCompilePolicyRow* SelectBestBehaviorCompilePolicy(
    const TArray<FFlightBehaviorCompilePolicyRow>& Policies,
    const uint32 BehaviorId,
    const FName CohortName = NAME_None,
    const FName ProfileName = NAME_None)
{
    const FFlightBehaviorCompilePolicyRow* BestRow = nullptr;
    int32 BestSpecificity = TNumericLimits<int32>::Lowest();
    int32 BestPriority = TNumericLimits<int32>::Lowest();

    for (const FFlightBehaviorCompilePolicyRow& Row : Policies)
    {
        if (!Row.MatchesContext(BehaviorId, CohortName, ProfileName))
        {
            continue;
        }

        const int32 Specificity = Row.GetSpecificityScore();
        if (!BestRow
            || Specificity > BestSpecificity
            || (Specificity == BestSpecificity && Row.Priority > BestPriority)
            || (Specificity == BestSpecificity && Row.Priority == BestPriority && Row.RowName.LexicalLess(BestRow->RowName)))
        {
            BestRow = &Row;
            BestSpecificity = Specificity;
            BestPriority = Row.Priority;
        }
    }

    return BestRow;
}
