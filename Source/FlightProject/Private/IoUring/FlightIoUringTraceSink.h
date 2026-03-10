// Copyright Kelly Rey Wilson. All Rights Reserved.
// io_uring powered high-performance Trace Sink for Unreal Engine

#pragma once

#include "CoreMinimal.h"
#include "IoUring/FlightIoRing.h"
#include "HAL/Runnable.h"
#include "Containers/Queue.h"

#if PLATFORM_LINUX

namespace Flight::IoUring
{

/**
 * FFlightIoUringTraceSink
 */
class FLIGHTPROJECT_API FFlightIoUringTraceSink : public FRunnable
{
public:
	FFlightIoUringTraceSink();
	virtual ~FFlightIoUringTraceSink();

	bool Start(const FString& FilePath);
	void Stop();

	bool IsActive() const { return bIsActive; }

	// FRunnable interface
	virtual uint32 Run() override;
	virtual void Exit() override {}
	virtual void Stop_Runnable() { bShouldExit = true; }

private:
	// Constant for slab size
	static constexpr uint32 SLAB_SIZE = 64 * 1024;
	static constexpr uint32 NUM_SLABS = 128;

	struct FSlab
	{
		uint8 Data[SLAB_SIZE];
		uint32 Used = 0;
		std::atomic<bool> bInFlight{false};
		uint32 Index = 0;
	};

	struct FWriteRequest
	{
		FSlab* Slab;
	};

	FRing IoRing;
	
	// Message Pump: TraceWorker producer -> CompletionThread consumer
	TQueue<FWriteRequest, EQueueMode::Mpsc> WriteQueue;
	FEvent* WakeEvent = nullptr;

	int32 FileFd = -1;
	std::atomic<uint64> FileOffset{0};
	bool bIsActive = false;
	std::atomic<bool> bStopRequested{false};

	// Quiescence tracking
	std::atomic<int32> ActiveWriters{0};
	std::atomic<int32> OutstandingWrites{0};

	// Slab pool
	TUniquePtr<FSlab[]> Slabs;
	std::atomic<uint32> CurrentSlabIndex{0};

	// Dedicated thread owns the FRing entirely
	FRunnableThread* CompletionThread = nullptr;
	std::atomic<bool> bShouldExit{false};
	
	// Unreal Trace callbacks
	static bool TraceWrite(UPTRINT Handle, const void* Data, uint32 Size);
	static void TraceClose(UPTRINT Handle);

	// Singleton instance for the callbacks
	static FFlightIoUringTraceSink* Instance;
};

} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
