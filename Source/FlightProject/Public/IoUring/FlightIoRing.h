// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#pragma once

#include "CoreMinimal.h"
#include "FlightIoUringTypes.h"

#if PLATFORM_LINUX

namespace Flight::IoUring
{

/**
 * Native io_uring Ring
 *
 * Low-level io_uring instance with direct syscall interface.
 * Each instance manages its own SQ/CQ rings and SQE array.
 *
 * Thread safety:
 * - Single writer (SQ operations) expected
 * - CQ can be read concurrently with SQ writes
 * - Use SINGLE_ISSUER flag for per-thread rings
 *
 * Design principles (from m2/systems):
 * - Zero libc dependency for io_uring ops
 * - Explicit buffer lifetime management
 * - Direct error returns (no errno)
 */
class FLIGHTPROJECT_API FRing
{
public:
	/** Default constructor (uninitialized) */
	FRing();

	/** Destructor - unmaps rings and closes fd */
	~FRing();

	// Non-copyable
	FRing(const FRing&) = delete;
	FRing& operator=(const FRing&) = delete;

	// Movable
	FRing(FRing&& Other) noexcept;
	FRing& operator=(FRing&& Other) noexcept;

	/**
	 * Initialize the ring
	 *
	 * @param Entries Queue depth (will be rounded up to power of 2)
	 * @param Flags Setup flags (SetupFlags::*)
	 * @return true on success, false on failure (check GetLastError())
	 */
	bool Initialize(uint32 Entries, uint32 Flags = 0);

	/** Check if ring is initialized */
	bool IsValid() const { return RingFd >= 0; }

	/** Get ring file descriptor (for MSG_RING targeting) */
	int32 GetFd() const { return RingFd; }

	/** Get last error code */
	int32 GetLastError() const { return LastError; }

	// ========== SQE Operations ==========

	/**
	 * Get next available SQE slot
	 *
	 * @return Pointer to SQE, or nullptr if queue is full
	 */
	FSqe* GetSqe();

	/**
	 * Submit pending SQEs to kernel
	 *
	 * @return Number of SQEs submitted, or -1 on error
	 */
	int32 Submit();

	/**
	 * Submit and wait for at least MinComplete completions
	 *
	 * @param MinComplete Minimum completions to wait for
	 * @return Number of SQEs submitted, or -1 on error
	 */
	int32 SubmitAndWait(uint32 MinComplete);

	// ========== CQE Operations ==========

	/**
	 * Peek at next CQE without consuming
	 *
	 * @return Pointer to CQE, or nullptr if none available
	 */
	const FCqe* PeekCqe() const;

	/**
	 * Wait for at least one CQE
	 *
	 * @return Pointer to CQE, or nullptr on error
	 */
	const FCqe* WaitCqe();

	/**
	 * Advance CQ head (mark CQEs as consumed)
	 *
	 * @param Count Number of CQEs to advance
	 */
	void CqAdvance(uint32 Count);

	/**
	 * Process all available CQEs
	 *
	 * @param Callback Function to call for each CQE
	 * @return Number of CQEs processed
	 */
	template<typename Func>
	uint32 ForEachCqe(Func&& Callback);

	// ========== Statistics ==========

	/** Get SQ entries capacity */
	uint32 GetSqCapacity() const { return SqEntries; }

	/** Get CQ entries capacity */
	uint32 GetCqCapacity() const { return CqEntries; }

	/** Get number of pending submissions */
	uint32 GetPendingSubmissions() const;

	/** Get number of available completions */
	uint32 GetAvailableCompletions() const;

private:
	// Ring file descriptor
	int32 RingFd = -1;

	// Ring parameters
	uint32 SqEntries = 0;
	uint32 CqEntries = 0;
	uint32 SqMask = 0;
	uint32 CqMask = 0;

	// SQ pointers (mmap'd)
	std::atomic<uint32>* SqHead = nullptr;  // Kernel-owned (read-only)
	std::atomic<uint32>* SqTail = nullptr;  // Userspace-owned
	uint32* SqArray = nullptr;
	FSqe* Sqes = nullptr;

	// CQ pointers (mmap'd)
	std::atomic<uint32>* CqHead = nullptr;  // Userspace-owned
	std::atomic<uint32>* CqTail = nullptr;  // Kernel-owned (read-only)
	FCqe* Cqes = nullptr;

	// mmap regions for cleanup
	void* SqMmap = nullptr;
	SIZE_T SqMmapSize = 0;
	void* CqMmap = nullptr;
	SIZE_T CqMmapSize = 0;
	void* SqesMmap = nullptr;
	SIZE_T SqesMmapSize = 0;

	// Error tracking
	int32 LastError = 0;

	// Internal helpers
	void Cleanup();
	bool SetupMmaps(const FParams& Params);
};

// ============================================================================
// Template Implementation
// ============================================================================

template<typename Func>
uint32 FRing::ForEachCqe(Func&& Callback)
{
	uint32 Count = 0;
	while (const FCqe* Cqe = PeekCqe())
	{
		Callback(*Cqe);
		CqAdvance(1);
		++Count;
	}
	return Count;
}

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
