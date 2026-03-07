// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

#if WITH_FLIGHT_COMPUTE_SHADERS

/** Pass 0a: Clear Grid Head Buffer */
class FLIGHTGPUCOMPUTE_API FFlightSwarmClearGridCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmClearGridCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmClearGridCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, GridHeadBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 0b: Build Spatial Linked List */
class FLIGHTGPUCOMPUTE_API FFlightSwarmBuildGridCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmBuildGridCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmBuildGridCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, GridHeadBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, GridNextBuffer)
		SHADER_PARAMETER(uint32, NumEntities)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 1: SPH Density and Pressure */
class FLIGHTGPUCOMPUTE_API FFlightSwarmDensityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmDensityCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmDensityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, GridHeadBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, GridNextBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector2f>, DensityPressureBuffer)
		SHADER_PARAMETER(FVector4f, PlayerPosRadius)
		SHADER_PARAMETER(FVector4f, BeaconPosStrength)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, GasConstant)
		SHADER_PARAMETER(float, RestDensity)
		SHADER_PARAMETER(float, Viscosity)
		SHADER_PARAMETER(uint32, NumEntities)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 2: SPH Force and Gameplay Logic */
class FLIGHTGPUCOMPUTE_API FFlightSwarmForceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmForceCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmForceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, GridHeadBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, GridNextBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector2f>, DensityPressureBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector4f>, ForceBlackboard)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, DroidStatesRW)
		SHADER_PARAMETER(FVector4f, PlayerPosRadius)
		SHADER_PARAMETER(FVector4f, BeaconPosStrength)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, GasConstant)
		SHADER_PARAMETER(float, RestDensity)
		SHADER_PARAMETER(float, Viscosity)
		SHADER_PARAMETER(float, AvoidanceStrength)
		SHADER_PARAMETER(float, MaxAcceleration)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(uint32, CommandType)
		SHADER_PARAMETER(uint32, NumEntities)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 3: Integration and Bounds */
class FLIGHTGPUCOMPUTE_API FFlightSwarmIntegrationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmIntegrationCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmIntegrationCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ForceBlackboard)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, DroidStatesRW)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(uint32, NumEntities)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 5: Future Rollout (Predictive) */
class FLIGHTGPUCOMPUTE_API FFlightSwarmPredictiveCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmPredictiveCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmPredictiveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ForceBlackboard)
		SHADER_PARAMETER(FVector4f, PlayerPosRadius)
		SHADER_PARAMETER(FVector4f, BeaconPosStrength)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, GasConstant)
		SHADER_PARAMETER(float, RestDensity)
		SHADER_PARAMETER(float, Viscosity)
		SHADER_PARAMETER(float, AvoidanceStrength)
		SHADER_PARAMETER(float, MaxAcceleration)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(uint32, CommandType)
		SHADER_PARAMETER(uint32, FrameIndex)
		SHADER_PARAMETER(uint32, PredictionHorizon)
		SHADER_PARAMETER(uint32, NumEntities)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

#endif // WITH_FLIGHT_COMPUTE_SHADERS
