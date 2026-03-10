// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Swarm/FlightSwarmSubsystem.h"
#include "Swarm/FlightSwarmRender.h"
#include "FlightLogCategories.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Core/FlightHlslReflection.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "DynamicRHI.h"
#include "CommonRenderResources.h"
#include "RenderTargetPool.h"
#include "Engine/World.h"
#include "Core/FlightFunctional.h"
#include "SystemTextures.h"
#include "RHIStaticStates.h"
#include "Misc/ScopeLock.h"

#if WITH_FLIGHT_COMPUTE_SHADERS
#include "FlightSwarmShaders.h"
#endif

using namespace Flight::Functional;

namespace
{
#if WITH_FLIGHT_COMPUTE_SHADERS
	static TAutoConsoleVariable<int32> CVarFlightSwarmPersistenceMode(
		TEXT("Flight.Swarm.PersistenceMode"),
		0,
		TEXT("Requested SCSL field persistence mode. 0=Stateless, 1=Persistent request (observability)."),
		ECVF_Default);

	static bool IsPersistenceRequested()
	{
		return CVarFlightSwarmPersistenceMode.GetValueOnAnyThread() != 0;
	}

	static bool DoesPersistentTextureMatch(
		const TRefCountPtr<FRDGPooledTexture>& PersistentTexture,
		const uint32 ExpectedResolution,
		const EPixelFormat ExpectedFormat)
	{
		if (!PersistentTexture.IsValid())
		{
			return false;
		}

		const FRHITexture* TextureRHI = PersistentTexture->GetRHI();
		if (!TextureRHI)
		{
			return false;
		}

		const FRHITextureDesc& Desc = TextureRHI->GetDesc();
		return Desc.Format == ExpectedFormat
			&& Desc.Dimension == ETextureDimension::Texture3D
			&& Desc.Extent.X == static_cast<int32>(ExpectedResolution)
			&& Desc.Extent.Y == static_cast<int32>(ExpectedResolution)
			&& Desc.Depth == static_cast<uint16>(ExpectedResolution);
	}

    /** Context for the monadic RDG pipeline */
    struct FSwarmRDGContext
    {
        FRDGBuilder* GraphBuilder;
        FGlobalShaderMap* ShaderMap;
        FSwarmGlobalCommand Command;
        uint32 NumEntities;
		ESwarmSortRequirement RequiredSort;

        FRDGBufferRef DroidBuffer = nullptr;
        FRDGBufferRef GridHeadBuffer = nullptr;
        FRDGBufferRef GridNextBuffer = nullptr;
        FRDGBufferRef DensityBuffer = nullptr;
        FRDGBufferRef ForceBuffer = nullptr;

        // Radix Sort Resources
        FRDGBufferRef SortBitCounts = nullptr;
        FRDGBufferRef SortOffsets = nullptr;
        FRDGBufferRef SortedDroidBuffer = nullptr;

        // SCSL: Event Field
        FRDGBufferRef EventBuffer = nullptr;

        // SCSL: Light Lattice Array Resources
        FRDGTextureRef LatticeInjectionRArray = nullptr;
        FRDGTextureRef LatticeInjectionGArray = nullptr;
        FRDGTextureRef LatticeInjectionBArray = nullptr;
        FRDGTextureRef LatticePropagateAArray = nullptr;
        FRDGTextureRef LatticePropagateBArray = nullptr;

        // SCSL: Cloud Array Resources
        FRDGTextureRef CloudInjectionBufferArray = nullptr;
        FRDGTextureRef CloudPropagateAArray = nullptr;
        FRDGTextureRef CloudPropagateBArray = nullptr;
        FRDGTextureRef PrevLatticeArray = nullptr;
        FRDGTextureRef CloudReadArray = nullptr;

		FVector3f GridCenter;
        bool bPersistenceRequested = false;
        bool bPersistenceApplied = false;
        bool bLatticePersistentValid = false;
        bool bCloudPersistentValid = false;
        bool bLatticePersistentMatches = false;
        bool bCloudPersistentMatches = false;
        bool bLatticePersistenceApplied = false;
        bool bCloudPersistenceApplied = false;

        uint32 ThreadGroups = 0;
        uint32 GridGroups = 0;
        uint32 LatticeGroups = 0;
        uint32 CloudGroups = 0;
    };

    /** Validation step: Verify all required shaders are present in the global map */
    TResult<FGlobalShaderMap*, FString> CheckShaders(FGlobalShaderMap* ShaderMap)
    {
        if (!ShaderMap) return Err<FGlobalShaderMap*, FString>(FString(TEXT("GlobalShaderMap is null")));

        const bool bHasAll = 
            ShaderMap->HasShader(&FFlightSwarmClearGridCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightSwarmBuildGridCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightRadixCountCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightRadixScanCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightRadixScatterCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightSwarmDensityCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightSwarmForceCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightSwarmPredictiveCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightSwarmIntegrationCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightLightInjectionCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightLightPropagationCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightLightConvertCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightCloudInjectionCS::GetStaticType(), 0) &&
            ShaderMap->HasShader(&FFlightCloudSimCS::GetStaticType(), 0);

        return bHasAll ? Ok(ShaderMap) : Err<FGlobalShaderMap*, FString>(FString(TEXT("Missing one or more SCSL compute shaders")));
    }

