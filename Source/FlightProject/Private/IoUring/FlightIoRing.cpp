// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#include "IoUring/FlightIoRing.h"

#if PLATFORM_LINUX

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <atomic>

namespace Flight::IoUring
{

// ============================================================================
// Direct Syscall Wrappers
// ============================================================================

namespace
{

/**
 * Direct io_uring_setup syscall
 * Bypasses libc for explicit control
 */
FORCEINLINE int32 SysIoUringSetup(uint32 Entries, FParams* Params)
{
	return static_cast<int32>(syscall(Syscall::IoUringSetup, Entries, Params));
}

/**
 * Direct io_uring_enter syscall
 */
FORCEINLINE int32 SysIoUringEnter(int32 Fd, uint32 ToSubmit, uint32 MinComplete, uint32 Flags)
{
	return static_cast<int32>(syscall(Syscall::IoUringEnter, Fd, ToSubmit, MinComplete, Flags, nullptr, 0));
}

/**
 * Direct io_uring_register syscall
 */
FORCEINLINE int32 SysIoUringRegister(int32 Fd, uint32 Opcode, void* Arg, uint32 NrArgs)
{
	return static_cast<int32>(syscall(Syscall::IoUringRegister, Fd, Opcode, Arg, NrArgs));
}

} // anonymous namespace

// ============================================================================
// FRing Implementation
// ============================================================================

FRing::FRing()
	: RingFd(-1)
	, SqEntries(0)
	, CqEntries(0)
	, SqMask(0)
	, CqMask(0)
	, SqHead(nullptr)
	, SqTail(nullptr)
	, SqArray(nullptr)
	, Sqes(nullptr)
	, CqHead(nullptr)
	, CqTail(nullptr)
	, Cqes(nullptr)
	, SqMmap(nullptr)
	, SqMmapSize(0)
	, CqMmap(nullptr)
	, CqMmapSize(0)
	, SqesMmap(nullptr)
	, SqesMmapSize(0)
	, LastError(0)
{
}

FRing::~FRing()
{
	Cleanup();
}

FRing::FRing(FRing&& Other) noexcept
	: RingFd(Other.RingFd)
	, SqEntries(Other.SqEntries)
	, CqEntries(Other.CqEntries)
	, SqMask(Other.SqMask)
	, CqMask(Other.CqMask)
	, SqHead(Other.SqHead)
	, SqTail(Other.SqTail)
	, SqArray(Other.SqArray)
	, Sqes(Other.Sqes)
	, CqHead(Other.CqHead)
	, CqTail(Other.CqTail)
	, Cqes(Other.Cqes)
	, SqMmap(Other.SqMmap)
	, SqMmapSize(Other.SqMmapSize)
	, CqMmap(Other.CqMmap)
	, CqMmapSize(Other.CqMmapSize)
	, SqesMmap(Other.SqesMmap)
	, SqesMmapSize(Other.SqesMmapSize)
	, LastError(Other.LastError)
{
	// Invalidate source
	Other.RingFd = -1;
	Other.SqMmap = nullptr;
	Other.CqMmap = nullptr;
	Other.SqesMmap = nullptr;
}

FRing& FRing::operator=(FRing&& Other) noexcept
{
	if (this != &Other)
	{
		Cleanup();

		RingFd = Other.RingFd;
		SqEntries = Other.SqEntries;
		CqEntries = Other.CqEntries;
		SqMask = Other.SqMask;
		CqMask = Other.CqMask;
		SqHead = Other.SqHead;
		SqTail = Other.SqTail;
		SqArray = Other.SqArray;
		Sqes = Other.Sqes;
		CqHead = Other.CqHead;
		CqTail = Other.CqTail;
		Cqes = Other.Cqes;
		SqMmap = Other.SqMmap;
		SqMmapSize = Other.SqMmapSize;
		CqMmap = Other.CqMmap;
		CqMmapSize = Other.CqMmapSize;
		SqesMmap = Other.SqesMmap;
		SqesMmapSize = Other.SqesMmapSize;
		LastError = Other.LastError;

		// Invalidate source
		Other.RingFd = -1;
		Other.SqMmap = nullptr;
		Other.CqMmap = nullptr;
		Other.SqesMmap = nullptr;
	}
	return *this;
}

bool FRing::Initialize(uint32 Entries, uint32 Flags)
{
	if (IsValid())
	{
		LastError = EBUSY;
		return false;
	}

	FParams Params;
	FMemory::Memzero(&Params, sizeof(Params));
	Params.Flags = Flags;

	// io_uring_setup syscall
	int32 Fd = SysIoUringSetup(Entries, &Params);
	if (Fd < 0)
	{
		LastError = -Fd;
		return false;
	}

	RingFd = Fd;
	SqEntries = Params.SqEntries;
	CqEntries = Params.CqEntries;

	// Setup mmaps
	if (!SetupMmaps(Params))
	{
		Cleanup();
		return false;
	}

	return true;
}

bool FRing::SetupMmaps(const FParams& Params)
{
	// Calculate mmap sizes
	// SQ ring size = offset to array + (entries * sizeof(uint32))
	SqMmapSize = Params.SqOff.Array + Params.SqEntries * sizeof(uint32);

	// CQ ring size = offset to cqes + (entries * sizeof(FCqe))
	CqMmapSize = Params.CqOff.Cqes + Params.CqEntries * sizeof(FCqe);

	// SQEs array
	SqesMmapSize = Params.SqEntries * sizeof(FSqe);

	// mmap SQ ring
	SqMmap = mmap(nullptr, SqMmapSize, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE, RingFd, MmapOffsets::SqRing);
	if (SqMmap == MAP_FAILED)
	{
		LastError = errno;
		SqMmap = nullptr;
		return false;
	}

	// mmap CQ ring (may share with SQ ring in some kernel versions)
	CqMmap = mmap(nullptr, CqMmapSize, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE, RingFd, MmapOffsets::CqRing);
	if (CqMmap == MAP_FAILED)
	{
		LastError = errno;
		CqMmap = nullptr;
		return false;
	}

	// mmap SQEs array
	SqesMmap = mmap(nullptr, SqesMmapSize, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE, RingFd, MmapOffsets::Sqes);
	if (SqesMmap == MAP_FAILED)
	{
		LastError = errno;
		SqesMmap = nullptr;
		return false;
	}

	// Setup pointers from offsets
	uint8* SqBase = static_cast<uint8*>(SqMmap);
	uint8* CqBase = static_cast<uint8*>(CqMmap);

	SqHead = reinterpret_cast<std::atomic<uint32>*>(SqBase + Params.SqOff.Head);
	SqTail = reinterpret_cast<std::atomic<uint32>*>(SqBase + Params.SqOff.Tail);
	SqMask = *reinterpret_cast<uint32*>(SqBase + Params.SqOff.RingMask);
	SqArray = reinterpret_cast<uint32*>(SqBase + Params.SqOff.Array);
	Sqes = static_cast<FSqe*>(SqesMmap);

	CqHead = reinterpret_cast<std::atomic<uint32>*>(CqBase + Params.CqOff.Head);
	CqTail = reinterpret_cast<std::atomic<uint32>*>(CqBase + Params.CqOff.Tail);
	CqMask = *reinterpret_cast<uint32*>(CqBase + Params.CqOff.RingMask);
	Cqes = reinterpret_cast<FCqe*>(CqBase + Params.CqOff.Cqes);

	return true;
}

void FRing::Cleanup()
{
	if (SqesMmap)
	{
		munmap(SqesMmap, SqesMmapSize);
		SqesMmap = nullptr;
	}
	if (CqMmap)
	{
		munmap(CqMmap, CqMmapSize);
		CqMmap = nullptr;
	}
	if (SqMmap)
	{
		munmap(SqMmap, SqMmapSize);
		SqMmap = nullptr;
	}
	if (RingFd >= 0)
	{
		close(RingFd);
		RingFd = -1;
	}

	SqHead = nullptr;
	SqTail = nullptr;
	SqArray = nullptr;
	Sqes = nullptr;
	CqHead = nullptr;
	CqTail = nullptr;
	Cqes = nullptr;
}

FSqe* FRing::GetSqe()
{
	// Load current tail (we own this)
	uint32 Tail = SqTail->load(std::memory_order_relaxed);
	// Load head (kernel owns this)
	uint32 Head = SqHead->load(std::memory_order_acquire);

	// Check if queue is full
	if (Tail - Head >= SqEntries)
	{
		return nullptr;
	}

	uint32 Index = Tail & SqMask;
	FSqe* Sqe = &Sqes[Index];

	// Zero the SQE for clean state
	Sqe->Zero();

	// Update array (maps SQ slot to SQE index - typically identity)
	SqArray[Index] = Index;

	// Advance tail (visible to Submit)
	SqTail->store(Tail + 1, std::memory_order_release);

	return Sqe;
}

int32 FRing::Submit()
{
	// Memory barrier before syscall
	std::atomic_thread_fence(std::memory_order_seq_cst);

	uint32 ToSubmit = GetPendingSubmissions();
	if (ToSubmit == 0)
	{
		return 0;
	}

	int32 Ret = SysIoUringEnter(RingFd, ToSubmit, 0, 0);
	if (Ret < 0)
	{
		LastError = -Ret;
		return -1;
	}

	return Ret;
}

int32 FRing::SubmitAndWait(uint32 MinComplete)
{
	std::atomic_thread_fence(std::memory_order_seq_cst);

	uint32 ToSubmit = GetPendingSubmissions();

	int32 Ret = SysIoUringEnter(RingFd, ToSubmit, MinComplete, EnterFlags::GetEvents);
	if (Ret < 0)
	{
		LastError = -Ret;
		return -1;
	}

	return Ret;
}

const FCqe* FRing::PeekCqe() const
{
	// Load head (we own this)
	uint32 Head = CqHead->load(std::memory_order_relaxed);
	// Load tail (kernel owns this)
	uint32 Tail = CqTail->load(std::memory_order_acquire);

	if (Head == Tail)
	{
		return nullptr;  // No completions available
	}

	return &Cqes[Head & CqMask];
}

const FCqe* FRing::WaitCqe()
{
	// First check if there's already a CQE available
	if (const FCqe* Cqe = PeekCqe())
	{
		return Cqe;
	}

	// Need to wait - use io_uring_enter with GETEVENTS
	int32 Ret = SysIoUringEnter(RingFd, 0, 1, EnterFlags::GetEvents);
	if (Ret < 0)
	{
		LastError = -Ret;
		return nullptr;
	}

	return PeekCqe();
}

void FRing::CqAdvance(uint32 Count)
{
	uint32 Head = CqHead->load(std::memory_order_relaxed);
	CqHead->store(Head + Count, std::memory_order_release);
}

uint32 FRing::GetPendingSubmissions() const
{
	uint32 Head = SqHead->load(std::memory_order_acquire);
	uint32 Tail = SqTail->load(std::memory_order_relaxed);
	return Tail - Head;
}

uint32 FRing::GetAvailableCompletions() const
{
	uint32 Head = CqHead->load(std::memory_order_relaxed);
	uint32 Tail = CqTail->load(std::memory_order_acquire);
	return Tail - Head;
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
