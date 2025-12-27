// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Test compute shader for io_uring integration

#include "IoUring/FlightTestComputeShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "RHIGPUReadback.h"

#if PLATFORM_LINUX
#include <sys/eventfd.h>
#include <unistd.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightTestCompute, Log, All);

// ============================================================================
// Shader Implementations
// ============================================================================

// NOTE: Shader registration disabled because game modules load too late in the
// engine startup sequence. Shaders need to be registered during PostConfigInit
// phase. To enable these shaders, move them to a plugin with:
//   "LoadingPhase": "PostConfigInit"
// or to a dedicated renderer module.
//
// See: https://docs.unrealengine.com/5.0/en-US/API/Runtime/RenderCore/FGlobalShaderType/
// Error: "Shader type was loaded too late, use ELoadingPhase::PostConfigInit"

#if 0 // DISABLED - Shaders cannot be registered from game modules (too late in loading)
IMPLEMENT_GLOBAL_SHADER(FFlightTestComputeShader,
	"/FlightProject/Private/FlightTestCompute.usf",
	"TestComputeMain",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightReductionComputeShader,
	"/FlightProject/Private/FlightTestCompute.usf",
	"ReductionComputeMain",
	SF_Compute);
#endif

// ============================================================================
// Dispatch Function
// ============================================================================

#if 0 // DISABLED - Shaders not available from game modules
FRDGBufferRef DispatchFlightTestCompute(
	FRDGBuilder& GraphBuilder,
	uint32 BufferSize,
	uint32 TestValue)
{
	// Create output buffer
	FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), BufferSize);
	FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("FlightTestOutput"));

	// Get shader
	TShaderMapRef<FFlightTestComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	// Setup parameters
	FFlightTestComputeShader::FParameters* Parameters = GraphBuilder.AllocParameters<FFlightTestComputeShader::FParameters>();
	Parameters->OutputBuffer = GraphBuilder.CreateUAV(OutputBuffer);
	Parameters->TestValue = TestValue;
	Parameters->BufferSize = BufferSize;

	// Calculate dispatch size
	uint32 ThreadGroupCount = FMath::DivideAndRoundUp(BufferSize, 64u);

	// Add compute pass
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("FlightTestCompute"),
		ComputeShader,
		Parameters,
		FIntVector(ThreadGroupCount, 1, 1));

	return OutputBuffer;
}
#endif

// ============================================================================
// GPU Completion Tracker
// ============================================================================

FFlightGpuCompletionTracker::FFlightGpuCompletionTracker()
{
#if PLATFORM_LINUX
	// Create eventfd for io_uring integration
	EventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (EventFd < 0)
	{
		UE_LOG(LogFlightTestCompute, Warning,
			TEXT("Failed to create eventfd for GPU completion tracking"));
	}
#endif
}

FFlightGpuCompletionTracker::~FFlightGpuCompletionTracker()
{
#if PLATFORM_LINUX
	if (EventFd >= 0)
	{
		close(EventFd);
		EventFd = -1;
	}
#endif
}

uint64 FFlightGpuCompletionTracker::TrackFence(FRHIGPUFence* FenceRHI, TFunction<void()> Callback)
{
	FScopeLock ScopeLock(&Lock);

	FTrackedFence Entry;
	Entry.Id = NextId++;
	Entry.Fence = FenceRHI;
	Entry.Callback = MoveTemp(Callback);
	Entry.StartTime = FPlatformTime::Seconds();

	TrackedFences.Add(MoveTemp(Entry));

	UE_LOG(LogFlightTestCompute, Verbose,
		TEXT("Tracking GPU fence %llu"), Entry.Id);

	return Entry.Id;
}

void FFlightGpuCompletionTracker::CancelTracking(uint64 TrackingId)
{
	FScopeLock ScopeLock(&Lock);

	TrackedFences.RemoveAll([TrackingId](const FTrackedFence& Entry)
	{
		return Entry.Id == TrackingId;
	});
}

int32 FFlightGpuCompletionTracker::Poll()
{
	FScopeLock ScopeLock(&Lock);

	int32 CompletedCount = 0;
	TArray<TFunction<void()>> CallbacksToInvoke;

	// Check each tracked fence
	for (int32 i = TrackedFences.Num() - 1; i >= 0; --i)
	{
		FTrackedFence& Entry = TrackedFences[i];

		if (Entry.Fence.IsValid() && Entry.Fence->Poll())
		{
			double ElapsedMs = (FPlatformTime::Seconds() - Entry.StartTime) * 1000.0;
			UE_LOG(LogFlightTestCompute, Verbose,
				TEXT("GPU fence %llu completed in %.2f ms"), Entry.Id, ElapsedMs);

			CallbacksToInvoke.Add(MoveTemp(Entry.Callback));
			TrackedFences.RemoveAt(i);
			++CompletedCount;
		}
	}

	// Signal eventfd if any fences completed
	if (CompletedCount > 0)
	{
		SignalEventFd();
	}

	// Invoke callbacks outside the lock
	Lock.Unlock();
	for (auto& Callback : CallbacksToInvoke)
	{
		Callback();
	}
	Lock.Lock();

	return CompletedCount;
}

void FFlightGpuCompletionTracker::SignalEventFd()
{
#if PLATFORM_LINUX
	if (EventFd >= 0)
	{
		uint64 Value = 1;
		ssize_t Written = write(EventFd, &Value, sizeof(Value));
		if (Written != sizeof(Value))
		{
			UE_LOG(LogFlightTestCompute, Warning,
				TEXT("Failed to signal eventfd"));
		}
	}
#endif
}
