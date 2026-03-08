// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "IoUring/FlightGpuPerceptionSubsystem.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "Spatial/FlightGpuPerceptionField.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Tests/AutomationCommon.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS

// ============================================================================
// Logic Path Verification
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightGpuPerceptionTest, "FlightProject.Spatial.GpuPerception", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightGpuPerceptionTest::RunTest(const FString& Parameters)
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
		AddError(TEXT("Could find no valid World for GpuPerception test."));
		return false;
	}

	UFlightGpuPerceptionSubsystem* Perception = World->GetSubsystem<UFlightGpuPerceptionSubsystem>();
	if (!Perception)
	{
		AddError(TEXT("FlightGpuPerceptionSubsystem not found."));
		return false;
	}

	const bool bShouldHaveGpu = FApp::CanEverRender() && (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5);
	const bool bIsAvailable = Perception->IsAvailable();

	if (!bIsAvailable)
	{
		if (bShouldHaveGpu)
		{
			AddError(TEXT("GPU Perception expected but not available in this RHI context (SM5+ required)."));
			return false;
		}
		else
		{
			AddInfo(TEXT("Environment does not support GPU rendering. Skipping async validation."));
			using namespace Flight::Spatial;
			FGpuPerceptionField PerceptionField(World);
			TestEqual(TEXT("Field Name should be GpuPerception"), PerceptionField.GetFieldName(), FName(TEXT("GpuPerception")));
			return true;
		}
	}

	// Basic Verification
	FFlightPerceptionRequest Request;
	Request.EntityPositions.Add(FVector4f(0, 0, 0, 1000.0f));
	
	int64 RequestId = Perception->SubmitPerceptionRequest(Request, [](const FFlightPerceptionResult& Result) {});

	TestTrue(TEXT("Request ID should be valid"), RequestId > 0);
	TestTrue(TEXT("Pending count should be > 0"), Perception->GetPendingCount() > 0);

	return true;
}

// ============================================================================
// Benchmarking Path
// ============================================================================

class FFlightGpuPerceptionCompletionCommand : public IAutomationLatentCommand
{
public:
	FFlightGpuPerceptionCompletionCommand(
		FAutomationTestBase* InTest,
		UWorld* InWorld)
		: Test(InTest)
		, World(InWorld)
	{}

	virtual bool Update() override
	{
		if (!Test)
		{
			return true;
		}

		if (!bStarted)
		{
			bStarted = true;
			StartTime = FPlatformTime::Seconds();

			if (!World.IsValid())
			{
				Test->AddError(TEXT("GpuPerception completion test: invalid world."));
				return true;
			}

			Perception = World->GetSubsystem<UFlightGpuPerceptionSubsystem>();
			IoUringSubsystem = World->GetSubsystem<UFlightIoUringSubsystem>();

			if (!Perception || !Perception->IsAvailable())
			{
				Test->AddInfo(TEXT("GpuPerception completion test skipped: subsystem unavailable."));
				return true;
			}

			FFlightPerceptionRequest Request;
			Request.EntityPositions.Add(FVector4f(0, 0, 0, 500.0f));
			Request.ObstacleMinBounds.Add(FVector4f(-50, -50, -50, 0));
			Request.ObstacleMaxBounds.Add(FVector4f(50, 50, 50, 0));

			Perception->SubmitPerceptionRequest(Request, [this](const FFlightPerceptionResult& Result)
			{
				bCallbackReceived = true;
				LastResult = Result;
			});
		}

		if (IoUringSubsystem)
		{
			IoUringSubsystem->ProcessCompletions();
		}

		if (bCallbackReceived)
		{
			Test->TestTrue(TEXT("Perception callback should resolve with a request id"), LastResult.RequestId > 0);
			Test->TestTrue(TEXT("Pending requests should be drained after callback"), Perception && Perception->GetPendingCount() == 0);
			return true;
		}

		if ((FPlatformTime::Seconds() - StartTime) > 10.0)
		{
			const int32 PendingCount = Perception ? Perception->GetPendingCount() : -1;
			Test->AddError(FString::Printf(
				TEXT("GpuPerception completion test timed out (pending=%d)."),
				PendingCount));
			return true;
		}

		return false;
	}

private:
	FAutomationTestBase* Test = nullptr;
	TWeakObjectPtr<UWorld> World;
	UFlightGpuPerceptionSubsystem* Perception = nullptr;
	UFlightIoUringSubsystem* IoUringSubsystem = nullptr;
	bool bStarted = false;
	bool bCallbackReceived = false;
	double StartTime = 0.0;
	FFlightPerceptionResult LastResult;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightGpuPerceptionCompletionTest,
	"FlightProject.Spatial.GpuPerception.CallbackResolves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightGpuPerceptionCompletionTest::RunTest(const FString& Parameters)
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
		AddError(TEXT("Could find no valid World for GpuPerception completion test."));
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FFlightGpuPerceptionCompletionCommand(this, World));
	return true;
}

