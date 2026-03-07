// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightSpatialField - Generalized Spatial Data and Force Abstractions
//
// Defines the core interface for spatial queries (Wind, Obstacles, Gradients)
// and the Mass Entity fragments used to interact with them reactively.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightReactive.h"
#include "Core/FlightAsyncExecutor.h"

namespace Flight::Spatial
{

using namespace Flight::Functional;
using namespace Flight::Reactive;
using namespace Flight::Async;

/**
 * Types of spatial fields supported by the simulation.
 */
enum class ESpatialFieldType : uint8
{
	Force,      // Physical force (Wind, Gravity)
	Density,    // Scalar density (Obstacle cloud, pheromones)
	Gradient,   // Directional gradient (Pathfinding flow fields)
	Occlusion   // Binary/Scalar occlusion (Horizon scan)
};

/**
 * IFlightSpatialField
 * Universal interface for spatial data providers.
 */
class IFlightSpatialField
{
public:
	virtual ~IFlightSpatialField() = default;

	/** Unique name for identification and debugging */
	virtual FName GetFieldName() const = 0;

	/** Field category */
	virtual ESpatialFieldType GetFieldType() const = 0;

	/** 
	 * Synchronous CPU sample path.
	 * Ideal for procedural math-based fields (Wind, Simple Gravity).
	 */
	virtual FVector SampleSync(const FVector& Position) const = 0;

	/**
	 * Asynchronous/Reactive sample path.
	 * Ideal for GPU-driven or heavy lookups (Horizon Scan, Complex VDB).
	 * Triggers the callback when data is ready via the executor.
	 */
	virtual void SampleAsync(const FVector& Position, TFunction<void(const FVector&)> Callback) = 0;
};

// ============================================================================
// Mass Entity Integration
// ============================================================================

/**
 * FMassSpatialQueryFragment
 * 
 * Generalized fragment for entities requesting spatial data.
 * Used by processors to aggregate forces or query environment state.
 */
struct FMassSpatialQueryFragment : public FMassFragment
{
	/** 
	 * The current result of the query. 
	 * Reactive: Logic systems can bind to this to react to environmental changes.
	 */
	TObservableField<FVector> CurrentResult{FVector::ZeroVector};

	/** Identifier for tracking in-flight async requests */
	FAsyncHandle ActiveRequest;

	/** Bitmask to filter which spatial fields this entity is interested in */
	uint32 FieldLayerMask = 0x1;

	/** Last time this query was refreshed (for throttling) */
	float LastUpdateTime = 0.0f;

	FLIGHT_REFLECT_BODY(FMassSpatialQueryFragment);
};

} // namespace Flight::Spatial

// Specialization for the fragment reflection
namespace Flight::Reflection
{
	using namespace Flight::Spatial;
	using namespace Flight::Reflection::Attr;

	FLIGHT_REFLECT_FIELDS_ATTR(FMassSpatialQueryFragment,
		FLIGHT_FIELD_ATTR(TObservableField<FVector>, CurrentResult, VisibleAnywhere, Category<"Simulation">),
		FLIGHT_FIELD_ATTR(uint32, FieldLayerMask, EditAnywhere, Category<"Simulation">),
		FLIGHT_FIELD_ATTR(float, LastUpdateTime, VisibleAnywhere, Category<"Internal">, Transient)
	);
}
