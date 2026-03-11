#include "Core/FlightActorAdapter.h"

#include "Orchestration/FlightOrchestrationSubsystem.h"

#include "Engine/World.h"

AFlightActorAdapterBase::AFlightActorAdapterBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AFlightActorAdapterBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	OnAdapterConstruction(Transform);
	MarkAdapterDirty(/*bRequestVisibilityRefresh=*/true);
}

void AFlightActorAdapterBase::BeginPlay()
{
	Super::BeginPlay();

	bAdapterRegistered = true;
	OnAdapterRegistered();
	ClearAdapterDirty();
	RequestOrchestrationVisibilityRefresh();
}

void AFlightActorAdapterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnAdapterUnregistered(EndPlayReason);
	bAdapterRegistered = false;
	RequestOrchestrationVisibilityRefresh();

	Super::EndPlay(EndPlayReason);
}

void AFlightActorAdapterBase::MarkAdapterDirty(const bool bRequestVisibilityRefresh)
{
	bAdapterDirty = true;

	if (bRequestVisibilityRefresh)
	{
		RequestOrchestrationVisibilityRefresh();
	}
}

void AFlightActorAdapterBase::RequestOrchestrationVisibilityRefresh() const
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}

	if (UFlightOrchestrationSubsystem* Orchestration = World->GetSubsystem<UFlightOrchestrationSubsystem>())
	{
		Orchestration->RebuildVisibility();
	}
}
