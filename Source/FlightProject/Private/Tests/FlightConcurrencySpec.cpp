// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Verse/UFlightVexTaskSubsystem.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "FlightTestUtils.h"

namespace
{
	UWorld* FindAutomationWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
			{
				return Context.World();
			}
		}

		return nullptr;
	}
}

BEGIN_DEFINE_SPEC(FFlightConcurrencySpec, "FlightProject.Integration.Concurrency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	UFlightVexTaskSubsystem* TaskSubsystem;
	UFlightVerseSubsystem* VerseSubsystem;
END_DEFINE_SPEC(FFlightConcurrencySpec)

void FFlightConcurrencySpec::Define()
{
	BeforeEach([this]() {
		UWorld* World = FindAutomationWorld();
		TaskSubsystem = World ? World->GetSubsystem<UFlightVexTaskSubsystem>() : nullptr;
		VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;
	});

	Describe("Task Graph Integration (@job)", [this]() {
		It("should execute a VEX job asynchronously and wait for completion", [this]() {
			if (!TaskSubsystem)
			{
				AddError(TEXT("Task subsystem is unavailable"));
				return;
			}

			bool bJobDone = false;
			
			// 1. Launch a mock job
			UE::Tasks::FTask Job = TaskSubsystem->LaunchVexJob(1, 0, [&bJobDone]() {
				FPlatformProcess::Sleep(0.1f); // Simulate work
				bJobDone = true;
			});

			// 2. Latent wait for completion
			// In a Spec test, we can use LatentBeforeEach/LatentIt or just wait
			Job.Wait();
			
			TestTrue("Job completed", bJobDone);
		});
	});

	Describe("Verse Leniency (@async)", [this]() {
		It("should track async compile state and gate non-executable behavior", [this]() {
			if (!VerseSubsystem)
			{
				AddError(TEXT("Verse subsystem is unavailable"));
				return;
			}

			const uint32 BehaviorID = 9100;
			const FString AsyncSource = TEXT("@gpu @async { @velocity = @position; @shield = @shield; @status = @status; }");
			FString OutErrors;
			const bool bCompiled = VerseSubsystem->CompileVex(BehaviorID, AsyncSource, OutErrors);

			TestFalse(TEXT("Async behavior should not be executable until VM compile path is implemented"), bCompiled);
			TestEqual(TEXT("Compile state should be GeneratedOnly"), VerseSubsystem->GetBehaviorCompileState(BehaviorID), EFlightVerseCompileState::GeneratedOnly);
			TestFalse(TEXT("Behavior should not be executable"), VerseSubsystem->HasExecutableBehavior(BehaviorID));
			TestTrue(TEXT("Diagnostics should report non-executable state"), OutErrors.Contains(TEXT("not executable")));

			Flight::Swarm::FDroidState DroidState{};
			DroidState.Position = FVector3f(1.0f, 2.0f, 3.0f);
			DroidState.Velocity = FVector3f(4.0f, 5.0f, 6.0f);
			DroidState.Shield = 7.0f;
			DroidState.Status = 8;

			const Flight::Swarm::FDroidState Baseline = DroidState;
			VerseSubsystem->ExecuteBehavior(BehaviorID, DroidState);
			TestEqual(TEXT("Non-executable behavior should not mutate state"), DroidState.Position, Baseline.Position);
			TestEqual(TEXT("Non-executable behavior should not mutate velocity"), DroidState.Velocity, Baseline.Velocity);
			TestEqual(TEXT("Non-executable behavior should not mutate shield"), DroidState.Shield, Baseline.Shield);
			TestEqual(TEXT("Non-executable behavior should not mutate status"), DroidState.Status, Baseline.Status);
		});
	});
}
