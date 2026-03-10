// Copyright Kelly Rey Wilson. All Rights Reserved.
// Specialized io_uring reactor for Vulkan synchronization

#pragma once

#include "CoreMinimal.h"
#include "IoUring/FlightIoRing.h"
#include "IoUring/FlightExportableSemaphore.h"
#include "Containers/Queue.h"

#if PLATFORM_LINUX

namespace Flight::IoUring
{

/**
 * Result of a GPU sync operation
 */
enum class ESyncResult : uint8
{
	Success,
	Canceled,
	Error
};

/**
 * Managed state for a pending GPU synchronization point.
 */
struct FSyncFdState : public TSharedFromThis<FSyncFdState>
{
	uint64 TrackingId = 0;
	int32 SyncFd = -1;
	uint64 UserData = 0;

	TUniquePtr<FExportableSemaphore> BinarySemaphore;
	TFunction<void(ESyncResult, int32)> Callback;

	std::atomic<bool> bPollArmed{false};
	std::atomic<bool> bTerminal{false};

	enum class EPhase : uint8
	{
		Created,
		Exported,
		Queued,
		Armed,
		Completed,
		Finalized
	};
	std::atomic<EPhase> Phase{EPhase::Created};

	FSyncFdState(uint64 InId) : TrackingId(InId) {}
	~FSyncFdState()
	{
		if (SyncFd >= 0)
		{
			close(SyncFd);
			SyncFd = -1;
		}
	}
};

/**
 * FVulkanIoUringReactor
 */
class FLIGHTPROJECT_API FVulkanIoUringReactor
{
public:
	FVulkanIoUringReactor();
	~FVulkanIoUringReactor();

	bool Initialize(uint32 Entries = 128);
	void Shutdown();

	bool ArmSyncPoint(
		VkQueue Queue,
		VkSemaphore Timeline,
		uint64 Value,
		uint64 TrackingId,
		TFunction<void(ESyncResult, int32)> Callback);

	/** Process completions and pump new requests (call from game thread) */
	int32 Tick();

	bool IsAvailable() const { return Ring.IsValid(); }

private:
	struct FSyncRequest
	{
		TSharedPtr<FSyncFdState> State;
		FSyncRequest() = default;
		FSyncRequest(TSharedPtr<FSyncFdState> InState) : State(InState) {}
	};

	FRing Ring;
	std::atomic<bool> bShutdownRequested{false};

	// Quiescence tracking
	std::atomic<int32> ActiveProducers{0};
	std::atomic<int32> OutstandingArmedOps{0};

	// Message Pump: ArmSyncPoint producers -> Tick consumer
	TQueue<FSyncRequest, EQueueMode::Mpsc> RequestQueue;

	FCriticalSection MapCS;
	TMap<uint64, TSharedRef<FSyncFdState>> PendingStates;
	std::atomic<uint64> NextUserData{1};

	void HandleCompletion(const FCqe& Cqe);
	void FinalizeState(TSharedRef<FSyncFdState> State, ESyncResult Result, int32 ErrorCode);
};} // namespace Flight::IoUring

#endif // PLATFORM_LINUX
