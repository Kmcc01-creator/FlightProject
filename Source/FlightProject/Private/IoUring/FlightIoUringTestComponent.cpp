// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration (Executor Refactored)

#include "IoUring/FlightIoUringTestComponent.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "Core/FlightAsyncExecutor.h"
#include "Engine/World.h"

#if PLATFORM_LINUX
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightIoUringTest, Log, All);

using namespace Flight::Async;

UFlightIoUringTestComponent::UFlightIoUringTestComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UFlightIoUringTestComponent::BeginPlay()
{
	Super::BeginPlay();

#if PLATFORM_LINUX
	// Reset test state
	PendingTests = 0;
	PassedTests = 0;
	FailedTests = 0;
	TimeSinceLastTest = 0.0f;

	// Enable tick for continuous testing if requested
	if (bEnableContinuousTest)
	{
		SetComponentTickEnabled(true);
	}

	// Run initial tests
	RunAllTests();
#else
	UE_LOG(LogFlightIoUringTest, Warning,
		TEXT("io_uring tests only available on Linux"));
#endif
}

void UFlightIoUringTestComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if PLATFORM_LINUX
	if (bEnableContinuousTest)
	{
		TimeSinceLastTest += DeltaTime;
		if (TimeSinceLastTest >= ContinuousTestInterval)
		{
			TimeSinceLastTest = 0.0f;
			TestNopOperation();
		}
	}
#endif
}

void UFlightIoUringTestComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if PLATFORM_LINUX
	// Clean up test resources
	if (EventFd >= 0)
	{
		close(EventFd);
		EventFd = -1;
	}
	if (TempFileFd >= 0)
	{
		close(TempFileFd);
		TempFileFd = -1;
	}

	UE_LOG(LogFlightIoUringTest, Log,
		TEXT("io_uring tests complete: %d passed, %d failed"),
		PassedTests, FailedTests);
#endif

	Super::EndPlay(EndPlayReason);
}

void UFlightIoUringTestComponent::RunAllTests()
{
	UE_LOG(LogFlightIoUringTest, Log, TEXT("=== Running Executor Tests ==="));

	TestSubsystemAvailable();
	// TestNopOperation(); // NOP is backend-specific, use Read for generic check
	TestEventfdPoll();
	TestFileReadWrite();
}

bool UFlightIoUringTestComponent::TestSubsystemAvailable()
{
	UE_LOG(LogFlightIoUringTest, Log, TEXT("Test: Subsystem Available"));

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  FAIL: No world"));
		++FailedTests;
		return false;
	}

	UFlightIoUringSubsystem* Subsystem = World->GetSubsystem<UFlightIoUringSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  FAIL: Subsystem not found"));
		++FailedTests;
		return false;
	}

	if (!Subsystem->IsAvailable() || !Subsystem->GetExecutor())
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  FAIL: Executor not available"));
		++FailedTests;
		return false;
	}

	UE_LOG(LogFlightIoUringTest, Log, TEXT("  PASS: Subsystem available. Backend: %s"), 
		Subsystem->GetExecutor()->GetBackendName());
	++PassedTests;
	return true;
}

bool UFlightIoUringTestComponent::TestNopOperation()
{
	// NOP was removed from generic interface as it's an implementation detail of io_uring.
	// We'll use Read/Write for generic testing.
	return true;
}

bool UFlightIoUringTestComponent::TestEventfdPoll()
{
	UE_LOG(LogFlightIoUringTest, Log, TEXT("Test: Eventfd Poll (EventWait)"));

#if PLATFORM_LINUX
	UFlightIoUringSubsystem* Subsystem = GetWorld()->GetSubsystem<UFlightIoUringSubsystem>();
	if (!Subsystem || !Subsystem->IsAvailable())
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  SKIP: Subsystem not available"));
		return false;
	}

	IFlightAsyncExecutor* Executor = Subsystem->GetExecutor();

	// Create eventfd for testing
	EventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (EventFd < 0)
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Could not create eventfd (errno: %d)"), errno);
		++FailedTests;
		return false;
	}

	++PendingTests;

	// Submit event wait
	FAsyncHandle Handle = FAsyncHandle::FromLinuxFd(EventFd);
	bool bSubmitted = Executor->SubmitEventWait(Handle, [this](const FAsyncResult& Result) {
		OnEventfdPollComplete(Result.ErrorCode, Result.Flags);
	});

	if (!bSubmitted)
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Could not submit event wait (error: %d)"),
			Executor->GetLastError());
		++FailedTests;
		--PendingTests;
		close(EventFd);
		EventFd = -1;
		return false;
	}

	// Signal the eventfd to trigger completion
	uint64 Value = 1;
	if (write(EventFd, &Value, sizeof(Value)) != sizeof(Value))
	{
		UE_LOG(LogFlightIoUringTest, Warning,
			TEXT("  Warning: Could not write to eventfd"));
	}

	UE_LOG(LogFlightIoUringTest, Log,
		TEXT("  Submitted wait on eventfd %d, waiting for completion..."), EventFd);
	return true;
