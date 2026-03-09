// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Rendering/FlightSimpleSCSLShaderPipelineSubsystem.h"

#include "FlightProject.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"

#include "CommonRenderResources.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "Misc/ScopeLock.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderCore.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Flight::Rendering
{

namespace
{

FFlightSimpleSCSLShaderPipelineConfig SanitizeConfig(const FFlightSimpleSCSLShaderPipelineConfig& InConfig)
{
	FFlightSimpleSCSLShaderPipelineConfig OutConfig = InConfig;
	OutConfig.BlendWeight = FMath::Clamp(OutConfig.BlendWeight, 0.0f, 1.0f);
	OutConfig.PosterizeSteps = FMath::Clamp(OutConfig.PosterizeSteps, 1.0f, 32.0f);
	OutConfig.EdgeWeight = FMath::Clamp(OutConfig.EdgeWeight, 0.0f, 4.0f);
	OutConfig.ScanlineWeight = FMath::Clamp(OutConfig.ScanlineWeight, 0.0f, 1.0f);
	OutConfig.TimeScale = FMath::Clamp(OutConfig.TimeScale, 0.0f, 8.0f);
	OutConfig.Tint.A = 1.0f;
	return OutConfig;
}

bool ConfigsEqual(
	const FFlightSimpleSCSLShaderPipelineConfig& Left,
	const FFlightSimpleSCSLShaderPipelineConfig& Right)
{
	return Left.bEnabled == Right.bEnabled
		&& Left.Tint.Equals(Right.Tint)
		&& FMath::IsNearlyEqual(Left.BlendWeight, Right.BlendWeight)
		&& FMath::IsNearlyEqual(Left.PosterizeSteps, Right.PosterizeSteps)
		&& FMath::IsNearlyEqual(Left.EdgeWeight, Right.EdgeWeight)
		&& FMath::IsNearlyEqual(Left.ScanlineWeight, Right.ScanlineWeight)
		&& FMath::IsNearlyEqual(Left.TimeScale, Right.TimeScale);
}

class FFlightSimpleSCSLPostProcessPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFlightSimpleSCSLPostProcessPS);
	SHADER_USE_PARAMETER_STRUCT(FFlightSimpleSCSLPostProcessPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector4f, Tint)
		SHADER_PARAMETER(float, BlendWeight)
		SHADER_PARAMETER(float, PosterizeSteps)
		SHADER_PARAMETER(float, EdgeWeight)
		SHADER_PARAMETER(float, ScanlineWeight)
		SHADER_PARAMETER(float, TimeSeconds)
		SHADER_PARAMETER(float, TimeScale)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

#if !WITH_EDITOR
// In non-editor builds, we can just implement normally.
IMPLEMENT_GLOBAL_SHADER(FFlightSimpleSCSLPostProcessPS, "/FlightProject/Private/FlightSimpleSCSLPostProcess.usf", "MainPS", SF_Pixel);
#else
// In editor/commandlet environments, we might need to skip this if we're in a restricted mode.
// However, IMPLEMENT_GLOBAL_SHADER is a macro that defines a global static variable.
// We can't easily wrap it in a runtime check.
// Let's look at how other global shaders handle this.
// Actually, the error was because the module was loaded too late relative to the shader system initialization.
IMPLEMENT_GLOBAL_SHADER(FFlightSimpleSCSLPostProcessPS, "/FlightProject/Private/FlightSimpleSCSLPostProcess.usf", "MainPS", SF_Pixel);
#endif

} // namespace

class FSimpleSCSLSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FSimpleSCSLSceneViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {}

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass PassId,
		const FSceneView& View,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override
	{
		if (PassId != EPostProcessingPass::Tonemap || !bIsPassEnabled)
		{
			return;
		}

		const FFlightSimpleSCSLShaderPipelineConfig ConfigSnapshot = GetConfigSnapshot();
		if (!ConfigSnapshot.bEnabled)
		{
			return;
		}

		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FSimpleSCSLSceneViewExtension::PostProcessPass_RenderThread));
	}

	void UpdateConfig(const FFlightSimpleSCSLShaderPipelineConfig& InConfig)
	{
		FScopeLock Lock(&ConfigMutex);
		Config = SanitizeConfig(InConfig);
	}

	FScreenPassTexture PostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs)
	{
		FScreenPassTexture SceneColor = const_cast<FPostProcessMaterialInputs&>(Inputs).ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		if (!SceneColor.IsValid())
		{
			return SceneColor;
		}

		const FFlightSimpleSCSLShaderPipelineConfig ConfigSnapshot = GetConfigSnapshot();
		if (!ConfigSnapshot.bEnabled || IsRunningCommandlet())
		{
			return SceneColor;
		}

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		if (!ShaderMap)
		{
			return SceneColor;
		}

		TShaderMapRef<FFlightSimpleSCSLPostProcessPS> PixelShader(ShaderMap);
		if (!PixelShader.IsValid())
		{
			return SceneColor;
		}

		const FScreenPassTextureViewport InputViewport(SceneColor);
		FScreenPassRenderTarget Output = FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder,
			SceneColor,
			ERenderTargetLoadAction::ENoAction,
			TEXT("Flight.SimpleSCSL.SceneColor"));

		FFlightSimpleSCSLPostProcessPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FFlightSimpleSCSLPostProcessPS::FParameters>();
		PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
		PassParameters->Output = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(Output));
		PassParameters->InputTexture = SceneColor.Texture;
		PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->Tint = FVector4f(
			ConfigSnapshot.Tint.R,
			ConfigSnapshot.Tint.G,
			ConfigSnapshot.Tint.B,
			ConfigSnapshot.Tint.A);
		PassParameters->BlendWeight = ConfigSnapshot.BlendWeight;
		PassParameters->PosterizeSteps = ConfigSnapshot.PosterizeSteps;
		PassParameters->EdgeWeight = ConfigSnapshot.EdgeWeight;
		PassParameters->ScanlineWeight = ConfigSnapshot.ScanlineWeight;
		PassParameters->TimeSeconds = View.Family->Time.GetWorldTimeSeconds();
		PassParameters->TimeScale = ConfigSnapshot.TimeScale;
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("Flight.SimpleSCSL"),
			PixelShader,
			PassParameters,
			Output.ViewRect);

		return MoveTemp(Output);
	}