    /** Step: Initialize RDG resources and thread group counts */
    TResult<FSwarmRDGContext> InitResources(
        FRDGBuilder* GraphBuilder, 
        FGlobalShaderMap* ShaderMap, 
        const FSwarmGlobalCommand& Command, 
        uint32 NumEntities,
        uint32 DroidStateElementStrideBytes,
        TRefCountPtr<FRDGPooledBuffer> DroidStateBuffer,
        TRefCountPtr<FRDGPooledTexture> PersistentLattice,
        TRefCountPtr<FRDGPooledTexture> PersistentCloud,
		ESwarmSortRequirement RequiredSort)
    {
        FSwarmRDGContext Ctx{ GraphBuilder, ShaderMap, Command, NumEntities, RequiredSort };
		Ctx.GridCenter = Command.PlayerPosRadius.GetSafeNormal().IsNearlyZero() ? FVector3f::ZeroVector : FVector3f(Command.PlayerPosRadius.X, Command.PlayerPosRadius.Y, Command.PlayerPosRadius.Z);

        Ctx.DroidBuffer = GraphBuilder->RegisterExternalBuffer(DroidStateBuffer);
        
        Ctx.GridHeadBuffer = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), Flight::Swarm::GridHashSize), TEXT("Swarm.GridHead"));
        Ctx.GridNextBuffer = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumEntities), TEXT("Swarm.GridNext"));
        Ctx.DensityBuffer = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), NumEntities), TEXT("Swarm.DensityPressure"));
        Ctx.ForceBuffer = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), NumEntities), TEXT("Swarm.ForceBlackboard"));

        // Radix Sort: Allocate counters and ping-pong target (16 bins for 4-bit passes)
        Ctx.SortBitCounts = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 16), TEXT("Swarm.Sort.BitCounts"));
        Ctx.SortOffsets = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 16), TEXT("Swarm.Sort.Offsets"));
        Ctx.SortedDroidBuffer = GraphBuilder->CreateBuffer(
            FRDGBufferDesc::CreateStructuredDesc(DroidStateElementStrideBytes, NumEntities),
            TEXT("Swarm.DroidStateBuffer.SortScratch"));

        // SCSL: Event Field Allocation & Upload
        const uint32 NumActiveEvents = Command.NumEvents;
        if (NumActiveEvents > 0)
        {
            Ctx.EventBuffer = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FTransientEvent), NumActiveEvents), TEXT("Swarm.Events"));
			// ... upload logic here ...
        }
        else
        {
            Ctx.EventBuffer = GraphBuilder->CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FTransientEvent), 1), TEXT("Swarm.Events.Dummy"));
			FTransientEvent DummyEv;
			FMemory::Memset(&DummyEv, 0, sizeof(FTransientEvent));
			GraphBuilder->QueueBufferUpload(Ctx.EventBuffer, &DummyEv, sizeof(FTransientEvent));
        }

        // SCSL: Allocate Light Lattice
        const uint32 LRes = Command.LatticeResolution;
        const FIntVector LExtent(LRes, LRes, LRes);
        
        FRDGTextureDesc LAccumDesc = FRDGTextureDesc::Create3D(LExtent, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        Ctx.LatticeInjectionRArray = GraphBuilder->CreateTexture(LAccumDesc, TEXT("Swarm.LatticeArray.InjectionR"));
        Ctx.LatticeInjectionGArray = GraphBuilder->CreateTexture(LAccumDesc, TEXT("Swarm.LatticeArray.InjectionG"));
        Ctx.LatticeInjectionBArray = GraphBuilder->CreateTexture(LAccumDesc, TEXT("Swarm.LatticeArray.InjectionB"));
        
        FRDGTextureDesc LPropDesc = FRDGTextureDesc::Create3D(LExtent, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
        Ctx.LatticePropagateAArray = GraphBuilder->CreateTexture(LPropDesc, TEXT("Swarm.LatticeArray.PropagateA"));
        Ctx.LatticePropagateBArray = GraphBuilder->CreateTexture(LPropDesc, TEXT("Swarm.LatticeArray.PropagateB"));
        
        // SCSL: Allocate Cloud
        const uint32 CRes = Command.CloudResolution;
        const FIntVector CExtent(CRes, CRes, CRes);
        FRDGTextureDesc CAccumDesc = FRDGTextureDesc::Create3D(CExtent, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
        Ctx.CloudInjectionBufferArray = GraphBuilder->CreateTexture(CAccumDesc, TEXT("Swarm.CloudArray.Injection"));
        
        FRDGTextureDesc CPropDesc = FRDGTextureDesc::Create3D(CExtent, PF_R16F, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
        Ctx.CloudPropagateAArray = GraphBuilder->CreateTexture(CPropDesc, TEXT("Swarm.CloudArray.PropagateA"));
        Ctx.CloudPropagateBArray = GraphBuilder->CreateTexture(CPropDesc, TEXT("Swarm.CloudArray.PropagateB"));

        Ctx.bPersistenceRequested = IsPersistenceRequested();
        Ctx.bLatticePersistentValid = PersistentLattice.IsValid();
        Ctx.bCloudPersistentValid = PersistentCloud.IsValid();
        Ctx.bLatticePersistentMatches = DoesPersistentTextureMatch(PersistentLattice, LRes, PF_FloatRGBA);
        Ctx.bCloudPersistentMatches = DoesPersistentTextureMatch(PersistentCloud, CRes, PF_R16F);
        Ctx.PrevLatticeArray = GSystemTextures.GetVolumetricBlackDummy(*GraphBuilder);
        Ctx.CloudReadArray = GSystemTextures.GetVolumetricBlackDummy(*GraphBuilder);

        if (Ctx.bPersistenceRequested && Ctx.bLatticePersistentMatches)
        {
            Ctx.PrevLatticeArray = GraphBuilder->RegisterExternalTexture((IPooledRenderTarget*)PersistentLattice.GetReference(), TEXT("Swarm.LatticeArray.PersistentPrev"));
            Ctx.bLatticePersistenceApplied = true;
        }

        if (Ctx.bPersistenceRequested && Ctx.bCloudPersistentMatches)
        {
            const FRDGTextureRef PrevCloudArray = GraphBuilder->RegisterExternalTexture((IPooledRenderTarget*)PersistentCloud.GetReference(), TEXT("Swarm.CloudArray.PersistentPrev"));
            AddCopyTexturePass(*GraphBuilder, PrevCloudArray, Ctx.CloudPropagateAArray);
            Ctx.CloudReadArray = Ctx.CloudPropagateAArray;
            Ctx.bCloudPersistenceApplied = true;
        }

        Ctx.bPersistenceApplied = Ctx.bLatticePersistenceApplied || Ctx.bCloudPersistenceApplied;

        Ctx.ThreadGroups = FMath::DivideAndRoundUp(NumEntities, 64u);
        Ctx.GridGroups = FMath::DivideAndRoundUp(Flight::Swarm::GridHashSize, 64u);
        Ctx.LatticeGroups = FMath::DivideAndRoundUp(LRes * LRes * LRes, 64u);
        Ctx.CloudGroups = FMath::DivideAndRoundUp(CRes * CRes * CRes, 64u);

        return Ok(Ctx);
    }

    /** Pass: Clear Spatial Grid */
    TResult<bool> AddClearGridPass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightSwarmClearGridCS::FParameters>();
        PassParameters->GridHeadBuffer = Ctx.GraphBuilder->CreateUAV(Ctx.GridHeadBuffer);
		PassParameters->GridCenter = FVector4f(Ctx.GridCenter, 0.0f);
		PassParameters->GridExtent = Ctx.Command.LatticeExtent;
		PassParameters->GridResolution = Ctx.Command.LatticeResolution;

        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightSwarmClearGridCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.ClearGrid"), ComputeShader, PassParameters, FIntVector(Ctx.GridGroups, 1, 1));
        return Ok(true);
    }

    /** Pass: Build Spatial Grid */
    TResult<bool> AddBuildGridPass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightSwarmBuildGridCS::FParameters>();
        PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
        PassParameters->GridHeadBuffer = Ctx.GraphBuilder->CreateUAV(Ctx.GridHeadBuffer);
        PassParameters->GridNextBuffer = Ctx.GraphBuilder->CreateUAV(Ctx.GridNextBuffer);
		PassParameters->GridCenter = FVector4f(Ctx.GridCenter, 0.0f);
		PassParameters->GridExtent = Ctx.Command.LatticeExtent;
		PassParameters->GridResolution = Ctx.Command.LatticeResolution;
        PassParameters->NumEntities = Ctx.NumEntities;

        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightSwarmBuildGridCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.BuildGrid"), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
        return Ok(true);
    }

    /** Radix Sort: Orchestrate 2-pass 4-bit parallel sort by Behavior ClassID (8 bits total) */
    TResult<bool> AddRadixSortPass(FSwarmRDGContext& Ctx)
    {
        uint32 BitOffsets[] = { 16, 20 };
        for (uint32 Pass = 0; Pass < 1; ++Pass) // Start with 1 pass
        {
            // 1. Count
            {
                AddClearUAVPass(*Ctx.GraphBuilder, Ctx.GraphBuilder->CreateUAV(Ctx.SortBitCounts), 0u);
                auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightRadixCountCS::FParameters>();
                PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
                PassParameters->BitCounts = Ctx.GraphBuilder->CreateUAV(Ctx.SortBitCounts);
                PassParameters->NumEntities = Ctx.NumEntities;
                PassParameters->BitOffset = BitOffsets[Pass];
                auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightRadixCountCS>();
                FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Sort.Count.P%u", Pass), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
            }
            // 2. Scan
            {
                auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightRadixScanCS::FParameters>();
                PassParameters->BitCounts = Ctx.GraphBuilder->CreateUAV(Ctx.SortBitCounts);
                PassParameters->Offsets = Ctx.GraphBuilder->CreateUAV(Ctx.SortOffsets);
                PassParameters->NumElements = 16;
                auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightRadixScanCS>();
                FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Sort.Scan.P%u", Pass), ComputeShader, PassParameters, FIntVector(1, 1, 1));
            }
            // 3. Scatter
            {
                auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightRadixScatterCS::FParameters>();
                PassParameters->InDroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
                PassParameters->OutDroidStates = Ctx.GraphBuilder->CreateUAV(Ctx.SortedDroidBuffer);
                PassParameters->Offsets = Ctx.GraphBuilder->CreateUAV(Ctx.SortOffsets); 
                PassParameters->NumEntities = Ctx.NumEntities;
                PassParameters->BitOffset = BitOffsets[Pass];
                auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightRadixScatterCS>();
                FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Sort.Scatter.P%u", Pass), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
            }
            Ctx.DroidBuffer = Ctx.SortedDroidBuffer;
        }
        return Ok(true);
    }

    /** Pass: SPH Density/Pressure */
    TResult<bool> AddDensityPass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightSwarmDensityCS::FParameters>();
        PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
        PassParameters->GridHeadBuffer = Ctx.GraphBuilder->CreateSRV(Ctx.GridHeadBuffer);
        PassParameters->GridNextBuffer = Ctx.GraphBuilder->CreateSRV(Ctx.GridNextBuffer);
        PassParameters->DensityPressureBuffer = Ctx.GraphBuilder->CreateUAV(Ctx.DensityBuffer);
		PassParameters->GridCenter = FVector4f(Ctx.GridCenter, 0.0f);
        PassParameters->PlayerPosRadius = Ctx.Command.PlayerPosRadius;
        PassParameters->BeaconPosStrength = Ctx.Command.BeaconPosStrength;
        PassParameters->SmoothingRadius = Ctx.Command.SmoothingRadius;
        PassParameters->GasConstant = Ctx.Command.GasConstant;
        PassParameters->RestDensity = Ctx.Command.RestDensity;
        PassParameters->Viscosity = Ctx.Command.Viscosity;
        PassParameters->NumEntities = Ctx.NumEntities;
        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightSwarmDensityCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Density"), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
        return Ok(true);
    }

    /** Pass: SPH Force */
    TResult<bool> AddForcePass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightSwarmForceCS::FParameters>();
        PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
        PassParameters->GridHeadBuffer = Ctx.GraphBuilder->CreateSRV(Ctx.GridHeadBuffer);
        PassParameters->GridNextBuffer = Ctx.GraphBuilder->CreateSRV(Ctx.GridNextBuffer);
        PassParameters->DensityPressureBuffer = Ctx.GraphBuilder->CreateSRV(Ctx.DensityBuffer);
        PassParameters->ForceBlackboard = Ctx.GraphBuilder->CreateUAV(Ctx.ForceBuffer);
        PassParameters->DroidStatesRW = Ctx.GraphBuilder->CreateUAV(Ctx.DroidBuffer);
        PassParameters->EventBuffer = Ctx.GraphBuilder->CreateSRV(Ctx.EventBuffer);

        PassParameters->ReadLatticeArray = Ctx.GraphBuilder->CreateSRV(Ctx.PrevLatticeArray);
        PassParameters->ReadLatticeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        PassParameters->OutLatticeRArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionRArray);
        PassParameters->OutLatticeGArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionGArray);
        PassParameters->OutLatticeBArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionBArray);

        PassParameters->ReadCloudArray = Ctx.GraphBuilder->CreateSRV(Ctx.CloudReadArray);
        PassParameters->ReadCloudSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        PassParameters->OutCloudAccumArray = Ctx.GraphBuilder->CreateUAV(Ctx.CloudInjectionBufferArray);

        PassParameters->GridCenter = FVector4f(Ctx.GridCenter, 0.0f);
        PassParameters->GridExtent = Ctx.Command.LatticeExtent;
        PassParameters->GridResolution = Ctx.Command.LatticeResolution;

        PassParameters->PlayerPosRadius = Ctx.Command.PlayerPosRadius;
        PassParameters->BeaconPosStrength = Ctx.Command.BeaconPosStrength;
        PassParameters->SmoothingRadius = Ctx.Command.SmoothingRadius;
        PassParameters->GasConstant = Ctx.Command.GasConstant;
        PassParameters->RestDensity = Ctx.Command.RestDensity;
        PassParameters->Viscosity = Ctx.Command.Viscosity;
        PassParameters->AvoidanceStrength = Ctx.Command.AvoidanceStrength;
        PassParameters->MaxAcceleration = Ctx.Command.MaxAcceleration;
        PassParameters->DeltaTime = Ctx.Command.DeltaTime;
        PassParameters->CommandType = Ctx.Command.CommandType;
        PassParameters->NumEntities = Ctx.NumEntities;
        PassParameters->NumEvents = Ctx.Command.NumEvents;

        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightSwarmForceCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Force"), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
        return Ok(true);
    }

    /** Pass: Predictive Rollout */
    TResult<bool> AddPredictivePass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightSwarmPredictiveCS::FParameters>();
        PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
        PassParameters->ForceBlackboard = Ctx.GraphBuilder->CreateUAV(Ctx.ForceBuffer);
        PassParameters->EventBuffer = Ctx.GraphBuilder->CreateSRV(Ctx.EventBuffer);
        PassParameters->PlayerPosRadius = Ctx.Command.PlayerPosRadius;
        PassParameters->BeaconPosStrength = Ctx.Command.BeaconPosStrength;
        PassParameters->SmoothingRadius = Ctx.Command.SmoothingRadius;
        PassParameters->GasConstant = Ctx.Command.GasConstant;
        PassParameters->RestDensity = Ctx.Command.RestDensity;
        PassParameters->Viscosity = Ctx.Command.Viscosity;
        PassParameters->AvoidanceStrength = Ctx.Command.AvoidanceStrength;
        PassParameters->MaxAcceleration = Ctx.Command.MaxAcceleration;
        PassParameters->DeltaTime = Ctx.Command.DeltaTime;
        PassParameters->CommandType = Ctx.Command.CommandType;
        PassParameters->FrameIndex = Ctx.Command.FrameIndex;
        PassParameters->PredictionHorizon = Ctx.Command.PredictionHorizon;
        PassParameters->NumEntities = Ctx.NumEntities;
        PassParameters->NumEvents = Ctx.Command.NumEvents;

        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightSwarmPredictiveCS>();

        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Predictive"), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
        return Ok(true);
    }

    /** Pass: Integration */
    TResult<bool> AddIntegrationPass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightSwarmIntegrationCS::FParameters>();
        PassParameters->ForceBlackboard = Ctx.GraphBuilder->CreateSRV(Ctx.ForceBuffer);
        PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
        PassParameters->DroidStatesRW = Ctx.GraphBuilder->CreateUAV(Ctx.DroidBuffer);
        PassParameters->DeltaTime = Ctx.Command.DeltaTime;
        PassParameters->NumEntities = Ctx.NumEntities;
        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightSwarmIntegrationCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Integrate"), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
        return Ok(true);
    }

    /** SCSL Pass: Light Injection */
    TResult<bool> AddLightInjectionPass(FSwarmRDGContext& Ctx)
    {
        AddClearUAVPass(*Ctx.GraphBuilder, Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionRArray), 0u);
        AddClearUAVPass(*Ctx.GraphBuilder, Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionGArray), 0u);
        AddClearUAVPass(*Ctx.GraphBuilder, Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionBArray), 0u);
        
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightLightInjectionCS::FParameters>();
        PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
        PassParameters->OutLatticeRArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionRArray);
        PassParameters->OutLatticeGArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionGArray);
        PassParameters->OutLatticeBArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticeInjectionBArray);
        PassParameters->GridCenter = FVector4f(Ctx.GridCenter, 0.0f);
        PassParameters->GridExtent = Ctx.Command.LatticeExtent;
        PassParameters->GridResolution = Ctx.Command.LatticeResolution;
        PassParameters->NumEntities = Ctx.NumEntities;
        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightLightInjectionCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Light.Inject"), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
        return Ok(true);
    }

    /** SCSL Pass: Light Convert (Fixed to Float) */
    TResult<bool> AddLightConvertPass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightLightConvertCS::FParameters>();
        PassParameters->InLatticeRArray = Ctx.GraphBuilder->CreateSRV(Ctx.LatticeInjectionRArray);
        PassParameters->InLatticeGArray = Ctx.GraphBuilder->CreateSRV(Ctx.LatticeInjectionGArray);
        PassParameters->InLatticeBArray = Ctx.GraphBuilder->CreateSRV(Ctx.LatticeInjectionBArray);
        PassParameters->PrevLatticeArray = Ctx.GraphBuilder->CreateSRV(Ctx.PrevLatticeArray);
        PassParameters->OutLatticeArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticePropagateAArray);
        PassParameters->bUsePrevLattice = (Ctx.bPersistenceRequested && Ctx.bLatticePersistentMatches) ? 1u : 0u;
        PassParameters->GridResolution = Ctx.Command.LatticeResolution;
        
        FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Ctx.Command.LatticeResolution, Ctx.Command.LatticeResolution, Ctx.Command.LatticeResolution), FIntVector(4, 4, 4));
        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightLightConvertCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Light.Convert"), ComputeShader, PassParameters, GroupCount);
        return Ok(true);
    }

    /** SCSL Pass: Light Propagation (Diffusion) */
    TResult<bool> AddLightPropagationPass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightLightPropagationCS::FParameters>();
        PassParameters->ReadLatticeArray = Ctx.GraphBuilder->CreateSRV(Ctx.LatticePropagateAArray);
        PassParameters->WriteLatticeArray = Ctx.GraphBuilder->CreateUAV(Ctx.LatticePropagateBArray);
        PassParameters->DecayRate = Ctx.Command.LatticeDecay;
        PassParameters->PropagationStrength = Ctx.Command.LatticePropStrength;
        PassParameters->GridResolution = Ctx.Command.LatticeResolution;
        FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Ctx.Command.LatticeResolution, Ctx.Command.LatticeResolution, Ctx.Command.LatticeResolution), FIntVector(4, 4, 4));
        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightLightPropagationCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Light.Propagate"), ComputeShader, PassParameters, GroupCount);
        return Ok(true);
    }

    /** SCSL Pass: Cloud Injection */
    TResult<bool> AddCloudInjectionPass(FSwarmRDGContext& Ctx)
    {
        AddClearUAVPass(*Ctx.GraphBuilder, Ctx.GraphBuilder->CreateUAV(Ctx.CloudInjectionBufferArray), 0u);
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightCloudInjectionCS::FParameters>();
        PassParameters->DroidStates = Ctx.GraphBuilder->CreateSRV(Ctx.DroidBuffer);
        PassParameters->OutCloudAccumArray = Ctx.GraphBuilder->CreateUAV(Ctx.CloudInjectionBufferArray);
        PassParameters->GridCenter = FVector4f(Ctx.GridCenter, 0.0f);
        PassParameters->GridExtent = Ctx.Command.CloudExtent;
        PassParameters->GridResolution = Ctx.Command.CloudResolution;
        PassParameters->NumEntities = Ctx.NumEntities;
        PassParameters->InjectionAmount = Ctx.Command.CloudInjection;
        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightCloudInjectionCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Cloud.Inject"), ComputeShader, PassParameters, FIntVector(Ctx.ThreadGroups, 1, 1));
        return Ok(true);
    }

    /** SCSL Pass: Cloud Simulation */
    TResult<bool> AddCloudSimPass(FSwarmRDGContext& Ctx)
    {
        auto* PassParameters = Ctx.GraphBuilder->AllocParameters<FFlightCloudSimCS::FParameters>();
        PassParameters->InCloudAccumArray = Ctx.GraphBuilder->CreateSRV(Ctx.CloudInjectionBufferArray);
        PassParameters->ReadCloudArray = Ctx.GraphBuilder->CreateSRV(Ctx.CloudReadArray);
        PassParameters->WriteCloudArray = Ctx.GraphBuilder->CreateUAV(Ctx.CloudPropagateBArray);
        PassParameters->DecayRate = Ctx.Command.CloudDecay;
        PassParameters->DiffusionStrength = Ctx.Command.CloudDiffusion;
        PassParameters->GridResolution = Ctx.Command.CloudResolution;
        FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(Ctx.Command.CloudResolution, Ctx.Command.CloudResolution, Ctx.Command.CloudResolution), FIntVector(4, 4, 4));
        auto ComputeShader = Ctx.ShaderMap->GetShader<FFlightCloudSimCS>();
        FComputeShaderUtils::AddPass(*Ctx.GraphBuilder, RDG_EVENT_NAME("Swarm.Cloud.Sim"), ComputeShader, PassParameters, GroupCount);
        return Ok(true);
	}