#else
	return false;
#endif
}

bool UFlightIoUringTestComponent::TestFileReadWrite()
{
	UE_LOG(LogFlightIoUringTest, Log, TEXT("Test: File Read/Write"));

#if PLATFORM_LINUX
	UFlightIoUringSubsystem* Subsystem = GetWorld()->GetSubsystem<UFlightIoUringSubsystem>();
	if (!Subsystem || !Subsystem->IsAvailable())
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  SKIP: Subsystem not available"));
		return false;
	}

	IFlightAsyncExecutor* Executor = Subsystem->GetExecutor();

	// Create temp file
	FString TempPath = FPaths::CreateTempFilename(
		*FPaths::ProjectIntermediateDir(), TEXT("async_test_"), TEXT(".tmp"));

	TempFileFd = open(TCHAR_TO_UTF8(*TempPath),
		O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (TempFileFd < 0)
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Could not create temp file (errno: %d)"), errno);
		++FailedTests;
		return false;
	}

	// Write test data synchronously first
	const char* TestData = "Async Executor Test Data";
	ssize_t Written = write(TempFileFd, TestData, strlen(TestData));
	if (Written != (ssize_t)strlen(TestData))
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  FAIL: Write failed"));
		close(TempFileFd);
		TempFileFd = -1;
		++FailedTests;
		return false;
	}

	// Prepare read buffer
	ReadBuffer.SetNumZeroed(strlen(TestData) + 1);

	++PendingTests;

	// Submit async read
	FAsyncHandle Handle = FAsyncHandle::FromLinuxFd(TempFileFd);
	bool bSubmitted = Executor->SubmitRead(Handle, ReadBuffer.GetData(), ReadBuffer.Num() - 1, 0, 
		[this](const FAsyncResult& Result) {
			OnFileReadComplete(Result.ErrorCode, Result.Flags);
		});

	if (!bSubmitted)
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Could not submit read (error: %d)"),
			Executor->GetLastError());
		++FailedTests;
		--PendingTests;
		close(TempFileFd);
		TempFileFd = -1;
		return false;
	}

	UE_LOG(LogFlightIoUringTest, Log,
		TEXT("  Submitted read from temp file, waiting for completion..."));
	return true;
#else
	return false;
#endif
}

void UFlightIoUringTestComponent::OnNopComplete(int32 Result, uint32 Flags)
{
	--PendingTests;
	if (Result == 0)
	{
		UE_LOG(LogFlightIoUringTest, Log, TEXT("  NOP PASS"));
		++PassedTests;
	}
	else
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  NOP FAIL"));
		++FailedTests;
	}
}

void UFlightIoUringTestComponent::OnEventfdPollComplete(int32 Result, uint32 Flags)
{
#if PLATFORM_LINUX
	--PendingTests;

	if (Result >= 0)
	{
		// Read the eventfd value
		uint64 Value = 0;
		read(EventFd, &Value, sizeof(Value));

		UE_LOG(LogFlightIoUringTest, Log,
			TEXT("  Eventfd PASS: Signaled with value %llu"), Value);
		++PassedTests;
	}
	else
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  Eventfd FAIL: Error %d"), -Result);
		++FailedTests;
	}

	// Clean up
	if (EventFd >= 0)
	{
		close(EventFd);
		EventFd = -1;
	}
#endif
}

void UFlightIoUringTestComponent::OnFileReadComplete(int32 Result, uint32 Flags)
{
#if PLATFORM_LINUX
	--PendingTests;

	if (Result > 0)
	{
		FString ReadData = UTF8_TO_TCHAR(reinterpret_cast<char*>(ReadBuffer.GetData()));
		UE_LOG(LogFlightIoUringTest, Log,
			TEXT("  File Read PASS: Read %d bytes: '%s'"), Result, *ReadData);
		++PassedTests;
	}
	else
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  File Read FAIL: Error %d"), -Result);
		++FailedTests;
	}

	// Clean up
	if (TempFileFd >= 0)
	{
		close(TempFileFd);
		TempFileFd = -1;
	}
	ReadBuffer.Empty();
#endif
}
