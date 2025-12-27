// FlightMeshIRTestComponent.h
// Test component for validating MeshIR system functionality

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Modeling/FlightMeshIR.h"
#include "FlightMeshIRTestComponent.generated.h"

class UDynamicMeshComponent;

/**
 * Test component for MeshIR system.
 *
 * Attach to any actor to test procedural mesh generation.
 * Runs a series of tests on BeginPlay and logs results.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FLIGHTPROJECT_API UFlightMeshIRTestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightMeshIRTestComponent();

	//-------------------------------------------------------------------------
	// Test Configuration
	//-------------------------------------------------------------------------

	/** Run tests automatically on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR Test")
	bool bRunTestsOnBeginPlay = true;

	/** Spawn visual mesh components for each test */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR Test")
	bool bSpawnVisualMeshes = true;

	/** Spacing between visual test meshes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR Test")
	float MeshSpacing = 200.0f;

	/** Which test preset to visualize (if not running all) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR Test")
	int32 SingleTestIndex = -1; // -1 = run all

	//-------------------------------------------------------------------------
	// Actions
	//-------------------------------------------------------------------------

	/** Run all tests and log results */
	UFUNCTION(BlueprintCallable, Category = "MeshIR Test")
	void RunAllTests();

	/** Clear any spawned visual meshes */
	UFUNCTION(BlueprintCallable, Category = "MeshIR Test")
	void ClearVisualMeshes();

	/** Regenerate visual meshes */
	UFUNCTION(BlueprintCallable, Category = "MeshIR Test")
	void RegenerateVisualMeshes();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	//-------------------------------------------------------------------------
	// Individual Tests
	//-------------------------------------------------------------------------

	bool TestBasicBoxCreation();
	bool TestBoxWithHole();
	bool TestMultipleOperations();
	bool TestHashConsistency();
	bool TestCacheHitMiss();
	bool TestPresets();

	//-------------------------------------------------------------------------
	// Helpers
	//-------------------------------------------------------------------------

	void LogTestResult(const FString& TestName, bool bPassed, const FString& Details = TEXT(""));
	void SpawnMeshComponent(UDynamicMesh* Mesh, const FString& Label, const FVector& Offset);

	/** Spawned mesh components for cleanup */
	UPROPERTY()
	TArray<TObjectPtr<UDynamicMeshComponent>> SpawnedMeshComponents;

	int32 TestsPassed = 0;
	int32 TestsFailed = 0;
};
