// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightAsyncExecutor - Unified Cross-Platform Async Interface
//
// Provides a universal abstraction for async I/O and GPU synchronization.
// Optimized for Linux io_uring but supports Windows and Generic fallbacks.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightReactive.h"

namespace Flight::Async
{

using namespace Flight::Functional;
using namespace Flight::Reactive;

/**
 * Platform-agnostic handle for async resources (File Descriptors, Win32 Handles, etc.)
 */
struct FAsyncHandle
{
	uint64 NativeValue = 0;

	bool IsValid() const { return NativeValue != 0; }
	
	static FAsyncHandle FromLinuxFd(int32 Fd) { return { static_cast<uint64>(Fd) }; }
	static FAsyncHandle FromWindowsHandle(void* Handle) { return { reinterpret_cast<uintptr_t>(Handle) }; }
	
	int32 AsLinuxFd() const { return static_cast<int32>(NativeValue); }
	void* AsWindowsHandle() const { return reinterpret_cast<void*>(static_cast<uintptr_t>(NativeValue)); }
};

/**
 * Result of an async operation
 */
struct FAsyncResult
{
	int32 ErrorCode = 0;
	uint32 BytesTransferred = 0;
	uint32 Flags = 0;

	bool IsSuccess() const { return ErrorCode >= 0; }
};

/**
 * IFlightAsyncExecutor
 * Interface for the high-performance async backend.
 */
class IFlightAsyncExecutor
{
public:
	virtual ~IFlightAsyncExecutor() = default;

	// ─────────────────────────────────────────────────────────────
	// Core Lifecycle
	// ─────────────────────────────────────────────────────────────

	/** Initialize the executor for the current platform */
	virtual bool Initialize() = 0;

	/** Process pending completions (call from game thread tick) */
	virtual int32 Tick() = 0;

	/** Get last error code from backend */
	virtual int32 GetLastError() const = 0;

	// ─────────────────────────────────────────────────────────────
	// Async I/O Operations
	// ─────────────────────────────────────────────────────────────

	using FCompletionCallback = TFunction<void(const FAsyncResult&)>;

	/** Submit an async read request */
	virtual bool SubmitRead(
		FAsyncHandle Handle,
		void* Buffer,
		uint32 Size,
		uint64 Offset,
		FCompletionCallback Callback) = 0;

	/** Submit an async write request */
	virtual bool SubmitWrite(
		FAsyncHandle Handle,
		const void* Buffer,
		uint32 Size,
		uint64 Offset,
		FCompletionCallback Callback) = 0;

	// ─────────────────────────────────────────────────────────────
	// GPU & Event Synchronization
	// ─────────────────────────────────────────────────────────────

	/** 
	 * Wait for a GPU tracking ID to complete.
	 * Returns a TReactiveValue that triggers when the work is finished.
	 */
	virtual TSharedPtr<TReactiveValue<bool>> WaitOnGpuWork(int64 TrackingId) = 0;

	/**
	 * Abstract polling/event wait.
	 * Linux: POLL_ADD on sync_fd.
	 * Windows: RegisterWaitForSingleObject.
	 */
	virtual bool SubmitEventWait(FAsyncHandle EventHandle, FCompletionCallback Callback) = 0;

	// ─────────────────────────────────────────────────────────────
	// Capabilities
	// ─────────────────────────────────────────────────────────────

	virtual bool IsIoUringOptimized() const = 0;
	virtual const TCHAR* GetBackendName() const = 0;
};

/**
 * Global factory for the best available executor
 */
FLIGHTPROJECT_API TUniquePtr<IFlightAsyncExecutor> CreatePlatformExecutor();

} // namespace Flight::Async
