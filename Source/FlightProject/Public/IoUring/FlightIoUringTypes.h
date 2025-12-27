// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_LINUX

/**
 * Native Linux io_uring types and constants
 *
 * These match the kernel definitions from linux/io_uring.h
 * We define them here to avoid header dependencies and ensure
 * we have exactly what we need.
 *
 * Design philosophy (from m2/systems):
 * - Zero external dependencies for core types
 * - Direct syscall interface (no libc wrappers)
 * - Explicit control over all behavior
 */

namespace Flight::IoUring
{

// ============================================================================
// Kernel Structures (must match linux/io_uring.h exactly)
// ============================================================================

/**
 * Submission Queue Entry (64 bytes)
 *
 * This is the unit of work submitted to io_uring. Each SQE describes
 * a single I/O operation to be performed by the kernel.
 */
struct alignas(64) FSqe
{
	uint8  Opcode;           // Operation code (IORING_OP_*)
	uint8  Flags;            // SQE flags (IOSQE_*)
	uint16 IoPrio;           // I/O priority
	int32  Fd;               // File descriptor
	uint64 OffOrAddr2;       // Offset or second address
	uint64 AddrOrSpliceOff;  // Address or splice offset
	uint32 Len;              // Length
	uint32 OpFlags;          // Operation-specific flags
	uint64 UserData;         // User data (returned in CQE)
	uint16 BufIndexOrGroup;  // Buffer index or group
	uint16 Personality;      // Personality
	int32  SpliceFdOrFileIndex;
	uint64 Addr3;            // Third address
	uint64 Resv;             // Reserved

	/** Zero all fields */
	void Zero() { FMemory::Memzero(this, sizeof(*this)); }

	/** Prepare READ operation */
	void PrepRead(int32 InFd, void* Buf, uint32 Size, uint64 Offset, uint64 InUserData);

	/** Prepare WRITE operation */
	void PrepWrite(int32 InFd, const void* Buf, uint32 Size, uint64 Offset, uint64 InUserData);

	/** Prepare CLOSE operation */
	void PrepClose(int32 InFd, uint64 InUserData);

	/** Prepare POLL_ADD operation */
	void PrepPollAdd(int32 InFd, uint32 PollMask, uint64 InUserData);

	/** Prepare POLL_ADD multishot */
	void PrepPollMultishot(int32 InFd, uint32 PollMask, uint64 InUserData);

	/** Prepare MSG_RING (inter-ring messaging) */
	void PrepMsgRing(int32 TargetFd, uint64 Payload, uint32 MsgTag, uint64 InUserData);

	/** Prepare MSG_RING with CQE flags passthrough */
	void PrepMsgRingWithFlags(int32 TargetFd, uint64 Payload, uint32 MsgTag,
	                          uint32 CqeFlags, uint64 InUserData);

	/** Prepare NOP (useful for testing and wakeups) */
	void PrepNop(uint64 InUserData);

	/** Prepare FSYNC */
	void PrepFsync(int32 InFd, uint32 FsyncFlags, uint64 InUserData);

	/** Set IO_LINK flag (chain with next SQE) */
	void SetLink() { Flags |= IOSQE_IO_LINK; }

	/** Set ASYNC flag (force async execution) */
	void SetAsync() { Flags |= IOSQE_ASYNC; }

	// SQE flag constants
	static constexpr uint8 IOSQE_FIXED_FILE     = 1 << 0;
	static constexpr uint8 IOSQE_IO_DRAIN       = 1 << 1;
	static constexpr uint8 IOSQE_IO_LINK        = 1 << 2;
	static constexpr uint8 IOSQE_IO_HARDLINK    = 1 << 3;
	static constexpr uint8 IOSQE_ASYNC          = 1 << 4;
	static constexpr uint8 IOSQE_BUFFER_SELECT  = 1 << 5;
	static constexpr uint8 IOSQE_CQE_SKIP_SUCCESS = 1 << 6;
};
static_assert(sizeof(FSqe) == 64, "FSqe must be 64 bytes");

/**
 * Completion Queue Entry (16 bytes)
 *
 * Returned by the kernel when an operation completes.
 */
struct FCqe
{
	uint64 UserData;  // User data from SQE
	int32  Res;       // Result (bytes transferred or negative errno)
	uint32 Flags;     // CQE flags (IORING_CQE_F_*)

	/** Check if this is a MSG_RING message */
	bool IsMsgRing() const { return (Flags & MSG_RING_CQE_MARKER) != 0; }

	/** Check if operation succeeded */
	bool IsSuccess() const { return Res >= 0; }

	/** Get errno (only valid if Res < 0) */
	int32 GetErrno() const { return -Res; }

	// CQE flag constants
	static constexpr uint32 IORING_CQE_F_BUFFER      = 1 << 0;
	static constexpr uint32 IORING_CQE_F_MORE        = 1 << 1;
	static constexpr uint32 IORING_CQE_F_SOCK_NONEMPTY = 1 << 2;
	static constexpr uint32 IORING_CQE_F_NOTIF       = 1 << 3;