class FFlightGpuScaleBenchmarkCommand : public IAutomationLatentCommand
{
public:
	FFlightGpuScaleBenchmarkCommand(UWorld* InWorld, int32 InNumEntities)
		: NumEntities(InNumEntities)
		, bStarted(false)
		, bDone(false)
		, StartTime(0)
		, LastLogTime(0)
	{
		if (InWorld)
		{
			Perception = InWorld->GetSubsystem<UFlightGpuPerceptionSubsystem>();
			IoUringSubsystem = InWorld->GetSubsystem<UFlightIoUringSubsystem>();
		}
	}

	virtual bool Update() override
	{
		if (!Perception || !Perception->IsAvailable() || !IoUringSubsystem)
		{
			return true;
		}

		IoUringSubsystem->ProcessCompletions();

		double CurrentTime = FPlatformTime::Seconds();

		if (!bStarted)
		{
			bStarted = true;
			StartTime = CurrentTime;
			LastLogTime = CurrentTime;

			FFlightPerceptionRequest Request;
			Request.EntityPositions.SetNumUninitialized(NumEntities);
			for (int32 i = 0; i < NumEntities; ++i)
			{
				Request.EntityPositions[i] = FVector4f(i * 10.0f, 0, 0, 500.0f);
			}

			// Add 100 obstacles
			Request.ObstacleMinBounds.SetNumUninitialized(100);
			Request.ObstacleMaxBounds.SetNumUninitialized(100);
			for (int32 i = 0; i < 100; ++i)
			{
				Request.ObstacleMinBounds[i] = FVector4f(i * 100.0f - 50.0f, -50.0f, -50.0f, 0);
				Request.ObstacleMaxBounds[i] = FVector4f(i * 100.0f + 50.0f, 50.0f, 50.0f, 0);
			}

			const int32 LocalNum = NumEntities;
			const double LocalStart = StartTime;

			UE_LOG(LogTemp, Display, TEXT("BENCHMARK [%d Entities]: Submitted (Data Size: %.2f MB)..."), 
				LocalNum, (LocalNum * (sizeof(FVector4f)*2 + sizeof(uint32))) / 1024.0 / 1024.0);

			Perception->SubmitPerceptionRequest(Request, [this, LocalNum, LocalStart](const FFlightPerceptionResult& Result)
			{
				double EndTime = FPlatformTime::Seconds();
				double RoundTripMs = (EndTime - LocalStart) * 1000.0;

				UE_LOG(LogTemp, Display, TEXT("BENCHMARK [%d Entities]: GPU=%.2fms, RoundTrip=%.2fms, Frame=%lld"), 
					LocalNum, Result.GpuTimeMs, RoundTripMs, Result.GpuTimestamp);
				
				bDone = true;
			});

			ENQUEUE_RENDER_COMMAND(FlushGpuBenchmark)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			});
		}

		if (CurrentTime - LastLogTime > 5.0)
		{
			UE_LOG(LogTemp, Display, TEXT("BENCHMARK [%d Entities]: Waiting for GPU completion (elapsed %.1fs)..."), 
				NumEntities, CurrentTime - StartTime);
			LastLogTime = CurrentTime;

			ENQUEUE_RENDER_COMMAND(FlushGpuBenchmarkRetry)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			});
		}

		if (CurrentTime - StartTime > 60.0) // Increased timeout for larger scales
		{
			UE_LOG(LogTemp, Error, TEXT("BENCHMARK [%d Entities]: TIMEOUT after 60s"), NumEntities);
			bDone = true;
		}

		return bDone;
	}

private:
	UFlightGpuPerceptionSubsystem* Perception = nullptr;
	UFlightIoUringSubsystem* IoUringSubsystem = nullptr;
	int32 NumEntities;
	bool bStarted;
	bool bDone;
	double StartTime;
	double LastLogTime;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightGpuPerceptionBenchmark, "FlightProject.Benchmark.GpuPerception", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightGpuPerceptionBenchmark::RunTest(const FString& Parameters)
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

	if (!World) return false;

	UFlightGpuPerceptionSubsystem* Perception = World->GetSubsystem<UFlightGpuPerceptionSubsystem>();
	if (!Perception || !Perception->IsAvailable())
	{
		AddInfo(TEXT("Skipping benchmark: GPU Perception not available."));
		return true;
	}

	// Extreme Scaling
	ADD_LATENT_AUTOMATION_COMMAND(FFlightGpuScaleBenchmarkCommand(World, 10000));
	ADD_LATENT_AUTOMATION_COMMAND(FFlightGpuScaleBenchmarkCommand(World, 100000));
	ADD_LATENT_AUTOMATION_COMMAND(FFlightGpuScaleBenchmarkCommand(World, 250000));
	ADD_LATENT_AUTOMATION_COMMAND(FFlightGpuScaleBenchmarkCommand(World, 500000));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
