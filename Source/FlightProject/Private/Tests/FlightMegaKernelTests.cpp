// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Orchestration/FlightOrchestrationSubsystem.h"
#include "Orchestration/FlightMegaKernelSubsystem.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexSchema.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMegaKernelSynthesisTest,
	"FlightProject.Integration.Orchestration.MegaKernel.Synthesis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMegaKernelSynthesisTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
			{
				World = Context.World();
				if (World) break;
			}
		}
	}

	if (!World)
	{
		return false;
	}

	UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>();
	UFlightMegaKernelSubsystem* MegaKernel = World->GetSubsystem<UFlightMegaKernelSubsystem>();
	UFlightVerseSubsystem* Verse = World->GetSubsystem<UFlightVerseSubsystem>();

	if (!Orchestration || !MegaKernel || !Verse)
	{
		return false;
	}

	// 0. Ensure schema is registered
	Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::Register();

	// 1. Register a GPU-bound behavior
	const uint32 BehaviorID = 101;
	const FString GpuSource = TEXT("@gpu\n@velocity = @position * 2.0;\n");
	
	FString Errors;
	Verse->CompileVex(BehaviorID, GpuSource, Errors, Flight::Vex::TTypeVexRegistry<Flight::Swarm::FDroidState>::GetTypeKey());
	
	// 1.5 Register a cohort that prefers this behavior
	Flight::Orchestration::FFlightCohortRecord Cohort;
	Cohort.Name = TEXT("Test.Gpu.Cohort");
	Cohort.Tags.Add(TEXT("Swarm"));
	Cohort.PreferredBehaviorId = BehaviorID;
	Orchestration->RegisterCohort(Cohort);

	// 2. Trigger orchestration rebuild (which calls RebuildExecutionPlan)
	Orchestration->RebuildExecutionPlan();

	// 3. Verify synthesis was triggered and produced HLSL
	const FString& Source = MegaKernel->GetMegaKernelSource();
	
	TestTrue(TEXT("Mega-Kernel should contain generation header"), Source.Contains(TEXT("Automatically generated")));
	TestTrue(TEXT("Mega-Kernel should contain VEX_MEGA_KERNEL_DEFINED"), Source.Contains(TEXT("#define VEX_MEGA_KERNEL_DEFINED")));
	TestTrue(TEXT("Mega-Kernel should contain ExecuteVexBehavior function"), Source.Contains(TEXT("void ExecuteVexBehavior")));
	
	// Verify Call Contract parameters
	TestTrue(TEXT("Mega-Kernel should contain pos parameter"), Source.Contains(TEXT("inout float3 pos")));
	TestTrue(TEXT("Mega-Kernel should contain vel parameter"), Source.Contains(TEXT("inout float3 vel")));
	
	// Verify behavior logic
	TestTrue(TEXT("Mega-Kernel should contain case for behavior"), Source.Contains(TEXT("case 101:")));
	TestTrue(TEXT("Mega-Kernel should contain store-back for velocity"), Source.Contains(TEXT("vel = velocity_local;")));

	// 4. Verify file was exported to the generated directory
	const FString ExpectedPath = FPaths::ProjectIntermediateDir() / TEXT("Shaders/Generated/VexMegaKernel.ush");
	TestTrue(TEXT("Synthesized shader file should exist on disk"), IFileManager::Get().FileExists(*ExpectedPath));

	return true;
}

#endif
