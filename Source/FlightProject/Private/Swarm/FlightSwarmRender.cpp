// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Swarm/FlightSwarmRender.h"
#include "SceneView.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SystemTextures.h"
#include "ScreenPass.h"
#include "CommonRenderResources.h"
#include "RenderCore.h"

#if WITH_FLIGHT_COMPUTE_SHADERS
#include "FlightSwarmShaders.h"
#endif

namespace Flight::Swarm
{

FSwarmSceneViewExtension::FSwarmSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FSwarmSceneViewExtension::UpdateSwarmData_RenderThread(
	TRefCountPtr<FRDGPooledBuffer> InDroidBuffer, 
	uint32 InNumEntities,
	TRefCountPtr<FRDGPooledTexture> InPropagatedLattice,
	TRefCountPtr<FRDGPooledTexture> InCloudTexture,
	const FVector3f& InLatticeGridCenter,
	float InLatticeGridExtent,
	uint32 InLatticeGridResolution,
	const FVector3f& InCloudGridCenter,
	float InCloudGridExtent,
	uint32 InCloudGridResolution)
{
	DroidBuffer = InDroidBuffer;
	NumEntities = InNumEntities;
	PropagatedLattice = InPropagatedLattice;
	CloudTexture = InCloudTexture;
	LatticeGridCenter = InLatticeGridCenter;
	LatticeGridExtent = InLatticeGridExtent;
	LatticeGridResolution = InLatticeGridResolution;
	CloudGridCenter = InCloudGridCenter;
	CloudGridExtent = InCloudGridExtent;
	CloudGridResolution = InCloudGridResolution;
}

void FSwarmSceneViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FSwarmSceneViewExtension::PostProcessPass_RenderThread));
	}
}

FScreenPassTexture FSwarmSceneViewExtension::PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTexture SceneColor = const_cast<FPostProcessMaterialInputs&>(Inputs).ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);

#if WITH_FLIGHT_COMPUTE_SHADERS
	if (!DroidBuffer.IsValid() || NumEntities == 0 || IsRunningCommandlet())
	{
		return SceneColor;
	}

	// Splatting and Resolve passes temporarily disabled to stabilize Phase 1 simulation verification.
	// We return standard SceneColor immediately to bypass the unstable NPR effects.
	return SceneColor;