#endif
}

#include "Swarm/FlightSwarmRender.h"

void UFlightSwarmSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("FlightSwarmSubsystem: Initializing Reactive GPU Swarm Infrastructure"));
	
	if (!IsRunningCommandlet())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<Flight::Swarm::FSwarmSceneViewExtension>();
	}
}

void UFlightSwarmSubsystem::Deinitialize()
{
	FlushRenderingCommands();
	if (DroidStateBuffer.IsValid()) DroidStateBuffer.SafeRelease();
	if (LatticeTexture.IsValid()) LatticeTexture.SafeRelease();
	if (CloudTexture.IsValid()) CloudTexture.SafeRelease();
	if (ViewExtension.IsValid()) ViewExtension.Reset();
	Super::Deinitialize();
}

void UFlightSwarmSubsystem::InitializeSwarm(int32 NumEntities)
{
	TotalEntities = NumEntities;
	const FString ResourceId = TEXT("Swarm.DroidStateBuffer");
	const uint32 NativeStrideBytes = sizeof(FDroidState);
	const uint32 NativeLayoutHash = Flight::Reflection::HLSL::ComputeLayoutHash<FDroidState>();
	const TArray<FString> ContractIssues = Flight::Schema::ValidateStructuredBufferContract(ResourceId, NativeStrideBytes, NativeLayoutHash);

	DroidStateResourceId = ResourceId;
	DroidStateElementStrideBytes = NativeStrideBytes;

	if (const TOptional<Flight::Schema::FFlightGpuStructuredBufferContract> Contract = Flight::Schema::ResolveStructuredBufferContract(ResourceId))
	{
		if (Contract->ElementStrideBytes == NativeStrideBytes)
		{
			DroidStateElementStrideBytes = Contract->ElementStrideBytes;
		}

		if (!Contract->bPreferUnrealRdg)
		{
			UE_LOG(LogFlightSwarm, Warning, TEXT("Swarm GPU contract '%s' does not prefer Unreal RDG, but the current swarm path still realizes it through RDG/RHI"), *ResourceId);
		}

		if (Contract->bRequiresRawVulkanInterop)
		{
			UE_LOG(LogFlightSwarm, Warning, TEXT("Swarm GPU contract '%s' requests raw Vulkan interop, but the current swarm state buffer path remains RDG/RHI-owned"), *ResourceId);
		}
	}

	for (const FString& Issue : ContractIssues)
	{
		UE_LOG(LogFlightSwarm, Warning, TEXT("Swarm GPU contract validation: %s"), *Issue);
	}

	const FString BufferDebugName = DroidStateResourceId;
	const uint32 EffectiveStrideBytes = DroidStateElementStrideBytes;
	TWeakObjectPtr<UFlightSwarmSubsystem> WeakThis(this);
	ENQUEUE_RENDER_COMMAND(InitializeDroidBuffer)([WeakThis, NumEntities, EffectiveStrideBytes, BufferDebugName](FRHICommandListImmediate& RHICmdList)
	{
		UFlightSwarmSubsystem* Self = WeakThis.Get();
		if (!Self) return;
		FRHIBufferCreateDesc Desc = FRHIBufferCreateDesc::CreateStructured(*BufferDebugName, EffectiveStrideBytes * NumEntities, EffectiveStrideBytes);
		Desc.AddUsage(BUF_ShaderResource | BUF_UnorderedAccess);
		Desc.SetInitialState(ERHIAccess::SRVMask);
		FBufferRHIRef DroidRHI = RHICmdList.CreateBuffer(Desc);
		TArray<FDroidState> InitialData; InitialData.SetNumUninitialized(NumEntities);
		for (int32 i = 0; i < NumEntities; ++i) { InitialData[i].Position = FVector3f(FMath::RandRange(-5000.f, 5000.f), FMath::RandRange(-5000.f, 5000.f), FMath::RandRange(1000.f, 5000.f)); InitialData[i].Velocity = FVector3f::ZeroVector; InitialData[i].Shield = 1.0f; InitialData[i].Status = 0; }
		void* BufferData = RHICmdList.LockBuffer(DroidRHI, 0, NumEntities * sizeof(FDroidState), RLM_WriteOnly);
		FMemory::Memcpy(BufferData, InitialData.GetData(), NumEntities * sizeof(FDroidState));
		RHICmdList.UnlockBuffer(DroidRHI);
		FRDGBufferDesc RDGDesc = FRDGBufferDesc::CreateStructuredDesc(EffectiveStrideBytes, NumEntities);
		Self->DroidStateBuffer = new FRDGPooledBuffer(DroidRHI, RDGDesc, NumEntities, *BufferDebugName);
		});
}

