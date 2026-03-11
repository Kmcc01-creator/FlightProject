// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/FlightGpuScriptBridge.h"

#include "Async/Async.h"
#include "IoUring/FlightGpuIoUringBridge.h"
#include "IoUring/FlightIoUringSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

void UFlightGpuScriptBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IoUringSubsystem = Collection.InitializeDependency<UFlightIoUringSubsystem>();
	GpuIoUringBridge = Collection.InitializeDependency<UFlightGpuIoUringBridge>();
}

void UFlightGpuScriptBridgeSubsystem::Deinitialize()
{
	FScopeLock Lock(&SubmissionMutex);
	PendingSubmissions.Reset();
	NextSubmissionHandleValue = 1;
	GpuIoUringBridge = nullptr;
	IoUringSubsystem = nullptr;
	Super::Deinitialize();
}

bool UFlightGpuScriptBridgeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return true;
}

bool UFlightGpuScriptBridgeSubsystem::CanSubmit(const FFlightGpuInvocation& Invocation, FString& OutReason) const
{
	if (Invocation.ProgramId.IsNone() && Invocation.BehaviorId == 0)
	{
		OutReason = TEXT("GPU submission requires a program or behavior identity.");
		return false;
	}

	if (Invocation.ExternalTrackingId <= 0 && !Invocation.bAllowDeferredExternalSignal)
	{
		OutReason = TEXT("GPU submission requires either an external tracking id or explicit deferred external signaling.");
		return false;
	}

	OutReason = Invocation.ExternalTrackingId > 0
		? TEXT("GPU submission is valid and can wait on an external tracking id.")
		: TEXT("GPU submission is valid and will wait for an external completion signal.");
	return true;
}

FFlightGpuSubmissionHandle UFlightGpuScriptBridgeSubsystem::Submit(const FFlightGpuInvocation& Invocation, FString& OutDetail)
{
	FFlightGpuSubmissionHandle Handle;

	FString ValidationReason;
	if (!CanSubmit(Invocation, ValidationReason))
	{
		OutDetail = ValidationReason;
		return Handle;
	}

	FPendingGpuSubmission Submission;
	Submission.Invocation = Invocation;
	Submission.Status = EFlightGpuSubmissionStatus::Submitted;
	Submission.ExternalTrackingId = Invocation.ExternalTrackingId;
	Submission.Detail = Invocation.ExternalTrackingId > 0
		? TEXT("Submitted to the native GPU script bridge and waiting on external GPU completion.")
		: TEXT("Submitted to the native GPU script bridge and waiting on an external runtime completion signal.");

	{
		FScopeLock Lock(&SubmissionMutex);
		Handle.Value = NextSubmissionHandleValue++;
		PendingSubmissions.Add(Handle.Value, Submission);
	}

	OutDetail = Submission.Detail;

	if (Invocation.ExternalTrackingId > 0 && GpuIoUringBridge && GpuIoUringBridge->IsAvailable())
	{
		const bool bRegistered = GpuIoUringBridge->SignalGpuCompletion(
			Invocation.ExternalTrackingId,
			[this, Handle]()
			{
				ResolveSubmission(Handle, EFlightGpuSubmissionStatus::Completed, TEXT("GPU completion bridge reported success."));
			},
			[this, Handle]()
			{
				ResolveSubmission(Handle, EFlightGpuSubmissionStatus::Failed, TEXT("GPU completion bridge reported failure."));
			});

		if (!bRegistered)
		{
			ResolveSubmission(
				Handle,
				Invocation.bAllowDeferredExternalSignal ? EFlightGpuSubmissionStatus::Submitted : EFlightGpuSubmissionStatus::Failed,
				Invocation.bAllowDeferredExternalSignal
					? TEXT("GPU completion bridge could not register the tracking id; waiting for deferred external completion instead.")
					: TEXT("GPU completion bridge could not register the tracking id."));
			OutDetail = Invocation.bAllowDeferredExternalSignal
				? TEXT("GPU completion bridge unavailable; waiting for deferred external completion instead.")
				: TEXT("GPU completion bridge failed to register the tracking id.");
		}
	}
	else if (Invocation.ExternalTrackingId > 0 && !Invocation.bAllowDeferredExternalSignal)
	{
		ResolveSubmission(Handle, EFlightGpuSubmissionStatus::Failed, TEXT("GPU completion bridge is unavailable for the requested tracking id."));
		OutDetail = TEXT("GPU completion bridge is unavailable for the requested tracking id.");
	}

	return Handle;
}

