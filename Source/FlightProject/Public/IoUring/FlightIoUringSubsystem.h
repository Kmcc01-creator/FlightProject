// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightProject - Experimental io_uring integration

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightIoRing.h"
#include "Core/FlightAsyncExecutor.h"
#include "FlightIoUringSubsystem.generated.h"

namespace Flight::Async
{
	class FIoUringExecutor;
}

/**
 * UFlightIoUringSubsystem
 *
 * World subsystem managing the async executor backend.
 * Pick the best implementation for the current platform.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightIoUringSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFlightIoUringSubsystem();

	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Get the active async executor */
	Flight::Async::IFlightAsyncExecutor* GetExecutor() { return Executor.Get(); }

	/** Check if the executor is available */
	UFUNCTION(BlueprintCallable, Category = "Flight|Async")
	bool IsAvailable() const;

	/** Get last error code from backend */
	UFUNCTION(BlueprintCallable, Category = "Flight|Async")
	int32 GetLastError() const { return Executor ? Executor->GetLastError() : 0; }

	/** Process pending completions (call from game thread tick) */
	UFUNCTION(BlueprintCallable, Category = "Flight|Async")
	int32 ProcessCompletions();

private:
	TUniquePtr<Flight::Async::IFlightAsyncExecutor> Executor;
};
