// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightSpatialField.h"

class UFlightGpuPerceptionSubsystem;

namespace Flight::Spatial
{

/**
 * FGpuPerceptionField
 * 
 * Bridges the UFlightGpuPerceptionSubsystem (RDG/Compute) into the IFlightSpatialField interface.
 * Primarily handles asynchronous obstacle perception queries.
 */
class FLIGHTPROJECT_API FGpuPerceptionField : public IFlightSpatialField
{
public:
	FGpuPerceptionField(UWorld* InWorld);
	virtual ~FGpuPerceptionField() override = default;

	// IFlightSpatialField interface
	virtual FName GetFieldName() const override { return TEXT("GpuPerception"); }
	virtual ESpatialFieldType GetFieldType() const override { return ESpatialFieldType::Occlusion; }

	/** Synchronous sampling is not supported for GPU-driven perception */
	virtual FVector SampleSync(const FVector& Position) const override { return FVector::ZeroVector; }

	/** 
	 * Asynchronous sample path.
	 * Dispatches a perception request to the GPU subsystem.
	 * 
	 * @note For high performance with many entities, use the Mass Processor batching path instead.
	 */
	virtual void SampleAsync(const FVector& Position, TFunction<void(const FVector&)> Callback) override;

private:
	TWeakObjectPtr<UFlightGpuPerceptionSubsystem> PerceptionSubsystem;
};

} // namespace Flight::Spatial
