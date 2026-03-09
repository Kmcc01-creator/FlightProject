#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightWorldBootstrapSubsystem.generated.h"

class AFlightSpatialLayoutDirector;
struct FFlightLightingConfigRow;

DECLARE_MULTICAST_DELEGATE(FOnFlightWorldBootstrapCompleted);

/**
 * Handles reusable world-preparation tasks (Mass runtime, lighting, spatial layout)
 * so GameModes, level loaders, scripting surfaces, and orchestration can reuse
 * the same setup logic without coupling that work to a specific mode asset.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightWorldBootstrapSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Runs the full bootstrap sequence (Mass resume, lighting, layout). */
    void RunBootstrap();

    /** Resumes Mass simulation if paused. */
    void ResumeMassSimulation();

    /** Applies lighting configuration and ensures night environment actors exist. */
    void EnsureNightEnvironment();

    /** Ensures a spatial layout director exists and rebuilds the layout. */
    void EnsureSpatialLayoutDirector();

    FOnFlightWorldBootstrapCompleted& OnBootstrapCompleted() { return BootstrapCompletedEvent; }

private:
    AFlightSpatialLayoutDirector* FindOrSpawnLayoutDirector();
    void ApplyLightingConfig(const FFlightLightingConfigRow* LightingConfig);

    TWeakObjectPtr<AFlightSpatialLayoutDirector> CachedSpatialLayoutDirector;
    FOnFlightWorldBootstrapCompleted BootstrapCompletedEvent;
};
