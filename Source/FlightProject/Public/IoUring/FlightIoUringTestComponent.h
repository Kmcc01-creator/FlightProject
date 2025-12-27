// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlightIoUringTestComponent.generated.h"

/**
 * UFlightIoUringTestComponent
 *
 * Test component for validating io_uring subsystem functionality.
 * Attach to any actor for development/debugging purposes.
 *
 * Tests:
 * - Subsystem initialization
 * - Basic NOP operations
 * - File read/write (to temp file)
 * - Poll operations (on eventfd)
 * - Completion callback routing
 */
UCLASS(ClassGroup = (Flight), meta = (BlueprintSpawnableComponent))
class FLIGHTPROJECT_API UFlightIoUringTestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFlightIoUringTestComponent();

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Run all io_uring tests */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring|Test")
	void RunAllTests();

	/** Run subsystem availability test */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring|Test")
	bool TestSubsystemAvailable();

	/** Run NOP operation test */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring|Test")
	bool TestNopOperation();

	/** Run eventfd poll test */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring|Test")
	bool TestEventfdPoll();

	/** Run temp file read/write test */
	UFUNCTION(BlueprintCallable, Category = "Flight|IoUring|Test")
	bool TestFileReadWrite();

	/** Enable continuous polling test (for stress testing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|IoUring|Test")
	bool bEnableContinuousTest = false;

	/** Interval between continuous test iterations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|IoUring|Test")
	float ContinuousTestInterval = 1.0f;

protected:
	/** Callback for NOP completion */
	void OnNopComplete(int32 Result, uint32 Flags);

	/** Callback for eventfd poll completion */
	void OnEventfdPollComplete(int32 Result, uint32 Flags);

	/** Callback for file read completion */
	void OnFileReadComplete(int32 Result, uint32 Flags);

private:
#if PLATFORM_LINUX
	// Test state
	int32 EventFd = -1;
	int32 TempFileFd = -1;
	TArray<uint8> ReadBuffer;

	// User data identifiers
	static constexpr uint64 UserDataNop = 0x1000;
	static constexpr uint64 UserDataEventfd = 0x2000;
	static constexpr uint64 UserDataFileRead = 0x3000;

	// Test tracking
	int32 PendingTests = 0;
	int32 PassedTests = 0;
	int32 FailedTests = 0;

	// Continuous test timing
	float TimeSinceLastTest = 0.0f;
#endif
};
