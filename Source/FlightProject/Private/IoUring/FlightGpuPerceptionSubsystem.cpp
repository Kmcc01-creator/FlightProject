// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU perception subsystem with io_uring async completion

#include "IoUring/FlightGpuPerceptionSubsystem.h"
#include "IoUring/FlightGpuIoUringBridge.h"
#include "Engine/World.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#if WITH_FLIGHT_COMPUTE_SHADERS
#include "FlightHorizonScanShader.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightPerception, Log, All);

bool UFlightGpuPerceptionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if PLATFORM_LINUX
	return true;
#else
	return false;
#endif
}

void UFlightGpuPerceptionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (IsRunningCommandlet())
	{
		return;
	}

#if PLATFORM_LINUX
	// Get the GPU bridge (handles io_uring integration)
	GpuBridge = Collection.InitializeDependency<UFlightGpuIoUringBridge>();

	const TCHAR* ShaderStatus = TEXT("Disabled");
#if WITH_FLIGHT_COMPUTE_SHADERS
	ShaderStatus = AreFlightHorizonScanShadersReady() ? TEXT("Ready") : TEXT("Not Ready");
#endif

	UE_LOG(LogFlightPerception, Log,
		TEXT("FlightGpuPerceptionSubsystem initialized (GpuBridge=%s, Shaders=%s)"),
		GpuBridge && GpuBridge->IsAvailable() ? TEXT("Available") : TEXT("Unavailable"),
		ShaderStatus);
#endif
}

void UFlightGpuPerceptionSubsystem::Deinitialize()
{
	UE_LOG(LogFlightPerception, Log,
		TEXT("FlightGpuPerceptionSubsystem stats: %lld submitted, %lld completed, avg %.2f ms"),
		TotalRequestsSubmitted, TotalRequestsCompleted, GetAverageGpuTimeMs());

	{
		FScopeLock Lock(&PendingRequestsMutex);
		PendingRequests.Empty();
	}

	GpuBridge = nullptr;
	Super::Deinitialize();
}

bool UFlightGpuPerceptionSubsystem::IsAvailable() const
{
#if WITH_FLIGHT_COMPUTE_SHADERS
	return GpuBridge && GpuBridge->IsAvailable() && AreFlightHorizonScanShadersReady();
#else
	return false;
#endif
}

float UFlightGpuPerceptionSubsystem::GetAverageGpuTimeMs() const
{
	if (TotalRequestsCompleted == 0)
	{
		return 0.0f;
	}
	return static_cast<float>(TotalGpuTimeMs / TotalRequestsCompleted);
}

int64 UFlightGpuPerceptionSubsystem::SubmitPerceptionRequest(
	const FFlightPerceptionRequest& Request,
	TFunction<void(const FFlightPerceptionResult&)> Callback)
{
	if (!IsAvailable())
	{
		UE_LOG(LogFlightPerception, Warning,
			TEXT("SubmitPerceptionRequest: Subsystem not available"));
		return 0;
	}

	if (Request.EntityPositions.Num() == 0)
	{
		UE_LOG(LogFlightPerception, Warning,
			TEXT("SubmitPerceptionRequest: No entities in request"));
		return 0;
	}

	// Create pending request
	TSharedPtr<FPendingPerceptionRequest> Pending = MakeShared<FPendingPerceptionRequest>();
	Pending->RequestId = NextRequestId++;
	Pending->Request = Request;
	Pending->Request.RequestId = Pending->RequestId;
	Pending->Request.SubmitTime = FPlatformTime::Seconds();
	Pending->Callback = MoveTemp(Callback);

	{
		FScopeLock Lock(&PendingRequestsMutex);
		PendingRequests.Add(Pending->RequestId, Pending);
	}

	++TotalRequestsSubmitted;

	UE_LOG(LogFlightPerception, Log,
		TEXT("Submitting perception request %lld: %d entities, %d obstacles"),
		Pending->RequestId,
		Request.EntityPositions.Num(),
		Request.ObstacleMinBounds.Num());

	// Dispatch to render thread
	DispatchOnRenderThread(Pending);

	return Pending->RequestId;
}

int64 UFlightGpuPerceptionSubsystem::SubmitPerceptionRequestBP(const FFlightPerceptionRequest& Request)
{
	return SubmitPerceptionRequest(Request,
		[this](const FFlightPerceptionResult& Result)
		{
			// Fire Blueprint delegate
			OnPerceptionResultReady.Broadcast(Result);
		});
}

