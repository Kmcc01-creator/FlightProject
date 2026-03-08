// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FlightVexTypes.generated.h"

UENUM(BlueprintType)
enum class EFlightVexSymbolResidency : uint8
{
	Shared,  // Available to both Verse (CPU) and HLSL (GPU)
	GpuOnly, // Only available in @gpu blocks or HLSL kernels
	CpuOnly  // Only available in @cpu blocks or Verse scripts
};

UENUM(BlueprintType)
enum class EFlightVexSymbolAffinity : uint8
{
	Any,          // Safe to access from any thread
	GameThread,   // Must only be accessed from the Game Thread
	RenderThread, // Must only be accessed from the Render Thread
	WorkerThread  // Designed for background task parallelism
};
