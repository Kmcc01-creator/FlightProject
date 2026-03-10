// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - GPU horizon scan shader implementation

#include "FlightHorizonScanShader.h"
#include "FlightGpuComputeModule.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "Misc/App.h"

#if WITH_FLIGHT_COMPUTE_SHADERS

DEFINE_LOG_CATEGORY_STATIC(LogFlightHorizonScan, Log, All);

// ============================================================================
// Shader Registration
// ============================================================================

IMPLEMENT_GLOBAL_SHADER(FFlightHorizonScanShader,
	"/FlightProject/Private/FlightHorizonScan.usf",
	"HorizonScanMain",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightObstacleCountShader,
	"/FlightProject/Private/FlightHorizonScan.usf",
	"ObstacleCountMain",
	SF_Compute);

// ============================================================================
// Dispatch Functions
// ============================================================================

void DispatchFlightObstacleCount(
	FRDGBuilder& GraphBuilder,
	const FFlightHorizonScanInput& Input,
	FRDGBufferRef OutputBuffer,
	FRDGBufferRef ForceBlackboard,
	FRDGBufferRef TimestampBuffer,
	uint32 FrameIndex)
{
	if (Input.EntityPositions.Num() == 0)
	{
		UE_LOG(LogFlightHorizonScan, Warning, TEXT("DispatchFlightObstacleCount: No entities"));
		return;
	}

	const uint32 NumEntities = Input.EntityPositions.Num();
	const uint32 NumObstacles = Input.ObstacleMinBounds.Num();

	// Create GPU buffers for input data
	FRDGBufferRef EntityBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("FlightScan.EntityPositions"),
		sizeof(FVector4f),
		NumEntities,
		Input.EntityPositions.GetData(),
		sizeof(FVector4f) * NumEntities);

	FRDGBufferRef ObstacleMinBuffer = nullptr;
	FRDGBufferRef ObstacleMaxBuffer = nullptr;

	if (NumObstacles > 0)
	{
		ObstacleMinBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("FlightScan.ObstacleMin"),
			sizeof(FVector4f),
			NumObstacles,
			Input.ObstacleMinBounds.GetData(),
			sizeof(FVector4f) * NumObstacles);

		ObstacleMaxBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("FlightScan.ObstacleMax"),
			sizeof(FVector4f),
			NumObstacles,
			Input.ObstacleMaxBounds.GetData(),
			sizeof(FVector4f) * NumObstacles);
	}
	else
	{
		// Create dummy buffers if no obstacles
		FVector4f Dummy(0, 0, 0, 0);
		ObstacleMinBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("FlightScan.ObstacleMin"),
			sizeof(FVector4f),
			1,
			&Dummy,
			sizeof(FVector4f));

		ObstacleMaxBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("FlightScan.ObstacleMax"),
			sizeof(FVector4f),
			1,
			&Dummy,
			sizeof(FVector4f));
	}

	if (IsRunningCommandlet())
	{
		return;
	}

	// Get shader map
	FGlobalShaderMap* GlobalShaderMap = FFlightGpuComputeModule::TryGetFlightGlobalShaderMap();
	if (!GlobalShaderMap || !GlobalShaderMap->HasShader(&FFlightObstacleCountShader::GetStaticType(), 0))
	{
		UE_LOG(LogFlightHorizonScan, Error,
			TEXT("DispatchFlightObstacleCount: Shader not found in GlobalShaderMap"));
		return;
	}

	// Get shader
	TShaderMapRef<FFlightObstacleCountShader> ComputeShader(GlobalShaderMap);

	if (!ComputeShader.IsValid())
	{
		UE_LOG(LogFlightHorizonScan, Error,
			TEXT("DispatchFlightObstacleCount: Shader not compiled"));
		return;
	}

	// Setup parameters
	FFlightObstacleCountShader::FParameters* Parameters =
		GraphBuilder.AllocParameters<FFlightObstacleCountShader::FParameters>();

	Parameters->EntityPositions = GraphBuilder.CreateSRV(EntityBuffer);
	Parameters->ObstacleMinBounds = GraphBuilder.CreateSRV(ObstacleMinBuffer);
	Parameters->ObstacleMaxBounds = GraphBuilder.CreateSRV(ObstacleMaxBuffer);
	Parameters->ObstacleCountResults = GraphBuilder.CreateUAV(OutputBuffer);
	Parameters->ForceBlackboard = GraphBuilder.CreateUAV(ForceBlackboard);
	Parameters->TimestampBuffer = GraphBuilder.CreateUAV(TimestampBuffer);
	Parameters->NumEntities = NumEntities;
	Parameters->NumObstacles = NumObstacles;
	Parameters->DispatchFrameIndex = FrameIndex;

	// Dispatch (64 threads per group)
	uint32 ThreadGroupCount = FMath::DivideAndRoundUp(NumEntities, 64u);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("FlightObstacleCount(%u entities, %u obstacles)", NumEntities, NumObstacles),
		ComputeShader,
		Parameters,
		FIntVector(ThreadGroupCount, 1, 1));

	UE_LOG(LogFlightHorizonScan, Verbose,
		TEXT("Dispatched ObstacleCount: %u entities, %u obstacles, %u groups"),
		NumEntities, NumObstacles, ThreadGroupCount);
}

bool AreFlightHorizonScanShadersReady()
{
	if (IsRunningCommandlet())
	{
		return false;
	}

	FGlobalShaderMap* GlobalShaderMap = FFlightGpuComputeModule::TryGetFlightGlobalShaderMap();
	if (!GlobalShaderMap)
	{
		return false;
	}

	// Check if the shader type is even present in the map
	if (!GlobalShaderMap->HasShader(&FFlightObstacleCountShader::GetStaticType(), 0))
	{
		return false;
	}

	TShaderMapRef<FFlightObstacleCountShader> Shader(GlobalShaderMap);
	return Shader.IsValid();
}

#endif // WITH_FLIGHT_COMPUTE_SHADERS