EFlightGpuSubmissionStatus UFlightGpuScriptBridgeSubsystem::Poll(FFlightGpuSubmissionHandle Handle, FString* OutDetail) const
{
	FScopeLock Lock(&SubmissionMutex);
	const FPendingGpuSubmission* Submission = PendingSubmissions.Find(Handle.Value);
	if (!Submission)
	{
		if (OutDetail)
		{
			*OutDetail = TEXT("Invalid GPU submission handle.");
		}
		return EFlightGpuSubmissionStatus::Invalid;
	}

	if (OutDetail)
	{
		*OutDetail = Submission->Detail;
	}
	return Submission->Status;
}

bool UFlightGpuScriptBridgeSubsystem::Await(
	FFlightGpuSubmissionHandle Handle,
	TFunction<void(EFlightGpuSubmissionStatus, const FString&)> OnResolved,
	FString& OutReason)
{
	EFlightGpuSubmissionStatus ImmediateStatus = EFlightGpuSubmissionStatus::Invalid;
	FString ImmediateDetail;
	bool bResolveImmediately = false;

	{
		FScopeLock Lock(&SubmissionMutex);
		FPendingGpuSubmission* Submission = PendingSubmissions.Find(Handle.Value);
		if (!Submission)
		{
			OutReason = TEXT("Cannot await an invalid GPU submission handle.");
			return false;
		}

		if (Submission->Status == EFlightGpuSubmissionStatus::Completed
			|| Submission->Status == EFlightGpuSubmissionStatus::Failed)
		{
			ImmediateStatus = Submission->Status;
			ImmediateDetail = Submission->Detail;
			bResolveImmediately = true;
		}
		else
		{
			Submission->CompletionCallbacks.Add(MoveTemp(OnResolved));
			OutReason = TEXT("Await registered with the native GPU script bridge.");
			return true;
		}
	}

	OutReason = TEXT("GPU submission had already reached a terminal state.");
	if (bResolveImmediately)
	{
		OnResolved(ImmediateStatus, ImmediateDetail);
	}
	return true;
}

bool UFlightGpuScriptBridgeSubsystem::CompleteExternalSubmission(
	FFlightGpuSubmissionHandle Handle,
	const bool bSuccess,
	const FString& Detail)
{
	ResolveSubmission(
		Handle,
		bSuccess ? EFlightGpuSubmissionStatus::Completed : EFlightGpuSubmissionStatus::Failed,
		Detail);
	return true;
}

bool UFlightGpuScriptBridgeSubsystem::TryBuildAwaitToken(FFlightGpuSubmissionHandle Handle, FFlightGpuAwaitToken& OutToken) const
{
	FScopeLock Lock(&SubmissionMutex);
	const FPendingGpuSubmission* Submission = PendingSubmissions.Find(Handle.Value);
	if (!Submission)
	{
		return false;
	}

	OutToken.Handle = Handle;
	OutToken.ExternalTrackingId = Submission->ExternalTrackingId;
	return true;
}

void UFlightGpuScriptBridgeSubsystem::ResolveSubmission(
	FFlightGpuSubmissionHandle Handle,
	EFlightGpuSubmissionStatus Status,
	const FString& Detail)
{
	TArray<TFunction<void(EFlightGpuSubmissionStatus, const FString&)>> CompletionCallbacks;

	{
		FScopeLock Lock(&SubmissionMutex);
		FPendingGpuSubmission* Submission = PendingSubmissions.Find(Handle.Value);
		if (!Submission)
		{
			return;
		}

		if (Submission->Status == EFlightGpuSubmissionStatus::Completed
			|| Submission->Status == EFlightGpuSubmissionStatus::Failed)
		{
			return;
		}

		Submission->Status = Status;
		Submission->Detail = Detail;
		CompletionCallbacks = MoveTemp(Submission->CompletionCallbacks);
	}

	auto InvokeCallbacks = [CompletionCallbacks = MoveTemp(CompletionCallbacks), Status, Detail]() mutable
	{
		for (TFunction<void(EFlightGpuSubmissionStatus, const FString&)>& Callback : CompletionCallbacks)
		{
			if (Callback)
			{
				Callback(Status, Detail);
			}
		}
	};

	if (IsInGameThread())
	{
		InvokeCallbacks();
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, MoveTemp(InvokeCallbacks));
	}
}
