// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Flight::Orchestration
{

enum class EFlightExecutionDomain : uint8
{
	Unknown,
	NativeCpu,
	TaskGraph,
	VerseVm,
	Simd,
	Gpu
};

struct FLIGHTPROJECT_API FFlightBehaviorRecord
{
	uint32 BehaviorID = 0;
	FName Name = NAME_None;
	FString CompileState;
	float ExecutionRateHz = 0.0f;
	uint32 FrameInterval = 1;
	bool bAsync = false;
	bool bExecutable = false;
	EFlightExecutionDomain ResolvedDomain = EFlightExecutionDomain::Unknown;
	TArray<FName> RequiredContracts;
	FString Diagnostics;
};

struct FLIGHTPROJECT_API FFlightBehaviorBinding
{
	FName CohortName = NAME_None;
	uint32 BehaviorID = 0;
	EFlightExecutionDomain ExecutionDomain = EFlightExecutionDomain::Unknown;
	uint32 FrameInterval = 1;
	bool bAsync = false;
	TArray<FName> RequiredContracts;
};

} // namespace Flight::Orchestration

