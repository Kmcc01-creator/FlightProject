// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Swarm/FlightSwarmSubsystem.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "DynamicRHI.h"
#include "CommonRenderResources.h"
#include "Engine/World.h"

#if WITH_FLIGHT_COMPUTE_SHADERS
#include "FlightSwarmShaders.h"
#endif

void UFlightSwarmSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("FlightSwarmSubsystem: Initializing Reactive GPU Swarm Infrastructure"));
}

void UFlightSwarmSubsystem::Deinitialize()
{
	// Ensure the render thread is done with our buffer before we release it
	FlushRenderingCommands();

	if (DroidStateBuffer.IsValid())
	{
		DroidStateBuffer.SafeRelease();
	}
	Super::Deinitialize();
}

void UFlightSwarmSubsystem::InitializeSwarm(int32 NumEntities)
{
	TotalEntities = NumEntities;
	
	TWeakObjectPtr<UFlightSwarmSubsystem> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(InitializeDroidBuffer)([WeakThis, NumEntities](FRHICommandListImmediate& RHICmdList)
	{
		UFlightSwarmSubsystem* Self = WeakThis.Get();
		if (!Self) return;

		// 1. Create the persistent RHI buffer
		FRHIBufferCreateDesc Desc = FRHIBufferCreateDesc::CreateStructured(
			TEXT("Flight.Swarm.DroidState"),
			sizeof(FDroidState) * NumEntities,
			sizeof(FDroidState)
		);
		Desc.AddUsage(BUF_ShaderResource | BUF_UnorderedAccess);
		Desc.SetInitialState(ERHIAccess::SRVMask);

		FBufferRHIRef DroidRHI = RHICmdList.CreateBuffer(Desc);

		// 2. Initialize with synthetic data
		TArray<FDroidState> InitialData;
		InitialData.SetNumUninitialized(NumEntities);
		for (int32 i = 0; i < NumEntities; ++i)
		{
			InitialData[i].Position = FVector3f(FMath::RandRange(-5000.f, 5000.f), FMath::RandRange(-5000.f, 5000.f), FMath::RandRange(1000.f, 5000.f));
			InitialData[i].Velocity = FVector3f::ZeroVector;
			InitialData[i].Shield = 1.0f;
			InitialData[i].Status = 0;
		}

		// Use command list versions of Lock/Unlock for proper synchronization
		void* BufferData = RHICmdList.LockBuffer(DroidRHI, 0, NumEntities * sizeof(FDroidState), RLM_WriteOnly);
		FMemory::Memcpy(BufferData, InitialData.GetData(), NumEntities * sizeof(FDroidState));
		RHICmdList.UnlockBuffer(DroidRHI);

		// 3. Wrap in a pooled buffer for RDG
		FRDGBufferDesc RDGDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FDroidState), NumEntities);
		
		// Assign to the subsystem member (Render Thread safe as TickSimulation also runs on Render Thread)
		Self->DroidStateBuffer = new FRDGPooledBuffer(DroidRHI, RDGDesc, NumEntities, TEXT("Flight.Swarm.DroidState"));

		UE_LOG(LogTemp, Log, TEXT("FlightSwarmSubsystem: Allocated Persistent GPU Buffer for %d entities (%.2f MB)"), 
			NumEntities, (NumEntities * sizeof(FDroidState)) / 1024.0 / 1024.0);
	});
}

FSwarmGlobalCommand UFlightSwarmSubsystem::GetPackedCommand(float DeltaTime)
{
	FSwarmGlobalCommand Cmd;
	Cmd.PlayerPosRadius = FVector4f((FVector3f)PlayerPosition.Get(), 1000.0f);
	Cmd.BeaconPosStrength = FVector4f((FVector3f)BeaconPosition.Get(), 1.0f);
	Cmd.SmoothingRadius = SmoothingRadius.Get();
	Cmd.GasConstant = GasConstant.Get();
	Cmd.RestDensity = RestDensity.Get();
	Cmd.Viscosity = Viscosity.Get();
	Cmd.AvoidanceStrength = AvoidanceStrength.Get();
	Cmd.MaxAcceleration = 5000.0f;
	Cmd.DeltaTime = DeltaTime;
	Cmd.CommandType = 0;
	Cmd.FrameIndex = static_cast<uint32>(GFrameCounter);
	Cmd.PredictionHorizon = static_cast<uint32>(PredictionHorizon.Get());
	
	Cmd.Padding0 = 0;
	Cmd.Padding1 = 0;

	return Cmd;
}

void UFlightSwarmSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only tick if the world is active (not during loading or stale editor ticks)
	if (GetWorld()->IsGameWorld() || GetWorld()->WorldType == EWorldType::PIE)
	{
		TickSimulation(DeltaTime);
	}
}

TStatId UFlightSwarmSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlightSwarmSubsystem, STATGROUP_Tickables);
}

void UFlightSwarmSubsystem::TickSimulation(float DeltaTime)
{
#if WITH_FLIGHT_COMPUTE_SHADERS
	if (TotalEntities <= 0) return;

	FSwarmGlobalCommand PackedCmd = GetPackedCommand(DeltaTime);
	TWeakObjectPtr<UFlightSwarmSubsystem> WeakThis(this);

	ENQUEUE_RENDER_COMMAND(FlightSwarmTick)([WeakThis, PackedCmd, NumEntities = (uint32)TotalEntities](FRHICommandListImmediate& RHICmdList)
	{
		UFlightSwarmSubsystem* Self = WeakThis.Get();
		if (!Self || !Self->DroidStateBuffer.IsValid()) return;

		FRDGBuilder GraphBuilder(RHICmdList);

		// 1. Register persistent buffer in RDG
		FRDGBufferRef DroidBuffer = GraphBuilder.RegisterExternalBuffer(Self->DroidStateBuffer);
		
		const uint32 GridSize = 1048576; // 2^20 - safer for older/integrated GPUs
		FRDGBufferDesc GridHeadDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GridSize);
		FRDGBufferRef GridHeadBuffer = GraphBuilder.CreateBuffer(GridHeadDesc, TEXT("Swarm.GridHead"));

		FRDGBufferDesc GridNextDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumEntities);
		FRDGBufferRef GridNextBuffer = GraphBuilder.CreateBuffer(GridNextDesc, TEXT("Swarm.GridNext"));

		FRDGBufferDesc DensityDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), NumEntities);
		FRDGBufferRef DensityBuffer = GraphBuilder.CreateBuffer(DensityDesc, TEXT("Swarm.DensityPressure"));

		FRDGBufferDesc ForceDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), NumEntities);
		FRDGBufferRef ForceBuffer = GraphBuilder.CreateBuffer(ForceDesc, TEXT("Swarm.ForceBlackboard"));

		uint32 ThreadGroups = FMath::DivideAndRoundUp(NumEntities, 64u);
		uint32 GridGroups = FMath::DivideAndRoundUp(GridSize, 64u);

		// --- PASS 0a: Clear Grid ---
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmClearGridCS::FParameters>();
			PassParameters->GridHeadBuffer = GraphBuilder.CreateUAV(GridHeadBuffer);
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FFlightSwarmClearGridCS>();
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Swarm.ClearGrid"), ComputeShader, PassParameters, FIntVector(GridGroups, 1, 1));
		}

		// --- PASS 0b: Build Grid ---
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmBuildGridCS::FParameters>();
			PassParameters->DroidStates = GraphBuilder.CreateSRV(DroidBuffer);
			PassParameters->GridHeadBuffer = GraphBuilder.CreateUAV(GridHeadBuffer);
			PassParameters->GridNextBuffer = GraphBuilder.CreateUAV(GridNextBuffer);
			PassParameters->NumEntities = NumEntities;
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FFlightSwarmBuildGridCS>();
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Swarm.BuildGrid"), ComputeShader, PassParameters, FIntVector(ThreadGroups, 1, 1));
		}

		// --- PASS 1: Density ---
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmDensityCS::FParameters>();
			PassParameters->DroidStates = GraphBuilder.CreateSRV(DroidBuffer);
			PassParameters->GridHeadBuffer = GraphBuilder.CreateSRV(GridHeadBuffer);
			PassParameters->GridNextBuffer = GraphBuilder.CreateSRV(GridNextBuffer);
			PassParameters->DensityPressureBuffer = GraphBuilder.CreateUAV(DensityBuffer);
			PassParameters->PlayerPosRadius = PackedCmd.PlayerPosRadius;
			PassParameters->BeaconPosStrength = PackedCmd.BeaconPosStrength;
			PassParameters->SmoothingRadius = PackedCmd.SmoothingRadius;
			PassParameters->GasConstant = PackedCmd.GasConstant;
			PassParameters->RestDensity = PackedCmd.RestDensity;
			PassParameters->Viscosity = PackedCmd.Viscosity;
			PassParameters->NumEntities = NumEntities;
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FFlightSwarmDensityCS>();
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Swarm.Density"), ComputeShader, PassParameters, FIntVector(ThreadGroups, 1, 1));
		}

		// --- PASS 2: Force (Reactive Layer) ---
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmForceCS::FParameters>();
			PassParameters->DroidStates = GraphBuilder.CreateSRV(DroidBuffer);
			PassParameters->GridHeadBuffer = GraphBuilder.CreateSRV(GridHeadBuffer);
			PassParameters->GridNextBuffer = GraphBuilder.CreateSRV(GridNextBuffer);
			PassParameters->DensityPressureBuffer = GraphBuilder.CreateSRV(DensityBuffer);
			PassParameters->ForceBlackboard = GraphBuilder.CreateUAV(ForceBuffer);
			PassParameters->DroidStatesRW = GraphBuilder.CreateUAV(DroidBuffer);
			PassParameters->PlayerPosRadius = PackedCmd.PlayerPosRadius;
			PassParameters->BeaconPosStrength = PackedCmd.BeaconPosStrength;
			PassParameters->SmoothingRadius = PackedCmd.SmoothingRadius;
			PassParameters->GasConstant = PackedCmd.GasConstant;
			PassParameters->RestDensity = PackedCmd.RestDensity;
			PassParameters->Viscosity = PackedCmd.Viscosity;
			PassParameters->AvoidanceStrength = PackedCmd.AvoidanceStrength;
			PassParameters->MaxAcceleration = PackedCmd.MaxAcceleration;
			PassParameters->DeltaTime = PackedCmd.DeltaTime;
			PassParameters->CommandType = PackedCmd.CommandType;
			PassParameters->NumEntities = NumEntities;
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FFlightSwarmForceCS>();
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Swarm.ForceReactive"), ComputeShader, PassParameters, FIntVector(ThreadGroups, 1, 1));
		}

		// --- PASS 2.5: Predictive Rollout (Model Layer) ---
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmPredictiveCS::FParameters>();
			PassParameters->DroidStates = GraphBuilder.CreateSRV(DroidBuffer);
			PassParameters->ForceBlackboard = GraphBuilder.CreateUAV(ForceBuffer);
			PassParameters->PlayerPosRadius = PackedCmd.PlayerPosRadius;
			PassParameters->BeaconPosStrength = PackedCmd.BeaconPosStrength;
			PassParameters->SmoothingRadius = PackedCmd.SmoothingRadius;
			PassParameters->GasConstant = PackedCmd.GasConstant;
			PassParameters->RestDensity = PackedCmd.RestDensity;
			PassParameters->Viscosity = PackedCmd.Viscosity;
			PassParameters->AvoidanceStrength = PackedCmd.AvoidanceStrength;
			PassParameters->MaxAcceleration = PackedCmd.MaxAcceleration;
			PassParameters->DeltaTime = PackedCmd.DeltaTime;
			PassParameters->CommandType = PackedCmd.CommandType;
			PassParameters->FrameIndex = PackedCmd.FrameIndex;
			PassParameters->PredictionHorizon = PackedCmd.PredictionHorizon;
			PassParameters->NumEntities = NumEntities;
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FFlightSwarmPredictiveCS>();
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Swarm.PredictiveRollout"), ComputeShader, PassParameters, FIntVector(ThreadGroups, 1, 1));
		}

		// --- PASS 3: Integration ---
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FFlightSwarmIntegrationCS::FParameters>();
			PassParameters->ForceBlackboard = GraphBuilder.CreateSRV(ForceBuffer);
			PassParameters->DroidStates = GraphBuilder.CreateSRV(DroidBuffer);
			PassParameters->DroidStatesRW = GraphBuilder.CreateUAV(DroidBuffer);
			PassParameters->DeltaTime = PackedCmd.DeltaTime;
			PassParameters->NumEntities = NumEntities;
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FFlightSwarmIntegrationCS>();
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Swarm.Integrate"), ComputeShader, PassParameters, FIntVector(ThreadGroups, 1, 1));
		}

		GraphBuilder.Execute();
	});
#endif
}
