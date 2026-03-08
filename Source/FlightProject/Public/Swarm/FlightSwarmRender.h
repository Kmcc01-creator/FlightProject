// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "RenderGraphUtils.h"

class FGlobalShaderMap;

namespace Flight::Swarm
{

/**
 * FSwarmSceneViewExtension
 * 
 * Injects a custom RDG pass into the Unreal rendering pipeline to perform
 * point splatting/volumetric rendering of the Swarm directly from the GPU buffer.
 */
class FSwarmSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FSwarmSceneViewExtension(const FAutoRegister& AutoRegister);

	// ~FSceneViewExtensionBase Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {}
	
	// We inject before post-processing so our emissive splats can be bloomed/tonemapped
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	// ~End FSceneViewExtensionBase Interface

	// The actual render pass delegate
	FScreenPassTexture PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const struct FPostProcessMaterialInputs& Inputs);

	// --- Custom Swarm Interface ---
	
	/** Sets the current frame's data. Call this from the Swarm subsystem on the render thread. */
	void UpdateSwarmData_RenderThread(
		TRefCountPtr<FRDGPooledBuffer> InDroidBuffer, 
		uint32 InNumEntities,
		TRefCountPtr<FRDGPooledTexture> InPropagatedLattice,
		TRefCountPtr<FRDGPooledTexture> InCloudTexture,
		const FVector3f& InLatticeGridCenter,
		float InLatticeGridExtent,
		uint32 InLatticeGridResolution,
		const FVector3f& InCloudGridCenter,
		float InCloudGridExtent,
		uint32 InCloudGridResolution);

private:
	TRefCountPtr<FRDGPooledBuffer> DroidBuffer;
	uint32 NumEntities = 0;
	TRefCountPtr<FRDGPooledTexture> PropagatedLattice;
	TRefCountPtr<FRDGPooledTexture> CloudTexture;
	FVector3f LatticeGridCenter = FVector3f::ZeroVector;
	float LatticeGridExtent = 10000.0f;
	uint32 LatticeGridResolution = 64;
	FVector3f CloudGridCenter = FVector3f::ZeroVector;
	float CloudGridExtent = 10000.0f;
	uint32 CloudGridResolution = 64;
};

} // namespace Flight::Swarm