FFlightSwarmPersistenceDebugSnapshot UFlightSwarmSubsystem::GetLastPersistenceDebugSnapshot() const
{
	FScopeLock Lock(&PersistenceSnapshotMutex);
	return LastPersistenceSnapshot;
}

void UFlightSwarmSubsystem::SetLastPersistenceDebugSnapshot_RenderThread(const FFlightSwarmPersistenceDebugSnapshot& Snapshot)
{
	FScopeLock Lock(&PersistenceSnapshotMutex);
	LastPersistenceSnapshot = Snapshot;
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
	Cmd.LatticeResolution = static_cast<uint32>(LatticeResolution.Get());
	Cmd.LatticeExtent = LatticeExtent.Get();
	Cmd.LatticeDecay = LatticeDecay.Get();
	Cmd.LatticePropStrength = LatticePropStrength.Get();
	Cmd.CloudResolution = static_cast<uint32>(CloudResolution.Get());
	Cmd.CloudExtent = CloudExtent.Get();
	Cmd.CloudDecay = CloudDecay.Get();
	Cmd.CloudDiffusion = CloudDiffusion.Get();
	Cmd.CloudInjection = CloudInjection.Get();
    Cmd.NumEvents = 0; 
	return Cmd;
}

void UFlightSwarmSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (GetWorld()->IsGameWorld() || GetWorld()->WorldType == EWorldType::PIE) TickSimulation(DeltaTime);
}

