// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Orchestration/FlightMegaKernelSubsystem.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Orchestration/FlightExecutionPlan.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/Frontend/VexParser.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "FlightProject.h"
#include "FlightLogCategories.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

void UFlightMegaKernelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	Collection.InitializeDependency<UFlightOrchestrationSubsystem>();
	Collection.InitializeDependency<UFlightVerseSubsystem>();

	// Register the virtual shader directory for generated source
	const FString GeneratedDir = FPaths::ProjectIntermediateDir() / TEXT("Shaders/Generated/");
	IFileManager::Get().MakeDirectory(*GeneratedDir, true);

	if (!IsRunningCommandlet())
	{
		if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/FlightProject/Generated")))
		{
			AddShaderSourceDirectoryMapping(TEXT("/FlightProject/Generated"), GeneratedDir);
		}
	}

	if (UFlightOrchestrationSubsystem* Orchestration = GetWorld()->GetSubsystem<UFlightOrchestrationSubsystem>())
	{
		Orchestration->OnExecutionPlanUpdated.AddUObject(this, &UFlightMegaKernelSubsystem::OnExecutionPlanUpdated);
	}
}

void UFlightMegaKernelSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>()) 
		{
			Orchestration->OnExecutionPlanUpdated.RemoveAll(this);
		}
	}
	Super::Deinitialize();
}

void UFlightMegaKernelSubsystem::Synthesize()
{
	using namespace Flight::Orchestration;
	using namespace Flight::Vex;

	UWorld* World = GetWorld();
	UFlightOrchestrationSubsystem* Orchestration = World ? World->GetSubsystem<UFlightOrchestrationSubsystem>() : nullptr;
	UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;

	if (!Orchestration || !VerseSubsystem)
	{
		return;
	}

	const FFlightExecutionPlan& Plan = Orchestration->GetExecutionPlan();
	LastPlanGeneration = Plan.Generation;

	TMap<uint32, FVexProgramAst> GpuScripts;
	
	// Identify GPU-bound behaviors from the execution plan
	for (const FFlightExecutionPlanStep& Step : Plan.Steps)
	{
		if (Step.ExecutionDomain == EFlightExecutionDomain::Gpu)
		{
			const UFlightVerseSubsystem::FVerseBehavior* Behavior = VerseSubsystem->Behaviors.Find(Step.BehaviorID);
			if (Behavior)
			{
				GpuScripts.Add(Step.BehaviorID, Behavior->NativeProgram);
			}
		}
	}

	if (GpuScripts.Num() == 0)
	{
		MegaKernelSource = TEXT("// No GPU-bound behaviors in current execution plan.\n");
		return;
	}

	UE_LOG(LogFlightProject, Log, TEXT("MegaKernel: Synthesizing unified source for %d behaviors (Plan Gen %u)"), GpuScripts.Num(), LastPlanGeneration);

	// Phase 3: Bridge to Virtual Shader File
	// We map the logical VEX symbols to the stable parameter names in our Call Contract.
	TMap<FString, FString> SymbolMap;
	SymbolMap.Add(TEXT("@position"), TEXT("pos"));
	SymbolMap.Add(TEXT("@velocity"), TEXT("vel"));
	SymbolMap.Add(TEXT("@shield"), TEXT("shield"));
	SymbolMap.Add(TEXT("@status"), TEXT("status"));
	SymbolMap.Add(TEXT("@dt"), TEXT("dt"));
	SymbolMap.Add(TEXT("@frame"), TEXT("frame"));

	// Call the lowering logic with our stable function name
	MegaKernelSource = LowerMegaKernel(GpuScripts, TArray<FVexSymbolDefinition>(), SymbolMap, TEXT("ExecuteVexBehavior"));

	// Write the synthesized source to the Intermediate generated directory
	const FString GeneratedDir = FPaths::ProjectIntermediateDir() / TEXT("Shaders/Generated/");
	IFileManager::Get().MakeDirectory(*GeneratedDir, true);
	
	const FString FilePath = GeneratedDir / TEXT("VexMegaKernel.ush");
	if (FFileHelper::SaveStringToFile(MegaKernelSource, *FilePath))
	{
		UE_LOG(LogFlightProject, Log, TEXT("MegaKernel: Exported synthesized source to %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogFlightProject, Error, TEXT("MegaKernel: Failed to write synthesized source to %s"), *FilePath);
	}
}

void UFlightMegaKernelSubsystem::OnExecutionPlanUpdated()
{
	Synthesize();
}
