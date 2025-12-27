// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Full GPU to io_uring integration test

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlightGpuIoUringIntegrationTest.generated.h"

/**
 * UFlightGpuIoUringIntegrationTest
 *
 * Comprehensive test component for GPU → io_uring integration.
 * Tests the complete data flow:
 *
 * 1. Submit compute shader via RDG
 * 2. Track completion via external Vulkan fence
 * 3. Export fence to sync fd
 * 4. Poll sync fd via io_uring
 * 5. Read back results when complete
 *
 * Attach to any actor for testing. Will run tests on BeginPlay
 * and report results to the log.
 */
UCLASS(ClassGroup = (Flight), meta = (BlueprintSpawnableComponent))
class FLIGHTPROJECT_API UFlightGpuIoUringIntegrationTest : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightGpuIoUringIntegrationTest();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Run the integration test suite */
	UFUNCTION(BlueprintCallable, Category = "Flight|Test")
	void RunTests();

	/** Test 1: Verify io_uring subsystem is available */
	UFUNCTION(BlueprintCallable, Category = "Flight|Test")
	bool TestIoUringAvailable();

	/** Test 2: Verify Vulkan external fence support */
	UFUNCTION(BlueprintCallable, Category = "Flight|Test")
	bool TestExternalFenceSupport();

	/** Test 3: Create and export a fence */
	UFUNCTION(BlueprintCallable, Category = "Flight|Test")
	bool TestFenceExport();

	/** Test 4: Poll exported fence via io_uring */
	UFUNCTION(BlueprintCallable, Category = "Flight|Test")
	bool TestFencePoll();

	/** Test 5: Full compute shader lifecycle */
	UFUNCTION(BlueprintCallable, Category = "Flight|Test")
	bool TestComputeLifecycle();

	/** Enable stress testing mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Test")
	bool bEnableStressTest = false;

	/** Number of iterations for stress test */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Test")
	int32 StressTestIterations = 100;

	/** Buffer size for compute tests */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Test")
	int32 ComputeBufferSize = 4096;

protected:
	void OnComputeComplete(int32 TestIndex, double ElapsedMs, bool bSuccess);
	void PrintTestSummary();

private:
	// Test state
	int32 TotalTests = 0;
	int32 PassedTests = 0;
	int32 FailedTests = 0;
	int32 SkippedTests = 0;

	// Stress test state
	int32 StressTestPending = 0;
	int32 StressTestCompleted = 0;
	double StressTestTotalTime = 0.0;
	double StressTestMinTime = DBL_MAX;
	double StressTestMaxTime = 0.0;

	// Subsystem references
#if PLATFORM_LINUX
	class UFlightIoUringSubsystem* IoUringSubsystem = nullptr;
	class UFlightGpuIoUringBridge* GpuBridge = nullptr;
#endif
};
