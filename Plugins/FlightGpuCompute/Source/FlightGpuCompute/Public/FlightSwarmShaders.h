// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"

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

/** Radix Sort Pass 1: Count Bits */
class FLIGHTGPUCOMPUTE_API FFlightRadixCountCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightRadixCountCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightRadixCountCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, BitCounts)
		SHADER_PARAMETER(uint32, NumEntities)
		SHADER_PARAMETER(uint32, BitOffset)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Radix Sort Pass 2: Prefix Sum (Scan) */
class FLIGHTGPUCOMPUTE_API FFlightRadixScanCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightRadixScanCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightRadixScanCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, BitCounts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, Offsets)
		SHADER_PARAMETER(uint32, NumElements)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Radix Sort Pass 3: Scatter Entities */
class FLIGHTGPUCOMPUTE_API FFlightRadixScatterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightRadixScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightRadixScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InDroidStates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutDroidStates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, Offsets)
		SHADER_PARAMETER(uint32, NumEntities)
		SHADER_PARAMETER(uint32, BitOffset)
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
		
		// SCSL: Event Buffer
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTransientEvent>, EventBuffer)

		// SCSL: Light Lattice Array
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, ReadLatticeArray)
		SHADER_PARAMETER_SAMPLER(SamplerState, ReadLatticeSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutLatticeRArray)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutLatticeGArray)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutLatticeBArray)

		// SCSL: Cloud Field Array
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, ReadCloudArray)
		SHADER_PARAMETER_SAMPLER(SamplerState, ReadCloudSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutCloudAccumArray)

		SHADER_PARAMETER(FVector3f, GridCenter)
		SHADER_PARAMETER(float, GridExtent)
		SHADER_PARAMETER(uint32, GridResolution)

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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTransientEvent>, EventBuffer)
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

/** Pass 6: F-Buffer Splatting (Projection of Field Potential) */
class FLIGHTGPUCOMPUTE_API FFlightSwarmSplattingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmSplattingCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmSplattingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, ReadLatticeArray)
		SHADER_PARAMETER_SAMPLER(SamplerState, ReadLatticeSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, ReadCloudArray)
		SHADER_PARAMETER_SAMPLER(SamplerState, ReadCloudSampler)
		
		// F-Buffer Accumulators (Atomics)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, AccumBufferAlignment) // N dot L
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, AccumBufferIntensity) // |L|
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, AccumBufferDensity)   // C_d
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, AccumBufferPhase)     // Phi
			
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER(FVector3f, GridCenter)
			SHADER_PARAMETER(float, GridExtent)
			SHADER_PARAMETER(uint32, GridResolution)
			SHADER_PARAMETER(FVector3f, CloudGridCenter)
			SHADER_PARAMETER(float, CloudGridExtent)
			SHADER_PARAMETER(uint32, CloudGridResolution)
			SHADER_PARAMETER(uint32, NumEntities)
			SHADER_PARAMETER(float, SplatSize)
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 7: F-Buffer Resolve & NPR Stylize */
class FLIGHTGPUCOMPUTE_API FFlightSwarmSplatResolveCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightSwarmSplatResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSwarmSplatResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint>, AccumBufferAlignment)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint>, AccumBufferIntensity)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint>, AccumBufferDensity)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint>, AccumBufferPhase)
		
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSceneColor)
		
		SHADER_PARAMETER(FVector4f, ShadowColor)
		SHADER_PARAMETER(FVector4f, LitColor)
		SHADER_PARAMETER(FVector2f, ViewSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 8: Light Injection (Swarm points to 3D Grid) */
class FLIGHTGPUCOMPUTE_API FFlightLightInjectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightLightInjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightLightInjectionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutLatticeRArray)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutLatticeGArray)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutLatticeBArray)
		SHADER_PARAMETER(FVector3f, GridCenter)
		SHADER_PARAMETER(float, GridExtent)
		SHADER_PARAMETER(uint32, GridResolution)
		SHADER_PARAMETER(uint32, NumEntities)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 9: Light Propagation (3D Diffusion) */
class FLIGHTGPUCOMPUTE_API FFlightLightPropagationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightLightPropagationCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightLightPropagationCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, ReadLatticeArray)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, WriteLatticeArray)
		SHADER_PARAMETER(float, DecayRate)
		SHADER_PARAMETER(float, PropagationStrength)
		SHADER_PARAMETER(uint32, GridResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 10: Lattice Convert (UINT to FLOAT) */
class FLIGHTGPUCOMPUTE_API FFlightLightConvertCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightLightConvertCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightLightConvertCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, InLatticeRArray)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, InLatticeGArray)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, InLatticeBArray)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, PrevLatticeArray)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutLatticeArray)
		SHADER_PARAMETER(uint32, bUsePrevLattice)
		SHADER_PARAMETER(uint32, GridResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 11: Cloud Injection (Swarm to Density Field) */
class FLIGHTGPUCOMPUTE_API FFlightCloudInjectionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightCloudInjectionCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightCloudInjectionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DroidStates)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, OutCloudAccumArray)
		SHADER_PARAMETER(FVector3f, GridCenter)
		SHADER_PARAMETER(float, GridExtent)
		SHADER_PARAMETER(uint32, GridResolution)
		SHADER_PARAMETER(uint32, NumEntities)
		SHADER_PARAMETER(float, InjectionAmount)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Pass 12: Cloud Simulation (Diffusion/Dissipation) */
class FLIGHTGPUCOMPUTE_API FFlightCloudSimCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFlightCloudSimCS);
	SHADER_USE_PARAMETER_STRUCT(FFlightCloudSimCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, InCloudAccumArray)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, ReadCloudArray)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, WriteCloudArray)
		SHADER_PARAMETER(float, DecayRate)
		SHADER_PARAMETER(float, DiffusionStrength)
		SHADER_PARAMETER(uint32, GridResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

#endif // WITH_FLIGHT_COMPUTE_SHADERS
