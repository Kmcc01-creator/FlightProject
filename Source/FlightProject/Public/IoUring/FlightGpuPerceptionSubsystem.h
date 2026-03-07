// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU perception subsystem with io_uring async completion
//
// Demonstrates the recommended pattern for GPU compute with io_uring notification:
// 1. Build RDG graph with compute dispatch
// 2. Execute graph
// 3. Call SignalGpuCompletion() to get async notification
// 4. Process results in callback (no polling needed!)

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightGpuPerceptionSubsystem.generated.h"

class UFlightGpuIoUringBridge;

/**
 * Perception request - submitted to GPU, results arrive async
 */
USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightPerceptionRequest
{
	GENERATED_BODY()

	/** Unique ID for this request */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	int64 RequestId = 0;

	/** Entity positions to scan from (xyz = pos, w = radius) */
	TArray<FVector4f> EntityPositions;

	/** Obstacle bounds for intersection testing */
	TArray<FVector4f> ObstacleMinBounds;
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

	/** Matching request ID */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	int64 RequestId = 0;

	/** GPU execution time in milliseconds */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	float GpuTimeMs = 0.0f;

	/** Per-entity obstacle counts */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	TArray<int32> ObstacleCounts;

	/** True if results are valid */
	UPROPERTY(BlueprintReadOnly, Category = "Flight|Perception")
	bool bSuccess = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPerceptionResultReady, const FFlightPerceptionResult&, Result);

/**
 * UFlightGpuPerceptionSubsystem
 *
 * Manages GPU-accelerated perception queries with async io_uring completion.
 *
 * Usage Pattern:
 * ```cpp
 * // 1. Get subsystem
 * auto* Perception = GetWorld()->GetSubsystem<UFlightGpuPerceptionSubsystem>();
 *
 * // 2. Submit request
 * FFlightPerceptionRequest Request;
 * Request.EntityPositions = GetSwarmPositions();
 * Request.ObstacleMinBounds = GetObstacleBounds();
 * // ...
 *
 * int64 RequestId = Perception->SubmitPerceptionRequest(Request,
 *     [this](const FFlightPerceptionResult& Result)
 *     {
 *         // This fires when GPU work completes - zero polling!
 *         ProcessPerceptionResults(Result);
 *     });
 *
 * // 3. That's it! Callback fires automatically via io_uring
 * ```
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
	 * Results delivered via callback when GPU work completes.
	 *
	 * @param Request The perception query parameters
	 * @param Callback Called on game thread when results are ready
	 * @return Request ID for tracking (0 if submission failed)
	 */
	int64 SubmitPerceptionRequest(
		const FFlightPerceptionRequest& Request,
		TFunction<void(const FFlightPerceptionResult&)> Callback);

	/**
	 * Blueprint-friendly version using delegate
	 */
	UFUNCTION(BlueprintCallable, Category = "Flight|Perception")
	int64 SubmitPerceptionRequestBP(const FFlightPerceptionRequest& Request);

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
		// Readback data would be stored here
	};

	TMap<int64, TSharedPtr<FPendingPerceptionRequest>> PendingRequests;
	FCriticalSection PendingRequestsMutex;

	int64 NextRequestId = 1;
	int64 TotalRequestsSubmitted = 0;
	int64 TotalRequestsCompleted = 0;
	double TotalGpuTimeMs = 0.0;

	UPROPERTY()
	UFlightGpuIoUringBridge* GpuBridge = nullptr;

	void OnGpuWorkComplete(int64 RequestId, double SubmitTime);
	void DispatchOnRenderThread(TSharedPtr<FPendingPerceptionRequest> Request);
};
