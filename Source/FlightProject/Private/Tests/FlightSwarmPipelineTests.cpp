// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Latent command to tick the swarm simulation and verify progress
 */
class FVerifySwarmSimulationCommand : public IAutomationLatentCommand
{
public:
	FVerifySwarmSimulationCommand(UFlightSwarmSubsystem* InSubsystem, int32 InTargetTicks)
		: Subsystem(InSubsystem)
		, TargetTicks(InTargetTicks)
		, CurrentTicks(0)
	{}

	virtual bool Update() override
	{
		if (!Subsystem) return true;

		// Simulate 16ms delta time per tick
		Subsystem->TickSimulation(0.016f);
		CurrentTicks++;

		if (CurrentTicks >= TargetTicks)
		{
			UE_LOG(LogTemp, Display, TEXT("Swarm Pipeline Test: Completed %d simulation ticks successfully."), CurrentTicks);
			return true;
		}

		return false;
	}

private:
	UFlightSwarmSubsystem* Subsystem;
	int32 TargetTicks;
	int32 CurrentTicks;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightSwarmFullPipelineTest, "FlightProject.Swarm.Pipeline.FullIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightSwarmFullPipelineTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
			{
				World = Context.World();
				break;
			}
		}
	}

	if (!World)
	{
		AddError(TEXT("Could not find valid World for Swarm Pipeline test."));
		return false;
	}

	UFlightSwarmSubsystem* SwarmSubsystem = World->GetSubsystem<UFlightSwarmSubsystem>();
	if (!SwarmSubsystem)
	{
		AddError(TEXT("FlightSwarmSubsystem not found."));
		return false;
	}

	// 1. Initialize Swarm (50,000 for standard integration test, 500k is for benchmarks)
	const int32 EntityCount = 50000;
	SwarmSubsystem->InitializeSwarm(EntityCount);
	
	TestEqual("Subsystem should report correct entity count", SwarmSubsystem->GetNumEntities(), EntityCount);

	// 2. Set some reactive parameters
	SwarmSubsystem->SmoothingRadius = 600.0f;
	SwarmSubsystem->PredictionHorizon = 15;
	SwarmSubsystem->PlayerPosition = FVector(1000, 1000, 1000);

	// 3. Queue latent simulation ticks
	// This verifies the 6-pass RDG pipeline executes without crashes or GPU hangs
	ADD_LATENT_AUTOMATION_COMMAND(FVerifySwarmSimulationCommand(SwarmSubsystem, 10));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
