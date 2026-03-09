// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU perception subsystem with io_uring async completion

#include "IoUring/FlightGpuPerceptionSubsystem.h"
#include "IoUring/FlightGpuIoUringBridge.h"
#include "Engine/World.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Async/Async.h"
#include "DynamicRHI.h"
#include "RHIResources.h"
#include "Misc/App.h"

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

	if (!FApp::CanEverRender())
	{
		UE_LOG(LogFlightPerception, Log,
			TEXT("FlightGpuPerceptionSubsystem: Rendering disabled (NullRHI/headless). GPU paths disabled."));
		return;
	}

	// Capability-based initialization:
	if (GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
	{
		UE_LOG(LogFlightPerception, Warning, 
			TEXT("FlightGpuPerceptionSubsystem: Insufficient RHI Feature Level (%d). GPU paths disabled."), 
			(int32)GMaxRHIFeatureLevel);
		return;
	}

#if PLATFORM_LINUX
	// Get the GPU bridge (handles io_uring integration)
	GpuBridge = Collection.InitializeDependency<UFlightGpuIoUringBridge>();

	const TCHAR* ShaderStatus = TEXT("Disabled");
#if WITH_FLIGHT_COMPUTE_SHADERS
	ShaderStatus = (AreFlightHorizonScanShadersReady() && !IsRunningCommandlet()) ? TEXT("Ready") : TEXT("Not Ready");
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
	if (!FApp::CanEverRender() || IsRunningCommandlet())
	{
		return false;
	}

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

void UFlightGpuPerceptionSubsystem::CompletePendingRequestWithFailure(int64 RequestId, const TCHAR* Reason)
{
	TSharedPtr<FPendingPerceptionRequest> Pending;
	{
		FScopeLock Lock(&PendingRequestsMutex);
		PendingRequests.RemoveAndCopyValue(RequestId, Pending);
	}

	if (!Pending.IsValid())
	{
		return;
	}

	FFlightPerceptionResult FailedResult;
	FailedResult.RequestId = RequestId;
	FailedResult.bSuccess = false;
	FailedResult.GpuTimeMs = static_cast<float>((FPlatformTime::Seconds() - Pending->Request.SubmitTime) * 1000.0);

	TotalRequestsCompleted++;
	UE_LOG(LogFlightPerception, Warning,
		TEXT("GPU perception request %lld failed: %s"),
		RequestId,
		Reason ? Reason : TEXT("unknown"));

	if (Pending->Callback)
	{
		Pending->Callback(FailedResult);
	}
}

int64 UFlightGpuPerceptionSubsystem::SubmitPerceptionRequest(
	const FFlightPerceptionRequest& Request,
	TFunction<void(const FFlightPerceptionResult&)> Callback)
{
	int64 RequestId = ++NextRequestId;

	TSharedPtr<FPendingPerceptionRequest> Pending = MakeShared<FPendingPerceptionRequest>();
	Pending->RequestId = RequestId;
	Pending->Request = Request;
	Pending->Request.SubmitTime = FPlatformTime::Seconds();
	Pending->Callback = Callback;

	{
		FScopeLock Lock(&PendingRequestsMutex);
		PendingRequests.Add(RequestId, Pending);
	}

	TotalRequestsSubmitted++;

	// Kick off the work on the render thread
	DispatchOnRenderThread(Pending);

	return RequestId;
}

void UFlightGpuPerceptionSubsystem::SubmitPerceptionRequestBP(const FFlightPerceptionRequest& Request)
{
	SubmitPerceptionRequest(Request, [this](const FFlightPerceptionResult& Result)
	{
		OnPerceptionResultReady.Broadcast(Result);
	});
}

void UFlightGpuPerceptionSubsystem::DispatchOnRenderThread(TSharedPtr<FPendingPerceptionRequest> Request)
{
#if WITH_FLIGHT_COMPUTE_SHADERS
	int64 RequestId = Request->RequestId;
	double SubmitTime = Request->Request.SubmitTime;
	TWeakObjectPtr<UFlightGpuPerceptionSubsystem> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(FlightPerceptionDispatch)(
		[WeakThis, Request, RequestId, SubmitTime](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			const uint32 NumEntities = Request->Request.EntityPositions.Num();

			// Allocate Results Buffers in RDG
			FRDGBufferDesc CountDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumEntities);
			FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(CountDesc, TEXT("FlightScan.ObstacleCounts"));

			FRDGBufferDesc ForceDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), NumEntities);
			FRDGBufferRef ForceBuffer = GraphBuilder.CreateBuffer(ForceDesc, TEXT("FlightScan.ForceBlackboard"));

			FRDGBufferDesc TimeDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FUintVector2), 1);
			FRDGBufferRef TimeBuffer = GraphBuilder.CreateBuffer(TimeDesc, TEXT("FlightScan.Timestamp"));

			// Build input for shader
			FFlightHorizonScanInput Input;
			Input.EntityPositions = Request->Request.EntityPositions;
			Input.ObstacleMinBounds = Request->Request.ObstacleMinBounds;
			Input.ObstacleMaxBounds = Request->Request.ObstacleMaxBounds;

			// Dispatch compute shader
			uint32 FrameIndex = static_cast<uint32>(GFrameCounter);
			DispatchFlightObstacleCount(GraphBuilder, Input, OutputBuffer, ForceBuffer, TimeBuffer, FrameIndex);

			// Extract pooled buffers
			TRefCountPtr<FRDGPooledBuffer> PooledOutput;
			GraphBuilder.QueueBufferExtraction(OutputBuffer, &PooledOutput);

			TRefCountPtr<FRDGPooledBuffer> PooledForce;
			GraphBuilder.QueueBufferExtraction(ForceBuffer, &PooledForce);

			TRefCountPtr<FRDGPooledBuffer> PooledTime;
			GraphBuilder.QueueBufferExtraction(TimeBuffer, &PooledTime);

			GraphBuilder.Execute();

			// Readback Capture
			struct FReadbackCapture
			{
				TRefCountPtr<FRDGPooledBuffer> PooledOutput;
				TRefCountPtr<FRDGPooledBuffer> PooledForce;
				TRefCountPtr<FRDGPooledBuffer> PooledTime;
			};
			TSharedPtr<FReadbackCapture> Capture = MakeShared<FReadbackCapture>();
			Capture->PooledOutput = PooledOutput;
			Capture->PooledForce = PooledForce;
			Capture->PooledTime = PooledTime;

			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, RequestId, SubmitTime, Capture]()
				{
					if (UFlightGpuPerceptionSubsystem* Self = WeakThis.Get())
					{
						if (!Self->GpuBridge || !Self->GpuBridge->IsAvailable())
						{
							Self->CompletePendingRequestWithFailure(RequestId, TEXT("GPU bridge unavailable"));
							return;
						}

						const bool bSubmitted = Self->GpuBridge->SignalGpuCompletion(
							RequestId,
							[WeakThis, RequestId, SubmitTime, Capture]()
							{
								if (UFlightGpuPerceptionSubsystem* SubSelf = WeakThis.Get())
								{
									SubSelf->OnGpuWorkCompleteEx(RequestId, SubmitTime,
										Capture->PooledOutput, Capture->PooledForce, Capture->PooledTime);
								}
							},
							[WeakThis, RequestId]()
							{
								if (UFlightGpuPerceptionSubsystem* SubSelf = WeakThis.Get())
								{
									SubSelf->CompletePendingRequestWithFailure(RequestId, TEXT("GPU completion signal setup failed"));
								}
							});

						if (!bSubmitted)
						{
							Self->CompletePendingRequestWithFailure(RequestId, TEXT("GPU completion signal rejected"));
						}
					}
				});
		});
