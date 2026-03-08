// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/UFlightVexTaskSubsystem.h"
#include "AutoRTFM.h"

UE::Tasks::FTask UFlightVexTaskSubsystem::LaunchVexJob(uint32 BehaviorID, int32 TaskIndex, TFunction<void()>&& TaskBody)
{
	// Generate a unique debug name for the task
	FString DebugName = FString::Printf(TEXT("VexJob_%u_%d"), BehaviorID, TaskIndex);

	// Launch the task using Unreal's Task Graph
	return UE::Tasks::Launch(
		*DebugName,
		[TaskBody = MoveTemp(TaskBody)]()
		{
			// Every off-thread VEX job is wrapped in a transaction for safety
			AutoRTFM::Transact([&]()
			{
				TaskBody();
			});
		},
		LowLevelTasks::ETaskPriority::Normal
	);
}

void UFlightVexTaskSubsystem::WaitAll(const TArray<UE::Tasks::FTask>& InTasks)
{
	if (InTasks.Num() > 0)
	{
		UE::Tasks::Wait(InTasks);
	}
}
