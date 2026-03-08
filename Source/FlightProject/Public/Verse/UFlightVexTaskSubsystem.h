// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"
#include "VerseVM/VVMContext.h"
#include "UFlightVexTaskSubsystem.generated.h"

/**
 * UFlightVexTaskSubsystem
 * 
 * Manages the asynchronous execution of VEX-generated jobs using UE::Tasks.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightVexTaskSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Launches a VEX job on the task graph.
	 * 
	 * @param BehaviorID The behavior containing the @job block.
	 * @param TaskIndex The index of the @job block in the program's Tasks array.
	 * @param TaskBody A lambda containing the actual Verse/VM execution logic.
	 */
	UE::Tasks::FTask LaunchVexJob(uint32 BehaviorID, int32 TaskIndex, TFunction<void()>&& TaskBody);

	/**
	 * Wait for all pending VEX tasks to complete.
	 * Typically called at the end of a simulation tick sync point.
	 */
	void WaitAll(const TArray<UE::Tasks::FTask>& InTasks);

private:
	// In a full implementation, we might track active tasks here for debugging
};
