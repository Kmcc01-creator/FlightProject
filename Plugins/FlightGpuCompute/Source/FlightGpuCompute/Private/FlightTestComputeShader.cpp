// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Test compute shader for io_uring integration

#include "FlightTestComputeShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "Misc/App.h"

#if WITH_FLIGHT_COMPUTE_SHADERS

DEFINE_LOG_CATEGORY_STATIC(LogFlightTestCompute, Log, All);

namespace
{
	inline FGlobalShaderMap* TryGetFlightGlobalShaderMap()

	{
		if (!FApp::CanEverRender())
		{
			return nullptr;
		}

		const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel];
		if (ShaderPlatform == SP_NumPlatforms || GGlobalShaderMap[ShaderPlatform] == nullptr)
		{
			return nullptr;
		}

		return GetGlobalShaderMap(ShaderPlatform);
	}
}

// ============================================================================
// Shader Implementations
// ============================================================================

// Register shaders with UE's shader system
// PostConfigInit loading phase ensures this runs before shader compilation begins
IMPLEMENT_GLOBAL_SHADER(FFlightTestComputeShader,
	"/FlightProject/Private/FlightTestCompute.usf",
	"TestComputeMain",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightReductionComputeShader,
	"/FlightProject/Private/FlightTestCompute.usf",
	"ReductionComputeMain",
	SF_Compute);

// ============================================================================
// Dispatch Function
// ============================================================================

FRDGBufferRef DispatchFlightTestCompute(
	FRDGBuilder& GraphBuilder,
	uint32 BufferSize,
	uint32 TestValue)
{
	// Create output buffer
	FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), BufferSize);
	FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("FlightTestOutput"));

	if (IsRunningCommandlet())
	{
		return OutputBuffer;
	}

	// Get shader from global shader map
	FGlobalShaderMap* GlobalShaderMap = TryGetFlightGlobalShaderMap();
	if (!GlobalShaderMap)
	{
		UE_LOG(LogFlightTestCompute, Verbose,
			TEXT("DispatchFlightTestCompute: GlobalShaderMap unavailable (non-rendering or not initialized yet)"));
		return OutputBuffer;
	}
	TShaderMapRef<FFlightTestComputeShader> ComputeShader(GlobalShaderMap);

	if (!ComputeShader.IsValid())
	{
		UE_LOG(LogFlightTestCompute, Error,
			TEXT("DispatchFlightTestCompute: Shader not valid - may not be compiled yet"));
		return OutputBuffer;
	}

	// Setup parameters
	FFlightTestComputeShader::FParameters* Parameters =
		GraphBuilder.AllocParameters<FFlightTestComputeShader::FParameters>();
	Parameters->OutputBuffer = GraphBuilder.CreateUAV(OutputBuffer);
	Parameters->TestValue = TestValue;
	Parameters->BufferSize = BufferSize;

	// Calculate dispatch size (64 threads per group)
	uint32 ThreadGroupCount = FMath::DivideAndRoundUp(BufferSize, 64u);

	// Add compute pass
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("FlightTestCompute(%u elements)", BufferSize),
		ComputeShader,
		Parameters,
		FIntVector(ThreadGroupCount, 1, 1));

	UE_LOG(LogFlightTestCompute, Verbose,
		TEXT("Dispatched FlightTestCompute: %u elements, TestValue=%u, %u groups"),
		BufferSize, TestValue, ThreadGroupCount);

	return OutputBuffer;
}

bool AreFlightComputeShadersReady()
{
	if (IsRunningCommandlet())
	{
		return false;
	}

	// Check if global shader map exists and has our shaders
	FGlobalShaderMap* GlobalShaderMap = TryGetFlightGlobalShaderMap();
	if (!GlobalShaderMap)
	{
		return false;
	}

	// Try to get the shader - returns null if not compiled
	TShaderMapRef<FFlightTestComputeShader> TestShader(GlobalShaderMap);
	return TestShader.IsValid();
}

#endif // WITH_FLIGHT_COMPUTE_SHADERS
