// FlightMeshIRTestComponent.cpp

#include "Modeling/FlightMeshIRTestComponent.h"
#include "Modeling/FlightMeshIRLibrary.h"
#include "Modeling/FlightMeshIRInterpreter.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMeshIRTest, Log, All);

UFlightMeshIRTestComponent::UFlightMeshIRTestComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFlightMeshIRTestComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bRunTestsOnBeginPlay)
	{
		RunAllTests();
	}
}

void UFlightMeshIRTestComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearVisualMeshes();
	Super::EndPlay(EndPlayReason);
}

void UFlightMeshIRTestComponent::RunAllTests()
{
	UE_LOG(LogMeshIRTest, Log, TEXT("========================================"));
	UE_LOG(LogMeshIRTest, Log, TEXT("MeshIR System Test Suite"));
	UE_LOG(LogMeshIRTest, Log, TEXT("========================================"));

	TestsPassed = 0;
	TestsFailed = 0;

	// Clear any existing visual meshes
	ClearVisualMeshes();

	// Run tests
	TestBasicBoxCreation();
	TestBoxWithHole();
	TestMultipleOperations();
	TestHashConsistency();
	TestCacheHitMiss();
	TestPresets();

	// Summary
	UE_LOG(LogMeshIRTest, Log, TEXT("========================================"));
	UE_LOG(LogMeshIRTest, Log, TEXT("Test Summary: %d passed, %d failed"),
		TestsPassed, TestsFailed);
	UE_LOG(LogMeshIRTest, Log, TEXT("========================================"));

	if (TestsFailed > 0)
	{
		UE_LOG(LogMeshIRTest, Warning, TEXT("Some tests failed!"));
	}
}

