// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightMegaKernelSubsystem.generated.h"

/**
 * UFlightMegaKernelSubsystem: Coordinating abstraction for GPU Mega-Kernel synthesis.
 * Observes the Orchestration Subsystem to generate unified HLSL for GPU-bound VEX behaviors.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightMegaKernelSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Get the current synthesized Mega-Kernel HLSL. */
	const FString& GetMegaKernelSource() const { return MegaKernelSource; }

	/** Trigger a manual synthesis of the Mega-Kernel. */
	void Synthesize();

private:
	void OnExecutionPlanUpdated();

	FString MegaKernelSource;
	uint32 LastPlanGeneration = 0;
};