private:
	FFlightSimpleSCSLShaderPipelineConfig GetConfigSnapshot() const
	{
		FScopeLock Lock(&ConfigMutex);
		return Config;
	}

	mutable FCriticalSection ConfigMutex;
	FFlightSimpleSCSLShaderPipelineConfig Config;
};

} // namespace Flight::Rendering

void UFlightSimpleSCSLShaderPipelineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!IsRunningCommandlet())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<Flight::Rendering::FSimpleSCSLSceneViewExtension>();
	}

	ApplyConfigToViewExtension();
}

void UFlightSimpleSCSLShaderPipelineSubsystem::Deinitialize()
{
	FlushRenderingCommands();
	if (ViewExtension.IsValid())
	{
		ViewExtension.Reset();
	}

	Super::Deinitialize();
}

bool UFlightSimpleSCSLShaderPipelineSubsystem::IsEnabled() const
{
	return Config.bEnabled;
}

FFlightSimpleSCSLShaderPipelineConfig UFlightSimpleSCSLShaderPipelineSubsystem::GetConfig() const
{
	return Config;
}

void UFlightSimpleSCSLShaderPipelineSubsystem::SetEnabled(const bool bInEnabled)
{
	if (Config.bEnabled == bInEnabled)
	{
		return;
	}

	Config.bEnabled = bInEnabled;
	ApplyConfigToViewExtension();
	NotifyOrchestrationChanged();
}

void UFlightSimpleSCSLShaderPipelineSubsystem::SetConfig(const FFlightSimpleSCSLShaderPipelineConfig& InConfig)
{
	const FFlightSimpleSCSLShaderPipelineConfig SanitizedConfig = Flight::Rendering::SanitizeConfig(InConfig);
	if (Flight::Rendering::ConfigsEqual(Config, SanitizedConfig))
	{
		return;
	}

	Config = SanitizedConfig;
	ApplyConfigToViewExtension();
	NotifyOrchestrationChanged();
}

FString UFlightSimpleSCSLShaderPipelineSubsystem::BuildStateJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("enabled"), Config.bEnabled);
	Root->SetBoolField(TEXT("commandletBypass"), IsRunningCommandlet());
	Root->SetBoolField(TEXT("hasViewExtension"), ViewExtension.IsValid());
	Root->SetStringField(TEXT("blendPoint"), TEXT("Tonemap"));
	Root->SetStringField(TEXT("mode"), TEXT("SimpleSCSLPostProcess"));
	Root->SetNumberField(TEXT("blendWeight"), Config.BlendWeight);
	Root->SetNumberField(TEXT("posterizeSteps"), Config.PosterizeSteps);
	Root->SetNumberField(TEXT("edgeWeight"), Config.EdgeWeight);
	Root->SetNumberField(TEXT("scanlineWeight"), Config.ScanlineWeight);
	Root->SetNumberField(TEXT("timeScale"), Config.TimeScale);

	TSharedRef<FJsonObject> TintObject = MakeShared<FJsonObject>();
	TintObject->SetNumberField(TEXT("r"), Config.Tint.R);
	TintObject->SetNumberField(TEXT("g"), Config.Tint.G);
	TintObject->SetNumberField(TEXT("b"), Config.Tint.B);
	TintObject->SetNumberField(TEXT("a"), Config.Tint.A);
	Root->SetObjectField(TEXT("tint"), TintObject);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

FString UFlightSimpleSCSLShaderPipelineSubsystem::BuildServiceDetail() const
{
	return BuildStateJson();
}

void UFlightSimpleSCSLShaderPipelineSubsystem::ApplyConfigToViewExtension()
{
	if (ViewExtension.IsValid())
	{
		ViewExtension->UpdateConfig(Config);
	}
}

void UFlightSimpleSCSLShaderPipelineSubsystem::NotifyOrchestrationChanged()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UFlightOrchestrationSubsystem* OrchestrationSubsystem = World->GetSubsystem<UFlightOrchestrationSubsystem>())
	{
		OrchestrationSubsystem->RebuildVisibility();
		OrchestrationSubsystem->RebuildExecutionPlan();
	}
}
