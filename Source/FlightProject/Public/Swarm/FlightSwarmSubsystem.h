// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Core/FlightReactive.h"
#include "RenderGraphResources.h"
#include "Tickable.h"

// Forward decl
namespace Flight::Swarm { class FSwarmSceneViewExtension; }

#include "FlightSwarmSubsystem.generated.h"

using namespace Flight::Reactive;
using namespace Flight::Swarm;

enum class EFlightSwarmPersistenceMode : uint8
{
	Stateless = 0,
	Persistent = 1
};

UENUM(BlueprintType)
enum class ESwarmSortRequirement : uint8
{
	None,
	Behavior, // Re-sort for warp coherency (BehaviorID changes)
	Spatial,  // Re-sort for neighbor lookups (SPH)
	Full      // Re-sort everything
};

struct FFlightSwarmPersistenceDebugSnapshot
{
	uint32 FrameIndex = 0;
	EFlightSwarmPersistenceMode RequestedMode = EFlightSwarmPersistenceMode::Stateless;
	EFlightSwarmPersistenceMode AppliedMode = EFlightSwarmPersistenceMode::Stateless;
	bool bLatticePersistentValid = false;
	bool bCloudPersistentValid = false;
	bool bLatticePersistentMatches = false;
	bool bCloudPersistentMatches = false;
	bool bUsedPriorLatticeFrame = false;
	bool bUsedPriorCloudFrame = false;
	uint32 LatticeResolution = 0;
	float LatticeExtent = 0.0f;
	uint32 CloudResolution = 0;
	float CloudExtent = 0.0f;
};

/**
 * UFlightSwarmSubsystem
 * 
 * Manages massive-scale GPU swarm simulation.
 * Bridges reactive CPU parameters to the persistent GPU simulation state.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightSwarmSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- FTickableGameObject Interface ---
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return TotalEntities > 0; }
	virtual TStatId GetStatId() const override;
	// --- End Interface ---

	/** Initialize the GPU swarm with N entities */
	UFUNCTION(BlueprintCallable, Category = "Flight|Swarm")
	void InitializeSwarm(int32 NumEntities);

	/** Reactive simulation parameters */
	TReactiveValue<float> SmoothingRadius{500.0f};
	TReactiveValue<float> GasConstant{2000.0f};
	TReactiveValue<float> RestDensity{1.0f};
	TReactiveValue<float> Viscosity{0.1f};
	TReactiveValue<float> AvoidanceStrength{1000.0f};
	TReactiveValue<int32> PredictionHorizon{10};

	/** SCSL: Light Lattice Parameters */
	TReactiveValue<int32> LatticeResolution{64};
	TReactiveValue<float> LatticeExtent{10000.0f};
	TReactiveValue<float> LatticeDecay{0.95f};
	TReactiveValue<float> LatticePropStrength{0.15f};

	/** SCSL: Volumetric Cloud Parameters */
	TReactiveValue<int32> CloudResolution{64};
	TReactiveValue<float> CloudExtent{10000.0f};
	TReactiveValue<float> CloudDecay{0.98f};
	TReactiveValue<float> CloudDiffusion{0.1f};
	TReactiveValue<float> CloudInjection{1.0f};

	/** Gameplay state */
	TReactiveValue<FVector> PlayerPosition{FVector::ZeroVector};
	TReactiveValue<FVector> BeaconPosition{FVector::ZeroVector};

	/** Get the persistent GPU buffer (Internal use for RDG) */
	TRefCountPtr<FRDGPooledBuffer> GetDroidStateBuffer() { return DroidStateBuffer; }
	int32 GetNumEntities() const { return TotalEntities; }
	FFlightSwarmPersistenceDebugSnapshot GetLastPersistenceDebugSnapshot() const;

	/** Manual tick for simulation (used by automation tests) */
	void TickSimulation(float DeltaTime);

	/** 
	 * Updates a range of entities in the GPU buffer. 
	 * Used for sparse blitting from Mass ECS.
	 */
	void UpdateDroidState_Sparse(int32 StartIndex, const TArrayView<const FDroidState>& Data);

	/** Requests a GPU sort for the next simulation tick */
	void RequestSort(ESwarmSortRequirement Requirement) { RequiredSort = Requirement; }

private:
	void SetLastPersistenceDebugSnapshot_RenderThread(const FFlightSwarmPersistenceDebugSnapshot& Snapshot);

	/** Persistent state on the GPU */
	TRefCountPtr<FRDGPooledBuffer> DroidStateBuffer;
	TRefCountPtr<FRDGPooledTexture> LatticeTexture;
	TRefCountPtr<FRDGPooledTexture> CloudTexture;
	mutable FCriticalSection PersistenceSnapshotMutex;
	FFlightSwarmPersistenceDebugSnapshot LastPersistenceSnapshot;
	int32 TotalEntities = 0;

	/** Current sort requirement for the next tick */
	ESwarmSortRequirement RequiredSort = ESwarmSortRequirement::Full;

	/** View extension for custom RDG rendering */
	TSharedPtr<Flight::Swarm::FSwarmSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;

	/** Packs current reactive values into a Command UBO */
	FSwarmGlobalCommand GetPackedCommand(float DeltaTime);
};
