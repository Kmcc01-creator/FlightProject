// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightGpuScriptBridge.generated.h"

class UFlightGpuIoUringBridge;
class UFlightIoUringSubsystem;

UENUM(BlueprintType)
enum class EFlightGpuLatencyClass : uint8
{
	SameFrameGpu UMETA(DisplayName = "SameFrameGpu"),
	NextFrameGpu UMETA(DisplayName = "NextFrameGpu"),
	CpuObserved UMETA(DisplayName = "CpuObserved")
};

UENUM(BlueprintType)
enum class EFlightGpuSubmissionStatus : uint8
{
	Invalid UMETA(DisplayName = "Invalid"),
	Submitted UMETA(DisplayName = "Submitted"),
	Completed UMETA(DisplayName = "Completed"),
	Failed UMETA(DisplayName = "Failed")
};

USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightGpuResourceBinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	FName Name = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	FName ContractKey = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	FString SymbolName;
};

USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightGpuInvocation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	FName ProgramId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	int32 BehaviorId = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	EFlightGpuLatencyClass LatencyClass = EFlightGpuLatencyClass::NextFrameGpu;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	TArray<FName> RequiredContracts;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	TArray<FFlightGpuResourceBinding> ResourceBindings;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	int64 ExternalTrackingId = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	bool bAwaitRequested = false;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	bool bMirrorRequested = false;

	UPROPERTY(BlueprintReadWrite, Category = "Flight|Gpu")
	bool bAllowDeferredExternalSignal = false;
};

USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightGpuSubmissionHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Flight|Gpu")
	int64 Value = 0;

	bool IsValid() const
	{
		return Value != 0;
	}
};

USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightGpuAwaitToken
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Flight|Gpu")
	FFlightGpuSubmissionHandle Handle;

	UPROPERTY(BlueprintReadOnly, Category = "Flight|Gpu")
	int64 ExternalTrackingId = 0;
};

class FLIGHTPROJECT_API IFlightGpuScriptBridge
{
public:
	virtual ~IFlightGpuScriptBridge() = default;

	virtual bool CanSubmit(const FFlightGpuInvocation& Invocation, FString& OutReason) const = 0;
	virtual FFlightGpuSubmissionHandle Submit(const FFlightGpuInvocation& Invocation, FString& OutDetail) = 0;
	virtual EFlightGpuSubmissionStatus Poll(FFlightGpuSubmissionHandle Handle, FString* OutDetail = nullptr) const = 0;
	virtual bool Await(
		FFlightGpuSubmissionHandle Handle,
		TFunction<void(EFlightGpuSubmissionStatus, const FString&)> OnResolved,
		FString& OutReason) = 0;
	virtual bool CompleteExternalSubmission(
		FFlightGpuSubmissionHandle Handle,
		bool bSuccess,
		const FString& Detail) = 0;
};

UCLASS()
class FLIGHTPROJECT_API UFlightGpuScriptBridgeSubsystem : public UWorldSubsystem, public IFlightGpuScriptBridge
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual bool CanSubmit(const FFlightGpuInvocation& Invocation, FString& OutReason) const override;
	virtual FFlightGpuSubmissionHandle Submit(const FFlightGpuInvocation& Invocation, FString& OutDetail) override;
	virtual EFlightGpuSubmissionStatus Poll(FFlightGpuSubmissionHandle Handle, FString* OutDetail = nullptr) const override;
	virtual bool Await(
		FFlightGpuSubmissionHandle Handle,
		TFunction<void(EFlightGpuSubmissionStatus, const FString&)> OnResolved,
		FString& OutReason) override;
	virtual bool CompleteExternalSubmission(
		FFlightGpuSubmissionHandle Handle,
		bool bSuccess,
		const FString& Detail) override;

	bool TryBuildAwaitToken(FFlightGpuSubmissionHandle Handle, FFlightGpuAwaitToken& OutToken) const;

private:
	struct FPendingGpuSubmission
	{
		FFlightGpuInvocation Invocation;
		EFlightGpuSubmissionStatus Status = EFlightGpuSubmissionStatus::Invalid;
		FString Detail;
		int64 ExternalTrackingId = 0;
		TArray<TFunction<void(EFlightGpuSubmissionStatus, const FString&)>> CompletionCallbacks;
	};

	void ResolveSubmission(
		FFlightGpuSubmissionHandle Handle,
		EFlightGpuSubmissionStatus Status,
		const FString& Detail);

	mutable FCriticalSection SubmissionMutex;
	TMap<int64, FPendingGpuSubmission> PendingSubmissions;
	int64 NextSubmissionHandleValue = 1;

	UPROPERTY()
	UFlightGpuIoUringBridge* GpuIoUringBridge = nullptr;

	UPROPERTY()
	UFlightIoUringSubsystem* IoUringSubsystem = nullptr;
};
