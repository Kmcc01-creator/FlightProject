// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

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

	UFlightSwarmSubsystem* GetSwarmSubsystem(FAutomationTestBase* Test)
	{
		UWorld* World = FindAutomationWorld();
		if (!World)
		{
			Test->AddError(TEXT("Could not find valid World for Swarm pipeline test."));
			return nullptr;
		}

		UFlightSwarmSubsystem* SwarmSubsystem = World->GetSubsystem<UFlightSwarmSubsystem>();
		if (!SwarmSubsystem)
		{
			Test->AddError(TEXT("FlightSwarmSubsystem not found."));
			return nullptr;
		}

		return SwarmSubsystem;
	}

	bool ShouldSkipNoGpu(FAutomationTestBase* Test, const TCHAR* TestLabel)
	{
		const bool bSupportsGpuCompute = FApp::CanEverRender() && (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5);
		if (bSupportsGpuCompute)
		{
			return false;
		}

		Test->AddInfo(FString::Printf(TEXT("Skipping %s: no GPU-capable RHI is available (headless/NullRHI context)."), TestLabel));
		return true;
	}

	struct FScopedPersistenceMode
	{
		explicit FScopedPersistenceMode(int32 NewValue)
		{
			Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Flight.Swarm.PersistenceMode"));
			if (Var)
			{
				PreviousValue = Var->GetInt();
				Var->Set(NewValue, ECVF_SetByCode);
			}
		}

		~FScopedPersistenceMode()
		{
			if (Var)
			{
				Var->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Var = nullptr;
		int32 PreviousValue = 0;
	};

	void ConfigureSwarmForPersistenceTest(UFlightSwarmSubsystem* Subsystem, int32 InLatticeResolution, int32 InCloudResolution)
	{
		Subsystem->LatticeResolution = InLatticeResolution;
		Subsystem->CloudResolution = InCloudResolution;
		Subsystem->LatticeExtent = 8000.0f;
		Subsystem->CloudExtent = 8000.0f;
		Subsystem->CloudInjection = 1.0f;
		Subsystem->PlayerPosition = FVector(0, 0, 1000);
	}

	FFlightSwarmPersistenceDebugSnapshot TickAndReadSnapshot(UFlightSwarmSubsystem* Subsystem)
	{
		Subsystem->TickSimulation(0.016f);
		FlushRenderingCommands();
		return Subsystem->GetLastPersistenceDebugSnapshot();
	}
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightSwarmGridHashConsistencyTest,
	"FlightProject.Swarm.Pipeline.GridHashSizeConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightSwarmPersistenceStatelessTest,
	"FlightProject.Swarm.Pipeline.Persistence.StatelessMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightSwarmPersistenceEnabledTest,
	"FlightProject.Swarm.Pipeline.Persistence.PersistentMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightSwarmGridHashConsistencyTest::RunTest(const FString& Parameters)
{
	const FString ShaderPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders/Private/FlightSwarmCommon.ush"));
	FString ShaderSource;
	if (!FFileHelper::LoadFileToString(ShaderSource, *ShaderPath))
	{
		AddError(FString::Printf(TEXT("Failed to read shader file: %s"), *ShaderPath));
		return false;
	}

	const FString DefineToken = TEXT("#define GRID_HASH_SIZE");
	const int32 TokenPos = ShaderSource.Find(DefineToken, ESearchCase::CaseSensitive);
	if (TokenPos == INDEX_NONE)
	{
		AddError(TEXT("GRID_HASH_SIZE define missing from FlightSwarmCommon.ush"));
		return false;
	}

	int32 Cursor = TokenPos + DefineToken.Len();
	while (Cursor < ShaderSource.Len() && FChar::IsWhitespace(ShaderSource[Cursor]))
	{
		++Cursor;
	}

	FString NumericToken;
	while (Cursor < ShaderSource.Len() && FChar::IsDigit(ShaderSource[Cursor]))
	{
		NumericToken.AppendChar(ShaderSource[Cursor]);
		++Cursor;
	}

	if (NumericToken.IsEmpty())
	{
		AddError(TEXT("Could not parse numeric GRID_HASH_SIZE value from shader define."));
		return false;
	}

	const int32 ShaderGridHashSize = FCString::Atoi(*NumericToken);
	TestEqual(
		TEXT("Shader GRID_HASH_SIZE must match Flight::Swarm::GridHashSize"),
		ShaderGridHashSize,
		static_cast<int32>(Flight::Swarm::GridHashSize));

	return true;
}

bool FFlightSwarmPersistenceStatelessTest::RunTest(const FString& Parameters)
{
	if (ShouldSkipNoGpu(this, TEXT("Swarm stateless persistence test")))
	{
		return true;
	}

	UFlightSwarmSubsystem* SwarmSubsystem = GetSwarmSubsystem(this);
	if (!SwarmSubsystem)
	{
		return false;
	}

	FScopedPersistenceMode ScopedMode(0);
	ConfigureSwarmForPersistenceTest(SwarmSubsystem, 40, 36);
	SwarmSubsystem->InitializeSwarm(4096);
	FlushRenderingCommands();

	TickAndReadSnapshot(SwarmSubsystem);
	const FFlightSwarmPersistenceDebugSnapshot Snapshot = TickAndReadSnapshot(SwarmSubsystem);

	TestEqual(
		TEXT("Requested mode should be stateless"),
		static_cast<uint8>(Snapshot.RequestedMode),
		static_cast<uint8>(EFlightSwarmPersistenceMode::Stateless));
	TestEqual(
		TEXT("Applied mode should remain stateless when persistence mode is disabled"),
		static_cast<uint8>(Snapshot.AppliedMode),
		static_cast<uint8>(EFlightSwarmPersistenceMode::Stateless));
	TestFalse(TEXT("Stateless mode should not consume prior lattice frame"), Snapshot.bUsedPriorLatticeFrame);
	TestFalse(TEXT("Stateless mode should not consume prior cloud frame"), Snapshot.bUsedPriorCloudFrame);

	return true;
}

bool FFlightSwarmPersistenceEnabledTest::RunTest(const FString& Parameters)
{
	if (ShouldSkipNoGpu(this, TEXT("Swarm persistent mode test")))
	{
		return true;
	}

	UFlightSwarmSubsystem* SwarmSubsystem = GetSwarmSubsystem(this);
	if (!SwarmSubsystem)
	{
		return false;
	}

	FScopedPersistenceMode ScopedMode(1);
	ConfigureSwarmForPersistenceTest(SwarmSubsystem, 52, 48);
	SwarmSubsystem->InitializeSwarm(4096);
	FlushRenderingCommands();

	const FFlightSwarmPersistenceDebugSnapshot FirstTickSnapshot = TickAndReadSnapshot(SwarmSubsystem);
	const FFlightSwarmPersistenceDebugSnapshot SecondTickSnapshot = TickAndReadSnapshot(SwarmSubsystem);

	TestEqual(
		TEXT("Requested mode should be persistent"),
		static_cast<uint8>(SecondTickSnapshot.RequestedMode),
		static_cast<uint8>(EFlightSwarmPersistenceMode::Persistent));
	TestEqual(
		TEXT("Applied mode should be persistent on second tick when prior textures are available"),
		static_cast<uint8>(SecondTickSnapshot.AppliedMode),
		static_cast<uint8>(EFlightSwarmPersistenceMode::Persistent));
	TestFalse(TEXT("First tick should not consume prior lattice frame"), FirstTickSnapshot.bUsedPriorLatticeFrame);
	TestFalse(TEXT("First tick should not consume prior cloud frame"), FirstTickSnapshot.bUsedPriorCloudFrame);
	TestTrue(TEXT("Second tick should consume prior lattice frame"), SecondTickSnapshot.bUsedPriorLatticeFrame);
	TestTrue(TEXT("Second tick should consume prior cloud frame"), SecondTickSnapshot.bUsedPriorCloudFrame);

	return true;
}

bool FFlightSwarmFullPipelineTest::RunTest(const FString& Parameters)
{
	if (ShouldSkipNoGpu(this, TEXT("Swarm full integration test")))
	{
		return true;
	}

	UFlightSwarmSubsystem* SwarmSubsystem = GetSwarmSubsystem(this);
	if (!SwarmSubsystem)
	{
		return false;
	}

	// 1. Initialize Swarm (100,000 for standard integration test, validating 4-bit Radix Sort)
	const int32 EntityCount = 100000;
	SwarmSubsystem->InitializeSwarm(EntityCount);
	
	TestEqual("Subsystem should report correct entity count", SwarmSubsystem->GetNumEntities(), EntityCount);

	// 2. Set some reactive parameters
	SwarmSubsystem->SmoothingRadius = 600.0f;
	SwarmSubsystem->PredictionHorizon = 15;
	SwarmSubsystem->PlayerPosition = FVector(1000, 1000, 1000);

	// 3. Queue latent simulation ticks
	// This verifies the full 15-pass RDG pipeline (Grid, Radix Sort, Simulation, Field Injection) executes without crashes
	ADD_LATENT_AUTOMATION_COMMAND(FVerifySwarmSimulationCommand(SwarmSubsystem, 10));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
