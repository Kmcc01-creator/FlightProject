// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Swarm/FlightSwarmRender.h"
#include "SceneView.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SystemTextures.h"

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

	// 2. Accumulation Pass (Project Field Potential)
	{
		FRDGBufferRef RDGDroidBuffer = GraphBuilder.RegisterExternalBuffer(DroidBuffer);

		auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmSplattingCS::FParameters>();
		PassParameters->DroidStates = GraphBuilder.CreateSRV(RDGDroidBuffer);
		
		FRDGTextureRef LatticeTexture = PropagatedLattice.IsValid() 
			? GraphBuilder.RegisterExternalTexture((IPooledRenderTarget*)PropagatedLattice.GetReference())
			: GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, TEXT("Swarm.Lattice.Dummy"));
		PassParameters->ReadLatticeArray = GraphBuilder.CreateSRV(LatticeTexture);
		PassParameters->ReadLatticeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		FRDGTextureRef CloudTex = CloudTexture.IsValid()
			? GraphBuilder.RegisterExternalTexture((IPooledRenderTarget*)CloudTexture.GetReference())
			: GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, TEXT("Swarm.Cloud.Dummy"));
		PassParameters->ReadCloudArray = GraphBuilder.CreateSRV(CloudTex);
		PassParameters->ReadCloudSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			PassParameters->AccumBufferAlignment = GraphBuilder.CreateUAV(AccumAlignment);
			PassParameters->AccumBufferIntensity = GraphBuilder.CreateUAV(AccumIntensity);
			PassParameters->AccumBufferDensity   = GraphBuilder.CreateUAV(AccumDensity);
			PassParameters->AccumBufferPhase     = GraphBuilder.CreateUAV(AccumPhase);

			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->GridCenter = LatticeGridCenter;
			PassParameters->GridExtent = LatticeGridExtent;
			PassParameters->GridResolution = LatticeGridResolution;
			PassParameters->CloudGridCenter = CloudGridCenter;
			PassParameters->CloudGridExtent = CloudGridExtent;
			PassParameters->CloudGridResolution = CloudGridResolution;
			PassParameters->NumEntities = NumEntities;
			PassParameters->SplatSize = 1.0f; 

		uint32 ThreadGroups = FMath::DivideAndRoundUp(NumEntities, 64u);
		auto ComputeShader = ShaderMap->GetShader<FFlightSwarmSplattingCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder, 
			RDG_EVENT_NAME("Swarm.FBuffer.Accumulate"), 
			ComputeShader, 
			PassParameters, 
			FIntVector(ThreadGroups, 1, 1)
		);
	}

	// 3. Resolve Pass (NPR LERP Shading)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmSplatResolveCS::FParameters>();
		PassParameters->AccumBufferAlignment = GraphBuilder.CreateSRV(AccumAlignment);
		PassParameters->AccumBufferIntensity = GraphBuilder.CreateSRV(AccumIntensity);
		PassParameters->AccumBufferDensity   = GraphBuilder.CreateSRV(AccumDensity);
		PassParameters->AccumBufferPhase     = GraphBuilder.CreateSRV(AccumPhase);
		
		PassParameters->OutSceneColor = GraphBuilder.CreateUAV(SceneColorTexture);
		
		// Stylization Params (Anime Theme)
		PassParameters->ShadowColor = FVector4f(0.02f, 0.05f, 0.2f, 1.0f); // Deep indigo
		PassParameters->LitColor    = FVector4f(0.1f, 0.8f, 1.0f, 1.0f);    // Cyan highlight
		PassParameters->ViewSize    = FVector2f(ViewSize);

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

#endif // WITH_FLIGHT_COMPUTE_SHADERS

	return SceneColor;
}

} // namespace Flight::Swarm
