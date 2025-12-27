// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#include "IoUring/FlightIoUringSubsystem.h"

#if PLATFORM_LINUX
#include <poll.h>  // For POLLIN, POLLOUT
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFlightIoUring, Log, All);

UFlightIoUringSubsystem::UFlightIoUringSubsystem()
{
}

bool UFlightIoUringSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if PLATFORM_LINUX
	// Only create on Linux
	return true;
#else
	return false;
#endif
}

void UFlightIoUringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if PLATFORM_LINUX
	UE_LOG(LogFlightIoUring, Log, TEXT("Initializing io_uring subsystem..."));

	// Initialize main ring with SINGLE_ISSUER for game thread ownership
	// COOP_TASKRUN reduces syscall overhead when polling
	uint32 Flags = Flight::IoUring::SetupFlags::SingleIssuer
	             | Flight::IoUring::SetupFlags::CoopTaskrun;

	if (!MainRing.Initialize(MainRingSize, Flags))
	{
		LastError = MainRing.GetLastError();
		UE_LOG(LogFlightIoUring, Warning,
			TEXT("Failed to initialize main io_uring ring: %d"), LastError);
		return;
	}

	// Initialize inbox ring for MSG_RING reception
	if (!InboxRing.Initialize(InboxRingSize, Flags))
	{
		LastError = InboxRing.GetLastError();
		UE_LOG(LogFlightIoUring, Warning,
			TEXT("Failed to initialize inbox io_uring ring: %d"), LastError);
		MainRing = Flight::IoUring::FRing();  // Clean up main ring
		return;
	}

	UE_LOG(LogFlightIoUring, Log,
		TEXT("io_uring initialized: main ring fd=%d (sq=%d, cq=%d), inbox fd=%d"),
		MainRing.GetFd(), MainRing.GetSqCapacity(), MainRing.GetCqCapacity(),
		InboxRing.GetFd());

#endif // PLATFORM_LINUX
}

void UFlightIoUringSubsystem::Deinitialize()
{
#if PLATFORM_LINUX
	UE_LOG(LogFlightIoUring, Log,
		TEXT("Shutting down io_uring subsystem. Stats: %llu submissions, %llu completions"),
		TotalSubmissions, TotalCompletions);

	Callbacks.Empty();

	// Rings clean up automatically via destructor
#endif

	Super::Deinitialize();
}

bool UFlightIoUringSubsystem::IsAvailable() const
{
#if PLATFORM_LINUX
	return MainRing.IsValid() && InboxRing.IsValid();
#else
	return false;
#endif
}

int32 UFlightIoUringSubsystem::GetLastError() const
{
	return LastError;
}

int32 UFlightIoUringSubsystem::ProcessCompletions()
{
#if PLATFORM_LINUX
	if (!IsAvailable())
	{
		return 0;
	}

	int32 TotalProcessed = 0;

	// Process main ring completions
	TotalProcessed += MainRing.ForEachCqe([this](const Flight::IoUring::FCqe& Cqe)
	{
		HandleCompletion(Cqe);
	});

	// Process inbox ring completions (MSG_RING messages)
	TotalProcessed += InboxRing.ForEachCqe([this](const Flight::IoUring::FCqe& Cqe)
	{
		// MSG_RING completions have a special marker in flags
		if (Cqe.IsMsgRing())
		{
			UE_LOG(LogFlightIoUring, Verbose,
				TEXT("MSG_RING received: user_data=%llu, res=%d"),
				Cqe.UserData, Cqe.Res);
		}
		HandleCompletion(Cqe);
	});

	TotalCompletions += TotalProcessed;
	return TotalProcessed;

#else
	return 0;
#endif
}

#if PLATFORM_LINUX

bool UFlightIoUringSubsystem::SubmitRead(int32 Fd, void* Buffer, uint32 Size,
                                          uint64 Offset, uint64 UserData)
{
	if (!IsAvailable())
	{
		return false;
	}

	Flight::IoUring::FSqe* Sqe = MainRing.GetSqe();
	if (!Sqe)
	{
		LastError = EAGAIN;
		UE_LOG(LogFlightIoUring, Warning, TEXT("SQ full, cannot submit read"));
		return false;
	}

	Sqe->PrepRead(Fd, Buffer, Size, Offset, UserData);

	int32 Submitted = MainRing.Submit();
	if (Submitted < 0)
	{
		LastError = MainRing.GetLastError();
		return false;
	}

	++TotalSubmissions;
	return true;
}

bool UFlightIoUringSubsystem::SubmitWrite(int32 Fd, const void* Buffer, uint32 Size,
                                           uint64 Offset, uint64 UserData)
{
	if (!IsAvailable())
	{
		return false;
	}

	Flight::IoUring::FSqe* Sqe = MainRing.GetSqe();
	if (!Sqe)
	{
		LastError = EAGAIN;
		UE_LOG(LogFlightIoUring, Warning, TEXT("SQ full, cannot submit write"));
		return false;
	}

	Sqe->PrepWrite(Fd, Buffer, Size, Offset, UserData);

	int32 Submitted = MainRing.Submit();
	if (Submitted < 0)
	{
		LastError = MainRing.GetLastError();
		return false;
	}

	++TotalSubmissions;
	return true;
}

bool UFlightIoUringSubsystem::SubmitPoll(int32 Fd, uint32 PollMask, uint64 UserData, bool bMultishot)
{
	if (!IsAvailable())
	{
		return false;
	}

	Flight::IoUring::FSqe* Sqe = MainRing.GetSqe();
	if (!Sqe)
	{
		LastError = EAGAIN;
		UE_LOG(LogFlightIoUring, Warning, TEXT("SQ full, cannot submit poll"));
		return false;
	}

	if (bMultishot)
	{
		Sqe->PrepPollMultishot(Fd, PollMask, UserData);
	}
	else
	{
		Sqe->PrepPollAdd(Fd, PollMask, UserData);
	}

	int32 Submitted = MainRing.Submit();
	if (Submitted < 0)
	{
		LastError = MainRing.GetLastError();
		return false;
	}

	++TotalSubmissions;
	return true;
}

void UFlightIoUringSubsystem::RegisterCallback(uint64 UserData,
	TFunction<void(int32 Result, uint32 Flags)> Callback)
{
	Callbacks.Add(UserData, MoveTemp(Callback));
}

void UFlightIoUringSubsystem::UnregisterCallback(uint64 UserData)
{
	Callbacks.Remove(UserData);
}

void UFlightIoUringSubsystem::HandleCompletion(const Flight::IoUring::FCqe& Cqe)
{
	if (TFunction<void(int32, uint32)>* Callback = Callbacks.Find(Cqe.UserData))
	{
		(*Callback)(Cqe.Res, Cqe.Flags);
	}
	else
	{
		// No callback registered - log for debugging
		UE_LOG(LogFlightIoUring, Verbose,
			TEXT("Completion without callback: user_data=%llu, res=%d, flags=0x%x"),
			Cqe.UserData, Cqe.Res, Cqe.Flags);
	}
}

#endif // PLATFORM_LINUX
