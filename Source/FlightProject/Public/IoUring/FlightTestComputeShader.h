// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Test compute shader for io_uring integration

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

// NOTE: Shader classes disabled because game modules load too late for shader
// registration. To enable, move to a plugin with LoadingPhase::PostConfigInit.
// See FlightTestComputeShader.cpp for details.

#if 0 // DISABLED - Shaders cannot be registered from game modules
/**
 * FFlightTestComputeShader
 *
 * Simple compute shader for testing GPU -> io_uring notification path.
 * Writes sequential values to a buffer that can be read back for verification.
 */
class FFlightTestComputeShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFlightTestComputeShader);
	SHADER_USE_PARAMETER_STRUCT(FFlightTestComputeShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputBuffer)
		SHADER_PARAMETER(uint32, TestValue)
		SHADER_PARAMETER(uint32, BufferSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 64);
	}
};

/**
 * FFlightReductionComputeShader
 *
 * Reduction shader for more realistic workload testing.
 * Sums input buffer and writes result to output.
 */
class FFlightReductionComputeShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFlightReductionComputeShader);
	SHADER_USE_PARAMETER_STRUCT(FFlightReductionComputeShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InputBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputBuffer)
		SHADER_PARAMETER(uint32, BufferSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/**
 * Dispatch test compute shader via RDG
 */
FLIGHTPROJECT_API FRDGBufferRef DispatchFlightTestCompute(
	FRDGBuilder& GraphBuilder,
	uint32 BufferSize,
	uint32 TestValue);
#endif

/**
 * FFlightGpuCompletionTracker
 *
 * Tracks GPU work completion and bridges to io_uring notification.
 * Uses a polling approach initially (can be optimized with fence export later).
 */
class FLIGHTPROJECT_API FFlightGpuCompletionTracker
{
public:
	FFlightGpuCompletionTracker();
	~FFlightGpuCompletionTracker();

	/**
	 * Track a GPU fence for completion
	 *
	 * @param FenceRHI The RHI fence to track
	 * @param Callback Called when fence signals (on game thread)
	 * @return Tracking ID for cancellation
	 */
	uint64 TrackFence(FRHIGPUFence* FenceRHI, TFunction<void()> Callback);

	/**
	 * Cancel fence tracking
	 */
	void CancelTracking(uint64 TrackingId);

	/**
	 * Poll tracked fences (call from game thread tick)
	 * @return Number of fences that signaled
	 */
	int32 Poll();

	/** Get eventfd for io_uring integration (signaled when any fence completes) */
	int32 GetEventFd() const { return EventFd; }

private:
	struct FTrackedFence
	{
		uint64 Id;
		FGPUFenceRHIRef Fence;
		TFunction<void()> Callback;
		double StartTime;
	};

	TArray<FTrackedFence> TrackedFences;
	uint64 NextId = 1;
	int32 EventFd = -1;
	FCriticalSection Lock;

	void SignalEventFd();
};