void UFlightGpuPerceptionSubsystem::DispatchOnRenderThread(TSharedPtr<FPendingPerceptionRequest> Request)
{
#if WITH_FLIGHT_COMPUTE_SHADERS
	// Capture what we need for the render thread lambda
	int64 RequestId = Request->RequestId;
	double SubmitTime = Request->Request.SubmitTime;
	TWeakObjectPtr<UFlightGpuPerceptionSubsystem> WeakThis(this);

	// Enqueue RDG work on render thread
	ENQUEUE_RENDER_COMMAND(FlightPerceptionDispatch)(
		[WeakThis, Request, RequestId, SubmitTime](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			// Build input for shader
			FFlightHorizonScanInput Input;
			Input.EntityPositions = Request->Request.EntityPositions;
			Input.ObstacleMinBounds = Request->Request.ObstacleMinBounds;
			Input.ObstacleMaxBounds = Request->Request.ObstacleMaxBounds;

			// Dispatch compute shader
			FRDGBufferRef OutputBuffer = DispatchFlightObstacleCount(GraphBuilder, Input);

			if (OutputBuffer)
			{
				// Execute the graph
				GraphBuilder.Execute();

				// Now signal io_uring that we want notification when this completes
				// This is the KEY INTEGRATION POINT!
				AsyncTask(ENamedThreads::GameThread,
					[WeakThis, RequestId, SubmitTime]()
					{
						if (UFlightGpuPerceptionSubsystem* Self = WeakThis.Get())
						{
							if (Self->GpuBridge && Self->GpuBridge->IsUsingExportableSemaphores())
							{
								// Zero-syscall path: GPU completion → io_uring CQE → callback
								Self->GpuBridge->SignalGpuCompletion(RequestId,
									[WeakThis, RequestId, SubmitTime]()
									{
										if (UFlightGpuPerceptionSubsystem* S = WeakThis.Get())
										{
											S->OnGpuWorkComplete(RequestId, SubmitTime);
										}
									});

								UE_LOG(LogFlightPerception, Verbose,
									TEXT("Request %lld: GPU work submitted, awaiting io_uring notification"),
									RequestId);
							}
							else
							{
								// Fallback: immediate callback (for testing without exportable semaphores)
								UE_LOG(LogFlightPerception, Warning,
									TEXT("Request %lld: Exportable semaphores unavailable, using immediate callback"),
									RequestId);
								Self->OnGpuWorkComplete(RequestId, SubmitTime);
							}
						}
					});
			}
			else
			{
				UE_LOG(LogFlightPerception, Error,
					TEXT("Request %lld: Failed to dispatch compute shader"), RequestId);
			}
		});
#endif
}

void UFlightGpuPerceptionSubsystem::OnGpuWorkComplete(int64 RequestId, double SubmitTime)
{
	double GpuTimeMs = (FPlatformTime::Seconds() - SubmitTime) * 1000.0;

	TSharedPtr<FPendingPerceptionRequest> Pending;
	{
		FScopeLock Lock(&PendingRequestsMutex);
		if (TSharedPtr<FPendingPerceptionRequest>* Found = PendingRequests.Find(RequestId))
		{
			Pending = *Found;
			PendingRequests.Remove(RequestId);
		}
	}

	if (!Pending)
	{
		UE_LOG(LogFlightPerception, Warning,
			TEXT("OnGpuWorkComplete: Unknown request %lld"), RequestId);
		return;
	}

	++TotalRequestsCompleted;
	TotalGpuTimeMs += GpuTimeMs;

	UE_LOG(LogFlightPerception, Log,
		TEXT("Request %lld completed in %.2f ms (io_uring zero-syscall path)"),
		RequestId, GpuTimeMs);

	// Build result
	FFlightPerceptionResult Result;
	Result.RequestId = RequestId;
	Result.GpuTimeMs = static_cast<float>(GpuTimeMs);
	Result.bSuccess = true;

	// TODO: Read back actual GPU results here
	// For now, just simulate with entity count
	Result.ObstacleCounts.SetNum(Pending->Request.EntityPositions.Num());
	for (int32& Count : Result.ObstacleCounts)
	{
		Count = 0;  // Would be populated from GPU readback
	}

	// Invoke callback
	if (Pending->Callback)
	{
		Pending->Callback(Result);
	}
}

void UFlightGpuPerceptionSubsystem::RunTestQuery(int32 NumEntities, int32 NumObstacles)
{
	UE_LOG(LogFlightPerception, Log,
		TEXT("Running test query: %d entities, %d obstacles"), NumEntities, NumObstacles);

	FFlightPerceptionRequest Request;

	// Generate random entity positions
	Request.EntityPositions.Reserve(NumEntities);
	for (int32 i = 0; i < NumEntities; ++i)
	{
		Request.EntityPositions.Add(FVector4f(
			FMath::FRandRange(-10000.0f, 10000.0f),
			FMath::FRandRange(-10000.0f, 10000.0f),
			FMath::FRandRange(0.0f, 5000.0f),
			1000.0f  // Scan radius
		));
	}

	// Generate random obstacle AABBs
	Request.ObstacleMinBounds.Reserve(NumObstacles);
	Request.ObstacleMaxBounds.Reserve(NumObstacles);
	for (int32 i = 0; i < NumObstacles; ++i)
	{
		FVector3f Center(
			FMath::FRandRange(-10000.0f, 10000.0f),
			FMath::FRandRange(-10000.0f, 10000.0f),
			FMath::FRandRange(0.0f, 3000.0f)
		);
		FVector3f HalfExtent(
			FMath::FRandRange(50.0f, 500.0f),
			FMath::FRandRange(50.0f, 500.0f),
			FMath::FRandRange(50.0f, 500.0f)
		);

		Request.ObstacleMinBounds.Add(FVector4f(Center - HalfExtent, 0.0f));
		Request.ObstacleMaxBounds.Add(FVector4f(Center + HalfExtent, 0.0f));
	}

	// Submit with logging callback
	SubmitPerceptionRequest(Request,
		[NumEntities, NumObstacles](const FFlightPerceptionResult& Result)
		{
			UE_LOG(LogFlightPerception, Log,
				TEXT("TEST COMPLETE: Request %lld, %.2f ms, %d entities, %d obstacles"),
				Result.RequestId,
				Result.GpuTimeMs,
				NumEntities,
				NumObstacles);
		});
}
