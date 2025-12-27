// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#include "IoUring/FlightIoUringTestComponent.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "IoUring/FlightIoRing.h"
#include "Engine/World.h"

#if PLATFORM_LINUX
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightIoUringTest, Log, All);

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
	UE_LOG(LogFlightIoUringTest, Log, TEXT("=== Running io_uring Tests ==="));

	TestSubsystemAvailable();
	TestNopOperation();
	TestEventfdPoll();
	// TestFileReadWrite(); // More complex, enable when ready
}

bool UFlightIoUringTestComponent::TestSubsystemAvailable()
{
	UE_LOG(LogFlightIoUringTest, Log, TEXT("Test: Subsystem Available"));

#if PLATFORM_LINUX
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

	if (!Subsystem->IsAvailable())
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Subsystem not available (error: %d)"),
			Subsystem->GetLastError());
		++FailedTests;
		return false;
	}

	UE_LOG(LogFlightIoUringTest, Log, TEXT("  PASS: Subsystem available"));
	++PassedTests;
	return true;
#else
	return false;
#endif
}

bool UFlightIoUringTestComponent::TestNopOperation()
{
	UE_LOG(LogFlightIoUringTest, Log, TEXT("Test: NOP Operation"));

#if PLATFORM_LINUX
	UFlightIoUringSubsystem* Subsystem = GetWorld()->GetSubsystem<UFlightIoUringSubsystem>();
	if (!Subsystem || !Subsystem->IsAvailable())
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  SKIP: Subsystem not available"));
		return false;
	}

	Flight::IoUring::FRing* Ring = Subsystem->GetMainRing();
	if (!Ring)
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  FAIL: No main ring"));
		++FailedTests;
		return false;
	}

	// Submit NOP
	Flight::IoUring::FSqe* Sqe = Ring->GetSqe();
	if (!Sqe)
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  FAIL: Could not get SQE"));
		++FailedTests;
		return false;
	}

	Sqe->PrepNop(UserDataNop);

	// Register callback
	Subsystem->RegisterCallback(UserDataNop,
		[this](int32 Result, uint32 Flags) { OnNopComplete(Result, Flags); });

	++PendingTests;

	int32 Submitted = Ring->Submit();
	if (Submitted < 0)
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Submit failed (error: %d)"), Ring->GetLastError());
		++FailedTests;
		--PendingTests;
		return false;
	}

	UE_LOG(LogFlightIoUringTest, Log, TEXT("  Submitted NOP, waiting for completion..."));
	return true;
#else
	return false;
#endif
}

bool UFlightIoUringTestComponent::TestEventfdPoll()
{
	UE_LOG(LogFlightIoUringTest, Log, TEXT("Test: Eventfd Poll"));

#if PLATFORM_LINUX
	UFlightIoUringSubsystem* Subsystem = GetWorld()->GetSubsystem<UFlightIoUringSubsystem>();
	if (!Subsystem || !Subsystem->IsAvailable())
	{
		UE_LOG(LogFlightIoUringTest, Error, TEXT("  SKIP: Subsystem not available"));
		return false;
	}

	// Create eventfd for testing
	EventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (EventFd < 0)
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Could not create eventfd (errno: %d)"), errno);
		++FailedTests;
		return false;
	}

	// Register poll callback
	Subsystem->RegisterCallback(UserDataEventfd,
		[this](int32 Result, uint32 Flags) { OnEventfdPollComplete(Result, Flags); });

	++PendingTests;

	// Submit poll
	if (!Subsystem->SubmitPoll(EventFd, POLLIN, UserDataEventfd, false))
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Could not submit poll (error: %d)"),
			Subsystem->GetLastError());
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
		TEXT("  Submitted poll on eventfd %d, waiting for completion..."), EventFd);
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

	// Create temp file
	FString TempPath = FPaths::CreateTempFilename(
		*FPaths::ProjectIntermediateDir(), TEXT("iouring_test_"), TEXT(".tmp"));

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
	const char* TestData = "io_uring test data from FlightProject";
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

	// Register callback
	Subsystem->RegisterCallback(UserDataFileRead,
		[this](int32 Result, uint32 Flags) { OnFileReadComplete(Result, Flags); });

	++PendingTests;

	// Submit async read
	if (!Subsystem->SubmitRead(TempFileFd, ReadBuffer.GetData(),
		ReadBuffer.Num() - 1, 0, UserDataFileRead))
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  FAIL: Could not submit read (error: %d)"),
			Subsystem->GetLastError());
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
#if PLATFORM_LINUX
	--PendingTests;

	if (Result == 0)
	{
		UE_LOG(LogFlightIoUringTest, Log, TEXT("  NOP PASS: Completed successfully"));
		++PassedTests;
	}
	else
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  NOP FAIL: Unexpected result %d"), Result);
		++FailedTests;
	}
#endif
}

void UFlightIoUringTestComponent::OnEventfdPollComplete(int32 Result, uint32 Flags)
{
#if PLATFORM_LINUX
	--PendingTests;

	if (Result > 0)
	{
		// Read the eventfd value
		uint64 Value = 0;
		read(EventFd, &Value, sizeof(Value));

		UE_LOG(LogFlightIoUringTest, Log,
			TEXT("  Eventfd Poll PASS: Signaled with value %llu"), Value);
		++PassedTests;
	}
	else if (Result == 0)
	{
		UE_LOG(LogFlightIoUringTest, Warning,
			TEXT("  Eventfd Poll: No events (timeout?)"));
	}
	else
	{
		UE_LOG(LogFlightIoUringTest, Error,
			TEXT("  Eventfd Poll FAIL: Error %d"), -Result);
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
	else if (Result == 0)
	{
		UE_LOG(LogFlightIoUringTest, Warning,
			TEXT("  File Read: EOF (0 bytes)"));
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