	/** Custom marker for MSG_RING CQEs (bit 31, safe for userspace) */
	static constexpr uint32 MSG_RING_CQE_MARKER = 1 << 31;
};
static_assert(sizeof(FCqe) == 16, "FCqe must be 16 bytes");

/**
 * Submission ring offsets (returned from io_uring_setup)
 */
struct FSqRingOffsets
{
	uint32 Head;
	uint32 Tail;
	uint32 RingMask;
	uint32 RingEntries;
	uint32 Flags;
	uint32 Dropped;
	uint32 Array;
	uint32 Resv1;
	uint64 UserAddr;
};

/**
 * Completion ring offsets (returned from io_uring_setup)
 */
struct FCqRingOffsets
{
	uint32 Head;
	uint32 Tail;
	uint32 RingMask;
	uint32 RingEntries;
	uint32 Overflow;
	uint32 Cqes;
	uint32 Flags;
	uint32 Resv1;
	uint64 UserAddr;
};

/**
 * Parameters for io_uring_setup()
 */
struct FParams
{
	uint32 SqEntries;
	uint32 CqEntries;
	uint32 Flags;
	uint32 SqThreadCpu;
	uint32 SqThreadIdle;
	uint32 Features;
	uint32 WqFd;
	uint32 Resv[3];
	FSqRingOffsets SqOff;
	FCqRingOffsets CqOff;
};

// ============================================================================
// Operation Codes
// ============================================================================

enum class EOpcode : uint8
{
	Nop           = 0,
	Readv         = 1,
	Writev        = 2,
	Fsync         = 3,
	ReadFixed     = 4,
	WriteFixed    = 5,
	PollAdd       = 6,
	PollRemove    = 7,
	SyncFileRange = 8,
	Sendmsg       = 9,
	Recvmsg       = 10,
	Timeout       = 11,
	TimeoutRemove = 12,
	Accept        = 13,
	AsyncCancel   = 14,
	LinkTimeout   = 15,
	Connect       = 16,
	Fallocate     = 17,
	Openat        = 18,
	Close         = 19,
	FilesUpdate   = 20,
	Statx         = 21,
	Read          = 22,
	Write         = 23,
	Fadvise       = 24,
	Madvise       = 25,
	Send          = 26,
	Recv          = 27,
	Openat2       = 28,
	EpollCtl      = 29,
	Splice        = 30,
	ProvideBuffers = 31,
	RemoveBuffers = 32,
	// ... gap for ops we don't use ...
	MsgRing       = 40,  // Inter-ring messaging (kernel 5.18+)
};

// ============================================================================
// Setup Flags
// ============================================================================

namespace SetupFlags
{
	constexpr uint32 IoPoll       = 1 << 0;
	constexpr uint32 SqPoll       = 1 << 1;
	constexpr uint32 SqAff        = 1 << 2;
	constexpr uint32 CqSize       = 1 << 3;
	constexpr uint32 Clamp        = 1 << 4;
	constexpr uint32 AttachWq     = 1 << 5;
	constexpr uint32 SingleIssuer = 1 << 12;  // Kernel 6.0+
	constexpr uint32 CoopTaskrun  = 1 << 8;   // Kernel 6.0+
	constexpr uint32 DeferTaskrun = 1 << 13;  // Kernel 6.1+
}

// ============================================================================
// Enter Flags
// ============================================================================

namespace EnterFlags
{
	constexpr uint32 GetEvents = 1 << 0;
	constexpr uint32 SqWakeup  = 1 << 1;
	constexpr uint32 SqWait    = 1 << 2;
}

// ============================================================================
// MSG_RING Constants
// ============================================================================

namespace MsgRing
{
	/** Data message mode */
	constexpr uint32 MsgData   = 0;
	/** FD passing mode */
	constexpr uint32 MsgSendFd = 1;
	/** Pass CQE flags to target */
	constexpr uint32 FlagsPass = 1 << 1;
}

// ============================================================================
// Poll Flags
// ============================================================================

namespace PollFlags
{
	constexpr uint32 AddMulti       = 1 << 0;
	constexpr uint32 UpdateEvents   = 1 << 1;
	constexpr uint32 UpdateUserData = 1 << 2;
	constexpr uint32 AddLevel       = 1 << 3;
}

// ============================================================================
// mmap Offsets
// ============================================================================

namespace MmapOffsets
{
	constexpr uint64 SqRing = 0;
	constexpr uint64 CqRing = 0x8000000;
	constexpr uint64 Sqes   = 0x10000000;
}

// ============================================================================
// Syscall Numbers (x86_64)
// ============================================================================

namespace Syscall
{
	constexpr long IoUringSetup    = 425;
	constexpr long IoUringEnter    = 426;
	constexpr long IoUringRegister = 427;
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