#if 0
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	if (!ShaderMap || 
		!ShaderMap->HasShader(&FFlightSwarmSplattingCS::GetStaticType(), 0) ||
		!ShaderMap->HasShader(&FFlightSwarmSplatResolveCS::GetStaticType(), 0))
	{
		return SceneColor;
	}

	FRDGTextureRef SceneColorTexture = SceneColor.Texture;
	if (!SceneColorTexture)
	{
		return SceneColor;
	}

	const FIntPoint ViewSize = SceneColorTexture->Desc.Extent;

	// 1. Create F-Buffer Accumulators (Atomics)
	FRDGTextureDesc AccumDesc = FRDGTextureDesc::Create2D(
		ViewSize,
		PF_R32_UINT,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV
	);

	FRDGTextureRef AccumAlignment = GraphBuilder.CreateTexture(AccumDesc, TEXT("Swarm.FBuffer.Alignment"));
	FRDGTextureRef AccumIntensity = GraphBuilder.CreateTexture(AccumDesc, TEXT("Swarm.FBuffer.Intensity"));
	FRDGTextureRef AccumDensity   = GraphBuilder.CreateTexture(AccumDesc, TEXT("Swarm.FBuffer.Density"));
	FRDGTextureRef AccumPhase     = GraphBuilder.CreateTexture(AccumDesc, TEXT("Swarm.FBuffer.Phase"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumAlignment), 0u);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumIntensity), 0u);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumDensity),   0u);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AccumPhase),     0u);

	// 2. Splatting Pass
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmSplattingCS::FParameters>();
		PassParameters->DroidStates = GraphBuilder.RegisterExternalBuffer(DroidBuffer);
		PassParameters->AccumBufferAlignment = GraphBuilder.CreateUAV(AccumAlignment);
		PassParameters->AccumBufferIntensity = GraphBuilder.CreateUAV(AccumIntensity);
		PassParameters->AccumBufferDensity   = GraphBuilder.CreateUAV(AccumDensity);
		PassParameters->AccumBufferPhase     = GraphBuilder.CreateUAV(AccumPhase);

		FRDGTextureRef LatticeTexture = PropagatedLattice ? GraphBuilder.RegisterExternalTexture(PropagatedLattice) : GSystemTextures.GetBlackDummy(GraphBuilder);
		PassParameters->ReadLatticeArray = GraphBuilder.CreateSRV(LatticeTexture);
		PassParameters->ReadLatticeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		FRDGTextureRef CloudTex = CloudTexture ? GraphBuilder.RegisterExternalTexture(CloudTexture) : GSystemTextures.GetBlackDummy(GraphBuilder);
		PassParameters->ReadCloudArray = GraphBuilder.CreateSRV(CloudTex);
		PassParameters->ReadCloudSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		PassParameters->WorldToClip = FMatrix44f(View.ViewMatrices.GetViewProjectionMatrix());
		PassParameters->ViewSizeAndInvSize = FVector4f(View.UnscaledViewRect.Width(), View.UnscaledViewRect.Height(), 1.0f / View.UnscaledViewRect.Width(), 1.0f / View.UnscaledViewRect.Height());
		PassParameters->GridCenter = FVector4f(LatticeGridCenter, 0.0f);
		PassParameters->GridExtent = LatticeGridExtent;
		PassParameters->GridResolution = LatticeGridResolution;
		PassParameters->CloudGridCenter = FVector4f(CloudGridCenter, 0.0f);
		PassParameters->CloudGridExtent = CloudGridExtent;
		PassParameters->CloudGridResolution = CloudGridResolution;
		PassParameters->NumEntities = NumEntities;
		PassParameters->SplatSize = 1.0f; 

		auto ComputeShader = ShaderMap->GetShader<FFlightSwarmSplattingCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder, 
			RDG_EVENT_NAME("Swarm.FBuffer.Splatting"), 
			ComputeShader, 
			PassParameters, 
			FComputeShaderUtils::GetGroupCount(NumEntities, 64)
		);
	}

	// 3. Resolve Pass (NPR LERP Shading)
	{
		FRDGTextureDesc ResolveDesc = FRDGTextureDesc::Create2D(
			ViewSize,
			PF_FloatRGBA,
			FClearValueBinding::Transparent,
			TexCreate_ShaderResource | TexCreate_UAV
		);
		FRDGTextureRef ResolvedTexture = GraphBuilder.CreateTexture(ResolveDesc, TEXT("Swarm.FBuffer.Resolved"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ResolvedTexture), FLinearColor::Transparent);

		auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmSplatResolveCS::FParameters>();
		PassParameters->AccumBufferAlignment = GraphBuilder.CreateSRV(AccumAlignment);
		PassParameters->AccumBufferIntensity = GraphBuilder.CreateSRV(AccumIntensity);
		PassParameters->AccumBufferDensity   = GraphBuilder.CreateSRV(AccumDensity);
		PassParameters->AccumBufferPhase     = GraphBuilder.CreateSRV(AccumPhase);

		PassParameters->AccumBufferAlignment_SRV = PassParameters->AccumBufferAlignment;
		PassParameters->AccumBufferIntensity_SRV = PassParameters->AccumBufferIntensity;
		PassParameters->AccumBufferDensity_SRV   = PassParameters->AccumBufferDensity;
		PassParameters->AccumBufferPhase_SRV     = PassParameters->AccumBufferPhase;
		
		PassParameters->OutSceneColor = GraphBuilder.CreateUAV(ResolvedTexture);
		
		PassParameters->ShadowColor = FVector4f(0.02f, 0.05f, 0.2f, 1.0f);
		PassParameters->LitColor    = FVector4f(0.1f, 0.8f, 1.0f, 1.0f);
		PassParameters->ViewSize    = FVector2f(ViewSize);
		PassParameters->ViewSizeAndInvSize = FVector4f(ViewSize.X, ViewSize.Y, 1.0f/ViewSize.X, 1.0f/ViewSize.Y);

		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ViewSize, FIntPoint(8, 8));
		auto ComputeShader = ShaderMap->GetShader<FFlightSwarmSplatResolveCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder, 
			RDG_EVENT_NAME("Swarm.FBuffer.ResolveNPR"), 
			ComputeShader, 
			PassParameters, 
			GroupCount
		);
	}
#endif

#endif // WITH_FLIGHT_COMPUTE_SHADERS

	return SceneColor;
}

} // namespace Flight::Swarm
