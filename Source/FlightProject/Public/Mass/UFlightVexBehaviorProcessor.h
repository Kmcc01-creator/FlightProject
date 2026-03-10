// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "UFlightVexBehaviorProcessor.generated.h"

class UFlightVerseSubsystem;
class UWorld;

namespace Flight::Orchestration
{
struct FFlightBehaviorBinding;
}

/**
 * Mass processor that executes VEX-generated Verse behaviors.
 * 
 * Uses the @rate metadata from VEX to schedule execution frequencies
 * across entity chunks efficiently.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightVexBehaviorProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UFlightVexBehaviorProcessor();
	static bool ResolveBehaviorSelection(
		UWorld* World,
		const UFlightVerseSubsystem& VerseSubsystem,
		uint32& OutBehaviorID,
		Flight::Orchestration::FFlightBehaviorBinding* OutBinding = nullptr,
		FName CohortName = NAME_None);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
