// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Test compute shader for io_uring integration
//
// NOTE: This plugin must load at PostConfigInit phase for shader registration.
// See FlightGpuCompute.uplugin LoadingPhase setting.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#if WITH_FLIGHT_COMPUTE_SHADERS

/**
 * FFlightTestComputeShader
 *
 * Simple compute shader for testing GPU -> io_uring notification path.
 * Writes sequential values to a buffer that can be read back for verification.
 */
class FLIGHTGPUCOMPUTE_API FFlightTestComputeShader : public FGlobalShader
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
class FLIGHTGPUCOMPUTE_API FFlightReductionComputeShader : public FGlobalShader
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
 *
 * @param GraphBuilder RDG builder for the pass
 * @param BufferSize Number of uint32 elements in output buffer
 * @param TestValue Base value to write (element i gets TestValue + i)
 * @return RDG buffer reference containing the output
 */
FLIGHTGPUCOMPUTE_API FRDGBufferRef DispatchFlightTestCompute(
	FRDGBuilder& GraphBuilder,
	uint32 BufferSize,
	uint32 TestValue);

/**
 * Check if compute shaders are ready for use
 */
FLIGHTGPUCOMPUTE_API bool AreFlightComputeShadersReady();

#endif // WITH_FLIGHT_COMPUTE_SHADERS