#endif
}

void UFlightGpuPerceptionSubsystem::OnGpuWorkCompleteEx(
	int64 RequestId, 
	double SubmitTime,
	TRefCountPtr<FRDGPooledBuffer> PooledOutput,
	TRefCountPtr<FRDGPooledBuffer> PooledForce,
	TRefCountPtr<FRDGPooledBuffer> PooledTime)
{
	TSharedPtr<FPendingPerceptionRequest> Pending;
	{
		FScopeLock Lock(&PendingRequestsMutex);
		PendingRequests.RemoveAndCopyValue(RequestId, Pending);
	}

	if (!Pending.IsValid()) return;

	TWeakObjectPtr<UFlightGpuPerceptionSubsystem> WeakThis(this);
	const uint32 NumEntities = Pending->Request.EntityPositions.Num();

	// FIX: Access RHI buffers ONLY on the Rendering Thread!
	ENQUEUE_RENDER_COMMAND(FlightPerceptionReadback)(
		[WeakThis, RequestId, SubmitTime, PooledOutput, PooledForce, PooledTime, NumEntities, Pending](FRHICommandListImmediate& RHICmdList)
		{
			FFlightPerceptionResult Result;
			Result.RequestId = RequestId;
			Result.bSuccess = PooledOutput.IsValid();
			Result.GpuTimeMs = (float)((FPlatformTime::Seconds() - SubmitTime) * 1000.0);

			// Buffer Readback (Render Thread safe)
			if (PooledOutput.IsValid())
			{
				Result.ObstacleCounts.SetNumUninitialized(NumEntities);
				void* Data = GDynamicRHI->RHILockBuffer(RHICmdList, PooledOutput->GetRHI(), 0, NumEntities * sizeof(uint32), RLM_ReadOnly);
				FMemory::Memcpy(Result.ObstacleCounts.GetData(), Data, NumEntities * sizeof(uint32));
				GDynamicRHI->RHIUnlockBuffer(RHICmdList, PooledOutput->GetRHI());
			}
			else
			{
				UE_LOG(LogFlightPerception, Warning,
					TEXT("Perception readback missing obstacle output buffer for request %lld"), RequestId);
			}

			if (PooledForce.IsValid())
			{
				Result.AccumulatedForces.SetNumUninitialized(NumEntities);
				void* Data = GDynamicRHI->RHILockBuffer(RHICmdList, PooledForce->GetRHI(), 0, NumEntities * sizeof(FVector4f), RLM_ReadOnly);
				FMemory::Memcpy(Result.AccumulatedForces.GetData(), Data, NumEntities * sizeof(FVector4f));
				GDynamicRHI->RHIUnlockBuffer(RHICmdList, PooledForce->GetRHI());
			}

			if (PooledTime.IsValid())
			{
				FUintVector2 GpuTimeData;
				void* Data = GDynamicRHI->RHILockBuffer(RHICmdList, PooledTime->GetRHI(), 0, sizeof(FUintVector2), RLM_ReadOnly);
				FMemory::Memcpy(&GpuTimeData, Data, sizeof(FUintVector2));
				GDynamicRHI->RHIUnlockBuffer(RHICmdList, PooledTime->GetRHI());
				Result.GpuTimestamp = (int64)GpuTimeData.X;
			}

			// Final callback MUST be on Game Thread
			AsyncTask(ENamedThreads::GameThread, [WeakThis, Result, Pending]()
			{
				if (UFlightGpuPerceptionSubsystem* Self = WeakThis.Get())
				{
					Self->TotalRequestsCompleted++;
					Self->TotalGpuTimeMs += Result.GpuTimeMs;

					UE_LOG(LogFlightPerception, Log, 
						TEXT("GPU Work Complete: Request %lld, %.2f ms, Frame %lld"), 
						Result.RequestId, Result.GpuTimeMs, Result.GpuTimestamp);

					if (Pending->Callback)
					{
						Pending->Callback(Result);
					}
				}
			});
		});
}

