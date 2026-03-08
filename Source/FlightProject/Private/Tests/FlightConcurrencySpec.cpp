// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Verse/UFlightVexTaskSubsystem.h"
#include "FlightTestUtils.h"

BEGIN_DEFINE_SPEC(FFlightConcurrencySpec, "FlightProject.Integration.Concurrency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	UFlightVexTaskSubsystem* TaskSubsystem;
END_DEFINE_SPEC(FFlightConcurrencySpec)

void FFlightConcurrencySpec::Define()
{
	BeforeEach([this]() {
		TaskSubsystem = GEngine->GetWorldContexts()[0].World()->GetSubsystem<UFlightVexTaskSubsystem>();
	});

	Describe("Task Graph Integration (@job)", [this]() {
		It("should execute a VEX job asynchronously and wait for completion", [this]() {
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
		It("should handle task suspension via placeholders", [this]() {
			// This would test the WaitOnGpu_Thunk logic
			// Requires full Verse VM setup which is usually a functional/integration test
		});
	});
}