void UFlightMeshIRTestComponent::ClearVisualMeshes()
{
	for (UDynamicMeshComponent* Comp : SpawnedMeshComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	SpawnedMeshComponents.Empty();
}

void UFlightMeshIRTestComponent::RegenerateVisualMeshes()
{
	ClearVisualMeshes();
	RunAllTests();
}

//-----------------------------------------------------------------------------
// Individual Tests
//-----------------------------------------------------------------------------

bool UFlightMeshIRTestComponent::TestBasicBoxCreation()
{
	const FString TestName = TEXT("BasicBoxCreation");

	// Create a simple box IR
	FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(100.0f, 100.0f, 50.0f);

	// Validate IR
	if (!IR.IsValid())
	{
		LogTestResult(TestName, false, TEXT("IR validation failed"));
		return false;
	}

	if (IR.Ops.Num() != 1)
	{
		LogTestResult(TestName, false, FString::Printf(TEXT("Expected 1 op, got %d"), IR.Ops.Num()));
		return false;
	}

	// Evaluate
	FFlightMeshIRResult Result;
	if (!UFlightMeshIRInterpreter::Evaluate(IR, Result))
	{
		LogTestResult(TestName, false, FString::Printf(TEXT("Evaluation failed: %s"), *Result.ErrorMessage));
		return false;
	}

	if (!Result.Mesh)
	{
		LogTestResult(TestName, false, TEXT("Result mesh is null"));
		return false;
	}

	// Spawn visual if enabled
	if (bSpawnVisualMeshes)
	{
		SpawnMeshComponent(Result.Mesh, TEXT("Box"), FVector(0, 0, 0));
	}

	LogTestResult(TestName, true, FString::Printf(TEXT("Evaluated in %.2fms"), Result.EvaluationTimeMs));
	return true;
}

bool UFlightMeshIRTestComponent::TestBoxWithHole()
{
	const FString TestName = TEXT("BoxWithHole");

	// Create box with cylindrical hole using preset
	FFlightMeshIR IR = UFlightMeshIRPresets::MakeBoxWithHole(100.0f, 100.0f, 50.0f, 25.0f);

	if (!IR.IsValid())
	{
		LogTestResult(TestName, false, TEXT("IR validation failed"));
		return false;
	}

	// Should have 2 operations: base box + subtract cylinder
	if (IR.Ops.Num() != 2)
	{
		LogTestResult(TestName, false, FString::Printf(TEXT("Expected 2 ops, got %d"), IR.Ops.Num()));
		return false;
	}

	// Verify second op is subtract
	if (IR.Ops[1].Operation != EFlightMeshOp::Subtract)
	{
		LogTestResult(TestName, false, TEXT("Second op should be Subtract"));
		return false;
	}

	// Evaluate
	FFlightMeshIRResult Result;
	if (!UFlightMeshIRInterpreter::Evaluate(IR, Result))
	{
		LogTestResult(TestName, false, FString::Printf(TEXT("Evaluation failed: %s"), *Result.ErrorMessage));
		return false;
	}

	if (bSpawnVisualMeshes)
	{
		SpawnMeshComponent(Result.Mesh, TEXT("BoxWithHole"), FVector(MeshSpacing, 0, 0));
	}

	LogTestResult(TestName, true, FString::Printf(TEXT("2 ops evaluated in %.2fms"), Result.EvaluationTimeMs));
	return true;
}

bool UFlightMeshIRTestComponent::TestMultipleOperations()
{
	const FString TestName = TEXT("MultipleOperations");

	// Create a complex shape: box with multiple subtractions
	FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(150.0f, 150.0f, 50.0f);

	// Subtract 4 cylinders at corners
	const float CornerOffset = 50.0f;
	const float HoleRadius = 15.0f;
	const float HoleHeight = 60.0f;

	TArray<FVector> Corners = {
		FVector(CornerOffset, CornerOffset, 0),
		FVector(-CornerOffset, CornerOffset, 0),
		FVector(-CornerOffset, -CornerOffset, 0),
		FVector(CornerOffset, -CornerOffset, 0)
	};

	for (const FVector& Corner : Corners)
	{
		UFlightMeshIRLibrary::SubtractCylinder(IR, HoleRadius, HoleHeight,
			FTransform(Corner), 24);
	}

	if (IR.Ops.Num() != 5) // 1 base + 4 subtracts
	{
		LogTestResult(TestName, false, FString::Printf(TEXT("Expected 5 ops, got %d"), IR.Ops.Num()));
		return false;
	}

	FFlightMeshIRResult Result;
	if (!UFlightMeshIRInterpreter::Evaluate(IR, Result))
	{
		LogTestResult(TestName, false, FString::Printf(TEXT("Evaluation failed: %s"), *Result.ErrorMessage));
		return false;
	}

	if (bSpawnVisualMeshes)
	{
		SpawnMeshComponent(Result.Mesh, TEXT("MultiHole"), FVector(MeshSpacing * 2, 0, 0));
	}

	LogTestResult(TestName, true, FString::Printf(TEXT("5 ops evaluated in %.2fms"), Result.EvaluationTimeMs));
	return true;
}

bool UFlightMeshIRTestComponent::TestHashConsistency()
{
	const FString TestName = TEXT("HashConsistency");

	// Create identical IRs and verify hashes match
	FFlightMeshIR IR1 = UFlightMeshIRLibrary::MakeBoxIR(100.0f, 100.0f, 50.0f);
	UFlightMeshIRLibrary::SubtractCylinder(IR1, 20.0f, 60.0f, FTransform::Identity);

	FFlightMeshIR IR2 = UFlightMeshIRLibrary::MakeBoxIR(100.0f, 100.0f, 50.0f);
	UFlightMeshIRLibrary::SubtractCylinder(IR2, 20.0f, 60.0f, FTransform::Identity);

	const int64 Hash1 = IR1.ComputeHash();
	const int64 Hash2 = IR2.ComputeHash();

	if (Hash1 != Hash2)
	{
		LogTestResult(TestName, false, FString::Printf(TEXT("Identical IRs have different hashes: %lld vs %lld"), Hash1, Hash2));
		return false;
	}

	// Create different IR and verify hash differs
	FFlightMeshIR IR3 = UFlightMeshIRLibrary::MakeBoxIR(100.0f, 100.0f, 50.0f);
	UFlightMeshIRLibrary::SubtractCylinder(IR3, 25.0f, 60.0f, FTransform::Identity); // Different radius

	const int64 Hash3 = IR3.ComputeHash();

	if (Hash1 == Hash3)
	{
		LogTestResult(TestName, false, TEXT("Different IRs have same hash (collision or bug)"));
		return false;
	}

	LogTestResult(TestName, true, FString::Printf(TEXT("Hash consistency verified: %lld"), Hash1));
	return true;
}

bool UFlightMeshIRTestComponent::TestCacheHitMiss()
{
	const FString TestName = TEXT("CacheHitMiss");

	// Clear cache first
	UFlightMeshIRLibrary::ClearGlobalCache();

	FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(80.0f, 80.0f, 40.0f);
	UFlightMeshIRLibrary::SubtractSphere(IR, 30.0f, FTransform::Identity);

	// First access - should be cache miss
	int32 EntryCount1, HitCount1, MissCount1;
	UFlightMeshIRLibrary::GetCacheStats(EntryCount1, HitCount1, MissCount1);

	UDynamicMesh* Mesh1 = UFlightMeshIRLibrary::GetOrCreateMesh(IR);

	int32 EntryCount2, HitCount2, MissCount2;
	UFlightMeshIRLibrary::GetCacheStats(EntryCount2, HitCount2, MissCount2);

	if (MissCount2 != MissCount1 + 1)
	{
		LogTestResult(TestName, false, TEXT("First access should be cache miss"));
		return false;
	}

	// Second access - should be cache hit
	UDynamicMesh* Mesh2 = UFlightMeshIRLibrary::GetOrCreateMesh(IR);

	int32 EntryCount3, HitCount3, MissCount3;
	UFlightMeshIRLibrary::GetCacheStats(EntryCount3, HitCount3, MissCount3);

	if (HitCount3 != HitCount2 + 1)
	{
		LogTestResult(TestName, false, TEXT("Second access should be cache hit"));
		return false;
	}

	// Should return same mesh pointer
	if (Mesh1 != Mesh2)
	{
		LogTestResult(TestName, false, TEXT("Cache should return same mesh pointer"));
		return false;
	}

	if (bSpawnVisualMeshes)
	{
		SpawnMeshComponent(Mesh1, TEXT("Cached"), FVector(MeshSpacing * 3, 0, 0));
	}

	LogTestResult(TestName, true, FString::Printf(TEXT("Cache working: %d entries, %d hits, %d misses"),
		EntryCount3, HitCount3, MissCount3));
	return true;
}

bool UFlightMeshIRTestComponent::TestPresets()
{
	const FString TestName = TEXT("Presets");

	bool bAllPresetsOk = true;
	int32 VisualIndex = 4;

	// Test L-Bracket
	{
		FFlightMeshIR IR = UFlightMeshIRPresets::MakeLBracket(80.0f, 25.0f, 8.0f);
		FFlightMeshIRResult Result;

		if (UFlightMeshIRInterpreter::Evaluate(IR, Result) && Result.Mesh)
		{
			if (bSpawnVisualMeshes)
			{
				SpawnMeshComponent(Result.Mesh, TEXT("LBracket"), FVector(MeshSpacing * VisualIndex++, 0, 0));
			}
		}
		else
		{
			UE_LOG(LogMeshIRTest, Warning, TEXT("L-Bracket preset failed"));
			bAllPresetsOk = false;
		}
	}

	// Test T-Junction
	{
		FFlightMeshIR IR = UFlightMeshIRPresets::MakeTJunction(20.0f, 80.0f, 15.0f, 40.0f);
		FFlightMeshIRResult Result;

		if (UFlightMeshIRInterpreter::Evaluate(IR, Result) && Result.Mesh)
		{
			if (bSpawnVisualMeshes)
			{
				SpawnMeshComponent(Result.Mesh, TEXT("TJunction"), FVector(MeshSpacing * VisualIndex++, 0, 0));
			}
		}
		else
		{
			UE_LOG(LogMeshIRTest, Warning, TEXT("T-Junction preset failed"));
			bAllPresetsOk = false;
		}
	}

	// Test Hex Nut
	{
		FFlightMeshIR IR = UFlightMeshIRPresets::MakeHexNut(25.0f, 12.0f, 8.0f);
		FFlightMeshIRResult Result;

		if (UFlightMeshIRInterpreter::Evaluate(IR, Result) && Result.Mesh)
		{
			if (bSpawnVisualMeshes)
			{
				SpawnMeshComponent(Result.Mesh, TEXT("HexNut"), FVector(MeshSpacing * VisualIndex++, 0, 0));
			}
		}
		else
		{
			UE_LOG(LogMeshIRTest, Warning, TEXT("Hex Nut preset failed"));
			bAllPresetsOk = false;
		}
	}

	LogTestResult(TestName, bAllPresetsOk, bAllPresetsOk ? TEXT("All presets evaluated") : TEXT("Some presets failed"));
	return bAllPresetsOk;
}

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

void UFlightMeshIRTestComponent::LogTestResult(const FString& TestName, bool bPassed, const FString& Details)
{
	if (bPassed)
	{
		TestsPassed++;
		UE_LOG(LogMeshIRTest, Log, TEXT("[PASS] %s%s"),
			*TestName,
			Details.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - %s"), *Details));
	}
	else
	{
		TestsFailed++;
		UE_LOG(LogMeshIRTest, Error, TEXT("[FAIL] %s%s"),
			*TestName,
			Details.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - %s"), *Details));
	}
}

void UFlightMeshIRTestComponent::SpawnMeshComponent(UDynamicMesh* Mesh, const FString& Label, const FVector& Offset)
{
	if (!Mesh || !GetOwner())
	{
		return;
	}

	AActor* Owner = GetOwner();

	// Create dynamic mesh component
	UDynamicMeshComponent* MeshComp = NewObject<UDynamicMeshComponent>(Owner,
		*FString::Printf(TEXT("MeshIRTest_%s"), *Label));

	if (MeshComp)
	{
		MeshComp->SetupAttachment(Owner->GetRootComponent());
		MeshComp->SetRelativeLocation(Offset);
		MeshComp->SetDynamicMesh(Mesh);
		MeshComp->RegisterComponent();

		SpawnedMeshComponents.Add(MeshComp);

		UE_LOG(LogMeshIRTest, Verbose, TEXT("Spawned mesh component: %s at %s"),
			*Label, *Offset.ToString());
	}
}