TStatId UFlightSwarmSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UFlightSwarmSubsystem, STATGROUP_Tickables); }

void UFlightSwarmSubsystem::TickSimulation(float DeltaTime)
{
#if WITH_FLIGHT_COMPUTE_SHADERS
	if (TotalEntities <= 0) return;
	if (!FApp::CanEverRender() || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5) return;
	FSwarmGlobalCommand PackedCmd = GetPackedCommand(DeltaTime);
	TWeakObjectPtr<UFlightSwarmSubsystem> WeakThis(this);
	ENQUEUE_RENDER_COMMAND(FlightSwarmTick)([WeakThis, PackedCmd, NumEntities = (uint32)TotalEntities](FRHICommandListImmediate& RHICmdList)
	{
		UFlightSwarmSubsystem* Self = WeakThis.Get();
		if (!Self || !Self->DroidStateBuffer.IsValid() || IsRunningCommandlet()) return;
		FRDGBuilder GraphBuilder(RHICmdList);
		auto PipelineResult = CheckShaders(GetGlobalShaderMap(GMaxRHIFeatureLevel))
			.AndThen([&](FGlobalShaderMap* ShaderMap) { return InitResources(&GraphBuilder, ShaderMap, PackedCmd, NumEntities, Self->DroidStateElementStrideBytes, Self->DroidStateBuffer, Self->LatticeTexture, Self->CloudTexture, Self->RequiredSort); })
			.AndThen([&](FSwarmRDGContext Ctx)
			{
				FFlightSwarmPersistenceDebugSnapshot Snapshot;
				Snapshot.FrameIndex = PackedCmd.FrameIndex;
				Snapshot.RequestedMode = Ctx.bPersistenceRequested ? EFlightSwarmPersistenceMode::Persistent : EFlightSwarmPersistenceMode::Stateless;
				Snapshot.AppliedMode = Ctx.bPersistenceApplied ? EFlightSwarmPersistenceMode::Persistent : EFlightSwarmPersistenceMode::Stateless;
				Snapshot.bLatticePersistentValid = Ctx.bLatticePersistentValid;
				Snapshot.bCloudPersistentValid = Ctx.bCloudPersistentValid;
				Snapshot.bLatticePersistentMatches = Ctx.bLatticePersistentMatches;
				Snapshot.bCloudPersistentMatches = Ctx.bCloudPersistentMatches;
				Snapshot.bUsedPriorLatticeFrame = Ctx.bLatticePersistenceApplied;
				Snapshot.bUsedPriorCloudFrame = Ctx.bCloudPersistenceApplied;
				Snapshot.LatticeResolution = Ctx.Command.LatticeResolution;
				Snapshot.LatticeExtent = Ctx.Command.LatticeExtent;
				Snapshot.CloudResolution = Ctx.Command.CloudResolution;
				Snapshot.CloudExtent = Ctx.Command.CloudExtent;
				Self->SetLastPersistenceDebugSnapshot_RenderThread(Snapshot);

				static FString LastModeKey;
				const FString CurrentModeKey = FString::Printf(
					TEXT("%u|%u|%d|%d|%d|%d"),
					Snapshot.LatticeResolution,
					Snapshot.CloudResolution,
					static_cast<int32>(Snapshot.RequestedMode),
					static_cast<int32>(Snapshot.AppliedMode),
					Snapshot.bLatticePersistentMatches ? 1 : 0,
					Snapshot.bCloudPersistentMatches ? 1 : 0);

				if (CurrentModeKey != LastModeKey)
				{
					LastModeKey = CurrentModeKey;
					UE_LOG(
						LogFlightSwarm,
						Display,
						TEXT("Swarm persistence mode: Requested=%s Applied=%s LatticeValid=%d LatticeMatch=%d CloudValid=%d CloudMatch=%d LatticeRes=%u LatticeExtent=%.2f CloudRes=%u CloudExtent=%.2f"),
						Snapshot.RequestedMode == EFlightSwarmPersistenceMode::Persistent ? TEXT("Persistent") : TEXT("Stateless"),
						Snapshot.AppliedMode == EFlightSwarmPersistenceMode::Persistent ? TEXT("Persistent") : TEXT("Stateless"),
						Snapshot.bLatticePersistentValid ? 1 : 0,
						Snapshot.bLatticePersistentMatches ? 1 : 0,
						Snapshot.bCloudPersistentValid ? 1 : 0,
						Snapshot.bCloudPersistentMatches ? 1 : 0,
						Snapshot.LatticeResolution,
						Snapshot.LatticeExtent,
						Snapshot.CloudResolution,
						Snapshot.CloudExtent);
				}

					return Ok(Ctx);
				})
				.AndThen([](FSwarmRDGContext Ctx) { return AddClearGridPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddBuildGridPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { 
				if (Ctx.RequiredSort != ESwarmSortRequirement::None)
				{
					return AddRadixSortPass(Ctx).Map([Ctx](bool) { return Ctx; }); 
				}
				return Ok(Ctx);
			})
			.AndThen([](FSwarmRDGContext Ctx) { return AddDensityPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddForcePass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddPredictivePass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddIntegrationPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddLightInjectionPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddLightConvertPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddLightPropagationPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddCloudInjectionPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([](FSwarmRDGContext Ctx) { return AddCloudSimPass(Ctx).Map([Ctx](bool) { return Ctx; }); })
			.AndThen([&](FSwarmRDGContext Ctx) {
				GraphBuilder.QueueTextureExtraction(Ctx.LatticePropagateBArray, (TRefCountPtr<IPooledRenderTarget>*)&Self->LatticeTexture, ERHIAccess::SRVMask);
				GraphBuilder.QueueTextureExtraction(Ctx.CloudPropagateBArray, (TRefCountPtr<IPooledRenderTarget>*)&Self->CloudTexture, ERHIAccess::SRVMask);
				return Ok(true);
			});
		if (PipelineResult.IsErr()) { static TSet<FString> LoggedErrors; FString ErrMsg = PipelineResult.GetError(); if (!LoggedErrors.Contains(ErrMsg)) { LoggedErrors.Add(ErrMsg); UE_LOG(LogTemp, Warning, TEXT("FlightSwarmSubsystem: Monadic RDG Pipeline failed.")); } }
		GraphBuilder.Execute();
		if (Self->ViewExtension.IsValid())
		{
			Self->ViewExtension->UpdateSwarmData_RenderThread(
				Self->DroidStateBuffer,
				NumEntities,
				Self->LatticeTexture,
				Self->CloudTexture,
				FVector3f::ZeroVector,
				PackedCmd.LatticeExtent,
				PackedCmd.LatticeResolution,
				FVector3f::ZeroVector,
				PackedCmd.CloudExtent,
				PackedCmd.CloudResolution);
		}
	});
	RequiredSort = ESwarmSortRequirement::None;
	#endif
}

void UFlightSwarmSubsystem::UpdateDroidState_Sparse(int32 StartIndex, const TArrayView<const FDroidState>& Data)
{
	if (!DroidStateBuffer.IsValid() || Data.Num() == 0) return;

	TWeakObjectPtr<UFlightSwarmSubsystem> WeakThis(this);
	TArray<FDroidState> DataCopy(Data.GetData(), Data.Num());

	ENQUEUE_RENDER_COMMAND(UpdateDroidBufferSparse)([WeakThis, StartIndex, DataCopy = MoveTemp(DataCopy)](FRHICommandListImmediate& RHICmdList)
	{
		UFlightSwarmSubsystem* Self = WeakThis.Get();
		if (!Self || !Self->DroidStateBuffer.IsValid()) return;

		FRHIBuffer* BufferRHI = Self->DroidStateBuffer->GetRHI();
		const uint32 Offset = StartIndex * sizeof(FDroidState);
		const uint32 Size = DataCopy.Num() * sizeof(FDroidState);

		void* Dest = RHICmdList.LockBuffer(BufferRHI, Offset, Size, RLM_WriteOnly);
		FMemory::Memcpy(Dest, DataCopy.GetData(), Size);
		RHICmdList.UnlockBuffer(BufferRHI);
	});
}
