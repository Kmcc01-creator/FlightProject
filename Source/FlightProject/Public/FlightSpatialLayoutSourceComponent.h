#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "FlightDataTypes.h"
#include "FlightSpatialLayoutSourceComponent.generated.h"

/**
 * Component that exposes layout rows to the spatial layout director, allowing
 * designer-authored actors to contribute procedural spawn points.
 */
UCLASS(ClassGroup = (Flight), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class FLIGHTPROJECT_API UFlightSpatialLayoutSourceComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UFlightSpatialLayoutSourceComponent();

    /** Append this component's layout rows (in world space) to the provided array. */
    void GatherLayoutRows(TArray<FFlightSpatialLayoutRow>& OutRows) const;

    /** Replace the generated rows used when gathering layout. */
    void SetGeneratedRows(const TArray<FFlightSpatialLayoutRow>& InRows);

    /** Clears any generated rows so only template rows are returned. */
    void ResetGeneratedRows();

    /** Template rows authored in the component that are automatically transformed into world space. */
    UPROPERTY(EditAnywhere, Category = "Layout")
    TArray<FFlightSpatialLayoutRow> TemplateRows;

private:
    UPROPERTY(Transient)
    TArray<FFlightSpatialLayoutRow> GeneratedRows;

    void AppendWorldSpaceRow(const FFlightSpatialLayoutRow& SourceRow, const FTransform& Transform, TArray<FFlightSpatialLayoutRow>& OutRows) const;
};
