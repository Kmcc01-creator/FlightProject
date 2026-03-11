#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightWaypointPathRegistry.generated.h"

class AFlightWaypointPath;

/**
 * Lightweight path data for cache-efficient processor access.
 * Pre-sampled spline data avoids USplineComponent lookups in hot loops.
 */
USTRUCT()
struct FLIGHTPROJECT_API FFlightPathData
{
    GENERATED_BODY()

    /** Total path length in cm */
    float TotalLength = 0.f;

    /** Whether path is closed loop */
    bool bClosedLoop = true;

    /** Pre-sampled location points (every 100cm along path) */
    TArray<FVector> SampledLocations;

    /** Pre-sampled rotation at each sample point */
    TArray<FQuat> SampledRotations;

    /** Pre-sampled forward tangent at each sample point */
    TArray<FVector> SampledTangents;

    /** Distance between samples */
    float SampleInterval = 100.f;

    /** Sample spline location at given distance using LUT */
    FVector SampleLocation(float Distance) const;

    /** Sample spline rotation at given distance using LUT */
    FQuat SampleRotation(float Distance) const;

    /** Sample forward tangent at given distance */
    FVector SampleTangent(float Distance) const;

private:
    /** Get interpolation parameters for a distance value */
    void GetSampleIndices(float Distance, int32& OutIndex0, int32& OutIndex1, float& OutAlpha) const;
};

/**
 * World subsystem that maintains a lightweight registry of flight paths.
 *
 * Processors query this instead of dereferencing AFlightWaypointPath actors,
 * avoiding UObject overhead in hot loops. Paths are pre-sampled into lookup
 * tables for cache-efficient access.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightWaypointPathRegistry : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //~ USubsystem interface
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    //~ End USubsystem interface

    /**
     * Register a waypoint path and get its handle.
     * Pre-samples the spline into a LUT for fast processor access.
     * @param Path The waypoint path actor to register
     * @param SampleInterval Distance between samples in cm (default 100)
     * @return Unique identifier for this path
     */
    FGuid RegisterPath(AFlightWaypointPath* Path, float SampleInterval = 100.f);

    /**
     * Register or refresh a synthetic polyline path for non-actor navigation commits.
     * @param ControlPoints Ordered world-space control points describing the path
     * @param bClosedLoop Whether the synthetic path should wrap back to the start
     * @param SampleInterval Distance between generated samples in cm
     * @param PreferredPathId Optional stable identifier to reuse when refreshing a synthetic path
     */
    FGuid RegisterSyntheticPath(
        const TArray<FVector>& ControlPoints,
        bool bClosedLoop,
        float SampleInterval = 100.f,
        const FGuid& PreferredPathId = FGuid());

    /**
     * Unregister a path when it's destroyed or no longer needed.
     * @param PathId The path identifier returned by RegisterPath
     */
    void UnregisterPath(const FGuid& PathId);

    /**
     * Find path data by ID. Returns nullptr if not found.
     * This is the primary access method used by processors.
     * @param PathId The path identifier
     * @return Pointer to path data, or nullptr if not registered
     */
    const FFlightPathData* FindPath(const FGuid& PathId) const;

    /**
     * Check if a path is registered.
     * @param PathId The path identifier
     * @return True if path exists in registry
     */
    bool IsPathRegistered(const FGuid& PathId) const;

    /**
     * Get total number of registered paths.
     */
    int32 GetNumPaths() const { return PathRegistry.Num(); }

    /**
     * Rebuild the LUT for a path (call after spline changes).
     * @param PathId The path identifier
     * @param Path The updated path actor
     */
    void RefreshPath(const FGuid& PathId, AFlightWaypointPath* Path);

private:
    /** Map from path ID to pre-sampled path data */
    UPROPERTY()
    TMap<FGuid, FFlightPathData> PathRegistry;

    /** Build LUT from spline component */
    void BuildPathLUT(FFlightPathData& OutData, AFlightWaypointPath* Path, float SampleInterval);
    void BuildSyntheticPathLUT(FFlightPathData& OutData, const TArray<FVector>& ControlPoints, bool bClosedLoop, float SampleInterval);
};
