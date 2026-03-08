#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "FlightMassFragments.generated.h"

/**
 * Path-following data for autonomous flight entities.
 * Replaces the data previously stored on AFlightAIPawn.
 */
USTRUCT()
struct FLIGHTPROJECT_API FFlightPathFollowFragment : public FMassFragment
{
    GENERATED_BODY()

    /** Handle to the path in the registry (lightweight, no UObject reference) */
    UPROPERTY()
    FGuid PathId;

    /** Current distance traveled along the path in cm */
    UPROPERTY()
    float CurrentDistance = 0.f;

    /** Target speed in cm/s */
    UPROPERTY()
    float DesiredSpeed = 1500.f;

    /** Whether to loop back to start when reaching path end */
    UPROPERTY()
    bool bLooping = true;
};

/**
 * Transform state for flight entities.
 * Kept separate from path data for cache efficiency during physics updates.
 */
USTRUCT()
struct FLIGHTPROJECT_API FFlightTransformFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Location = FVector::ZeroVector;

    UPROPERTY()
    FQuat Rotation = FQuat::Identity;

    UPROPERTY()
    FVector Velocity = FVector::ZeroVector;
};

/**
 * Fragment tracking whether this entity was modified by VEX/Verse this frame.
 * Used for sparse blitting to the GPU.
 */
USTRUCT()
struct FLIGHTPROJECT_API FFlightVexDirtyFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Bitmask or simple bool indicating if CPU state needs sync to GPU */
	UPROPERTY()
	bool bIsDirty = false;
};

/**
 * Visual/rendering properties for flight entities.
 * Only required when representation (Actor) is spawned.
 */
USTRUCT()
struct FLIGHTPROJECT_API FFlightVisualFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FLinearColor LightColor = FLinearColor(0.588f, 0.784f, 1.0f, 1.0f);

    UPROPERTY()
    float LightIntensity = 8000.f;

    UPROPERTY()
    float LightRadius = 1500.f;

    UPROPERTY()
    float LightHeightOffset = 250.f;

    UPROPERTY()
    uint8 bUseInverseSquaredFalloff : 1;

    FFlightVisualFragment()
        : bUseInverseSquaredFalloff(false)
    {
    }
};

/**
 * Tag fragment indicating this entity is part of a swarm.
 * Used for batch operations and queries.
 */
USTRUCT()
struct FLIGHTPROJECT_API FFlightSwarmMemberTag : public FMassTag
{
    GENERATED_BODY()
};

/**
 * Shared fragment for entities on the same path.
 * Reduces memory when many entities share path configuration.
 */
USTRUCT()
struct FLIGHTPROJECT_API FFlightSharedPathFragment : public FMassSharedFragment
{
    GENERATED_BODY()

    /** Path identifier for registry lookup */
    UPROPERTY()
    FGuid PathId;

    /** Cached path length to avoid repeated lookups */
    UPROPERTY()
    float CachedPathLength = 0.f;

    /** Default speed for entities on this path */
    UPROPERTY()
    float DefaultSpeed = 1500.f;

    /** Whether entities on this path should loop */
    UPROPERTY()
    bool bDefaultLooping = true;
};
