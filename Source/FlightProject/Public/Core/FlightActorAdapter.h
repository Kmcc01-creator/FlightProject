#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightActorAdapter.generated.h"

/**
 * Narrow lifecycle base for actor-backed adapter surfaces.
 * Concrete adapters should implement domain-specific lowering and registration
 * in hook methods rather than overriding AActor lifecycle directly.
 */
UCLASS(Abstract)
class FLIGHTPROJECT_API AFlightActorAdapterBase : public AActor
{
	GENERATED_BODY()

public:
	AFlightActorAdapterBase();

	virtual void OnConstruction(const FTransform& Transform) override final;
	virtual void BeginPlay() override final;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override final;

	bool IsAdapterRegistered() const { return bAdapterRegistered; }
	bool IsAdapterDirty() const { return bAdapterDirty; }

protected:
	void MarkAdapterDirty(bool bRequestVisibilityRefresh = true);
	void ClearAdapterDirty() { bAdapterDirty = false; }
	void RequestOrchestrationVisibilityRefresh() const;

	virtual void OnAdapterConstruction(const FTransform& Transform) {}
	virtual void OnAdapterRegistered() {}
	virtual void OnAdapterUnregistered(const EEndPlayReason::Type EndPlayReason) {}

private:
	UPROPERTY(Transient, VisibleAnywhere, Category = "Flight|Adapter")
	bool bAdapterRegistered = false;

	UPROPERTY(Transient, VisibleAnywhere, Category = "Flight|Adapter")
	bool bAdapterDirty = true;
};
