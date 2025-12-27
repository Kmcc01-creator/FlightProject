#include "FlightSpatialLayoutSourceComponent.h"
#include "GameFramework/Actor.h"

UFlightSpatialLayoutSourceComponent::UFlightSpatialLayoutSourceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UFlightSpatialLayoutSourceComponent::GatherLayoutRows(TArray<FFlightSpatialLayoutRow>& OutRows) const
{
    const FTransform ComponentTransform = GetComponentTransform();

    for (const FFlightSpatialLayoutRow& TemplateRow : TemplateRows)
    {
        AppendWorldSpaceRow(TemplateRow, ComponentTransform, OutRows);
    }

    for (const FFlightSpatialLayoutRow& GeneratedRow : GeneratedRows)
    {
        OutRows.Add(GeneratedRow);
    }
}

void UFlightSpatialLayoutSourceComponent::SetGeneratedRows(const TArray<FFlightSpatialLayoutRow>& InRows)
{
    GeneratedRows = InRows;
}

void UFlightSpatialLayoutSourceComponent::ResetGeneratedRows()
{
    GeneratedRows.Reset();
}

void UFlightSpatialLayoutSourceComponent::AppendWorldSpaceRow(const FFlightSpatialLayoutRow& SourceRow, const FTransform& Transform, TArray<FFlightSpatialLayoutRow>& OutRows) const
{
    FFlightSpatialLayoutRow Row = SourceRow;

    const FVector WorldLocation = Transform.TransformPosition(SourceRow.GetLocation());
    const FRotator WorldRotation = (Transform.GetRotation() * SourceRow.GetRotation().Quaternion()).Rotator();
    const FVector WorldScale = Transform.GetScale3D() * SourceRow.GetScale();

    Row.PositionX = WorldLocation.X;
    Row.PositionY = WorldLocation.Y;
    Row.PositionZ = WorldLocation.Z;
    Row.RotationPitch = WorldRotation.Pitch;
    Row.RotationYaw = WorldRotation.Yaw;
    Row.RotationRoll = WorldRotation.Roll;
    Row.ScaleX = WorldScale.X;
    Row.ScaleY = WorldScale.Y;
    Row.ScaleZ = WorldScale.Z;

    if (Row.RowName.IsNone() && GetOwner())
    {
        Row.RowName = FName(*FString::Printf(TEXT("%s_%s_%d"),
            *GetOwner()->GetName(),
            TEXT("Layout"),
            OutRows.Num()));
    }

    OutRows.Add(MoveTemp(Row));
}
