// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU perception subsystem with io_uring async completion

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RenderGraphResources.h"
#include "FlightGpuPerceptionSubsystem.generated.h"

class UFlightGpuIoUringBridge;

/**
 * Perception request - submitted to GPU, results arrive async
 */
USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightPerceptionRequest
{
	GENERATED_BODY()

	FFlightPerceptionRequest() {}

	/** Unique ID for this request */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	int64 RequestId = 0;

	/** Entity positions to scan from (xyz = pos, w = radius) */
	UPROPERTY(BlueprintReadWrite, Category = "Flight|Perception")
	TArray<FVector4f> EntityPositions;

	/** Obstacle bounds for intersection testing */
	UPROPERTY(BlueprintReadWrite, Category = "Flight|Perception")
	TArray<FVector4f> ObstacleMinBounds;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Perception")
	TArray<FVector4f> ObstacleMaxBounds;

	/** When the request was submitted */
	double SubmitTime = 0.0;
};

/**
 * Perception result - delivered via callback when GPU completes
 */
USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightPerceptionResult
{
	GENERATED_BODY()

	FFlightPerceptionResult() {}

	/** Matching request ID */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	int64 RequestId = 0;

	/** GPU execution time in milliseconds */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	float GpuTimeMs = 0.0f;

	/** GPU Timestamp (Frame Index correlation) */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	int64 GpuTimestamp = 0;

	/** Per-entity obstacle counts */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	TArray<int32> ObstacleCounts;

	/** Per-entity accumulated forces from GPU Blackboard */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	TArray<FVector4f> AccumulatedForces;

	/** True if results are valid */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	bool bSuccess = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPerceptionResultReady, const FFlightPerceptionResult&, Result);

/**
 * UFlightGpuPerceptionSubsystem
 */
UCLASS()
class FLIGHTPROJECT_API UFlightGpuPerceptionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * Submit a perception request to the GPU (C++ API with callback).
	 */
	int64 SubmitPerceptionRequest(
		const FFlightPerceptionRequest& Request,
		TFunction<void(const FFlightPerceptionResult&)> Callback);

	/**
	 * Blueprint-friendly version using delegate
	 */
	UFUNCTION(BlueprintCallable, Category = "Flight|Perception", meta=(DisplayName="Submit Perception Request"))
	void SubmitPerceptionRequestBP(const FFlightPerceptionRequest& Request);

	/** Fired when any perception result is ready (for Blueprint) */
	UPROPERTY(BlueprintAssignable, Category = "Flight|Perception")
	FOnPerceptionResultReady OnPerceptionResultReady;

	/** Check if the subsystem is ready (GPU bridge + shaders available) */
	UFUNCTION(BlueprintCallable, Category = "Flight|Perception")
	bool IsAvailable() const;

	/** Get pending request count */
	UFUNCTION(BlueprintCallable, Category = "Flight|Perception")
	int32 GetPendingCount() const { return PendingRequests.Num(); }

	/** Statistics */
	UFUNCTION(BlueprintCallable, Category = "Flight|Perception")
	int64 GetTotalRequestsSubmitted() const { return TotalRequestsSubmitted; }

	UFUNCTION(BlueprintCallable, Category = "Flight|Perception")
	int64 GetTotalRequestsCompleted() const { return TotalRequestsCompleted; }

	UFUNCTION(BlueprintCallable, Category = "Flight|Perception")
	float GetAverageGpuTimeMs() const;

	/** Run a quick test with synthetic data */
	UFUNCTION(BlueprintCallable, Category = "Flight|Perception")
	void RunTestQuery(int32 NumEntities = 100, int32 NumObstacles = 50);

private:
	struct FPendingPerceptionRequest
	{
		int64 RequestId;
		FFlightPerceptionRequest Request;
		TFunction<void(const FFlightPerceptionResult&)> Callback;
	};

	TMap<int64, TSharedPtr<FPendingPerceptionRequest>> PendingRequests;
	FCriticalSection PendingRequestsMutex;

	int64 NextRequestId = 1;
	int64 TotalRequestsSubmitted = 0;
	int64 TotalRequestsCompleted = 0;
	double TotalGpuTimeMs = 0.0;

	UPROPERTY()
	UFlightGpuIoUringBridge* GpuBridge = nullptr;

	void DispatchOnRenderThread(TSharedPtr<FPendingPerceptionRequest> Request);

public:
	/** Internal use only: Handles completion with buffer data */
	void OnGpuWorkCompleteEx(
		int64 RequestId, 
		double SubmitTime,
		TRefCountPtr<FRDGPooledBuffer> PooledOutput,
		TRefCountPtr<FRDGPooledBuffer> PooledForce,
		TRefCountPtr<FRDGPooledBuffer> PooledTime);
};
