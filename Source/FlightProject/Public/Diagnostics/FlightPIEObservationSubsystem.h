#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "FlightPIEObservationSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FFlightObservedActorEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString EventType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString ActorName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString ActorLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString ActorClassPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString ActorModulePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString LevelPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString PackagePath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString OwnerName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString InstigatorName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	FString SourceHint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	bool bWasLoaded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	double WallSecondsSinceTraceStart = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Observability")
	float WorldSeconds = 0.0f;
};

/**
 * PIE-only subsystem that records actor origins/timing for "what loaded from where, and when".
 * Writes JSON snapshots for schema/observation workflows.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightPIEObservationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Flight|Observability")
	int32 GetObservedEventCount() const { return Events.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Flight|Observability")
	FString ExportObservationSnapshot(
		const FString& RelativeOutputPath = TEXT("Saved/Flight/Observations/pie_entity_trace.json")) const;

private:
	void CaptureInitialActors();
	void HandleActorSpawned(AActor* SpawnedActor);
	void RecordActorEvent(AActor* Actor, const TCHAR* EventType);
	FString BuildDefaultRelativeExportPath() const;

private:
	bool bTracingActive = false;
	double TraceStartWallSeconds = 0.0;
	FDelegateHandle SpawnedHandle;

	UPROPERTY(Transient)
	TArray<FFlightObservedActorEvent> Events;
};
