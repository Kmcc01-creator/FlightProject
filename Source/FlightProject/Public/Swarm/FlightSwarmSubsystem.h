// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Core/FlightReactive.h"
#include "RenderGraphResources.h"
#include "Tickable.h"
#include "FlightSwarmSubsystem.generated.h"

using namespace Flight::Reactive;
using namespace Flight::Swarm;

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

	/** Gameplay state */
	TReactiveValue<FVector> PlayerPosition{FVector::ZeroVector};
	TReactiveValue<FVector> BeaconPosition{FVector::ZeroVector};

	/** Get the persistent GPU buffer (Internal use for RDG) */
	TRefCountPtr<FRDGPooledBuffer> GetDroidStateBuffer() { return DroidStateBuffer; }
	int32 GetNumEntities() const { return TotalEntities; }

	/** Manual tick for simulation (used by automation tests) */
	void TickSimulation(float DeltaTime);

private:
	/** Persistent state on the GPU */
	TRefCountPtr<FRDGPooledBuffer> DroidStateBuffer;
	int32 TotalEntities = 0;

	/** Packs current reactive values into a Command UBO */
	FSwarmGlobalCommand GetPackedCommand(float DeltaTime);
};