void UFlightGpuPerceptionSubsystem::RunTestQuery(int32 NumEntities, int32 NumObstacles)
{
	FFlightPerceptionRequest Request;
	Request.EntityPositions.SetNum(NumEntities);
	for (int32 i = 0; i < NumEntities; ++i)
	{
		Request.EntityPositions[i] = FVector4f(i * 100.0f, 0, 0, 1000.0f);
	}

	Request.ObstacleMinBounds.SetNum(NumObstacles);
	Request.ObstacleMaxBounds.SetNum(NumObstacles);
	for (int32 i = 0; i < NumObstacles; ++i)
	{
		Request.ObstacleMinBounds[i] = FVector4f(i * 200.0f - 50.0f, -50.0f, -50.0f, 0);
		Request.ObstacleMaxBounds[i] = FVector4f(i * 200.0f + 50.0f, 50.0f, 50.0f, 0);
	}

	SubmitPerceptionRequest(Request, [NumEntities, NumObstacles](const FFlightPerceptionResult& Result)
	{
		UE_LOG(LogFlightPerception, Log,
			TEXT("TEST COMPLETE: Request %lld, %.2f ms, %d entities, %d obstacles, GpuFrame %lld"),
			Result.RequestId,
			Result.GpuTimeMs,
			NumEntities,
			NumObstacles,
			Result.GpuTimestamp);
	});
}
