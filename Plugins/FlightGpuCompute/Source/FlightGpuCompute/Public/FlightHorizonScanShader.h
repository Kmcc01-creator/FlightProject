// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU horizon scan shader for swarm perception

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#if WITH_FLIGHT_COMPUTE_SHADERS

/**
 * FFlightHorizonScanShader
 *
 * GPU compute shader for radial obstacle detection.
 * Each entity casts rays in a spherical pattern and records hit distances.
 */
class FLIGHTGPUCOMPUTE_API FFlightHorizonScanShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFlightHorizonScanShader);
	SHADER_USE_PARAMETER_STRUCT(FFlightHorizonScanShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, EntityPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ObstacleMinBounds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ObstacleMaxBounds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FUintVector4>, ScanResults)
		SHADER_PARAMETER(uint32, NumEntities)
		SHADER_PARAMETER(uint32, NumObstacles)
		SHADER_PARAMETER(uint32, NumRaysPerEntity)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) || 
               IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}
};

/**
 * FFlightObstacleCountShader
 *
 * Simpler shader that just counts obstacles within scan radius.
 * Useful for quick proximity queries.
 */
class FLIGHTGPUCOMPUTE_API FFlightObstacleCountShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFlightObstacleCountShader);
	SHADER_USE_PARAMETER_STRUCT(FFlightObstacleCountShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, EntityPositions)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ObstacleMinBounds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector4f>, ObstacleMaxBounds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, ObstacleCountResults)
		SHADER_PARAMETER(uint32, NumEntities)
		SHADER_PARAMETER(uint32, NumObstacles)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) || 
               IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}
};

/**
 * Horizon scan input data
 */
struct FLIGHTGPUCOMPUTE_API FFlightHorizonScanInput
{
	TArray<FVector4f> EntityPositions;  // xyz = position, w = scan radius
	TArray<FVector4f> ObstacleMinBounds;
	TArray<FVector4f> ObstacleMaxBounds;
	uint32 NumRaysPerEntity = 32;
};

/**
 * Horizon scan results (returned via callback)
 */
struct FLIGHTGPUCOMPUTE_API FFlightHorizonScanResults
{
	int64 TrackingId = 0;
	double GpuTimeMs = 0.0;
	TArray<uint32> ObstacleCounts;  // Per-entity obstacle count
	// Full scan results would be unpacked from GPU buffer here
};

/**
 * Dispatch obstacle count compute shader via RDG.
 * Returns immediately - use io_uring bridge for async completion notification.
 *
 * @param GraphBuilder RDG builder
 * @param Input Scan parameters and entity data
 * @return RDG buffer containing per-entity obstacle counts
 */
FLIGHTGPUCOMPUTE_API FRDGBufferRef DispatchFlightObstacleCount(
	FRDGBuilder& GraphBuilder,
	const FFlightHorizonScanInput& Input);

/**
 * Check if horizon scan shaders are ready
 */
FLIGHTGPUCOMPUTE_API bool AreFlightHorizonScanShadersReady();

#endif // WITH_FLIGHT_COMPUTE_SHADERS
