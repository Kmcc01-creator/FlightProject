#include "Mass/FlightWaypointPathRegistry.h"
#include "FlightWaypointPath.h"
#include "Components/SplineComponent.h"

//-----------------------------------------------------------------------------
// FFlightPathData
//-----------------------------------------------------------------------------

void FFlightPathData::GetSampleIndices(float Distance, int32& OutIndex0, int32& OutIndex1, float& OutAlpha) const
{
    if (SampledLocations.Num() == 0)
    {
        OutIndex0 = 0;
        OutIndex1 = 0;
        OutAlpha = 0.f;
        return;
    }

    // Clamp or wrap distance
    if (bClosedLoop && TotalLength > 0.f)
    {
        Distance = FMath::Fmod(Distance, TotalLength);
        if (Distance < 0.f)
        {
            Distance += TotalLength;
        }
    }
    else
    {
        Distance = FMath::Clamp(Distance, 0.f, TotalLength);
    }

    // Find sample indices
    const float SampleFloat = Distance / SampleInterval;
    OutIndex0 = FMath::FloorToInt32(SampleFloat);
    OutAlpha = SampleFloat - OutIndex0;

    const int32 MaxIndex = SampledLocations.Num() - 1;
    OutIndex0 = FMath::Clamp(OutIndex0, 0, MaxIndex);
    OutIndex1 = bClosedLoop ? (OutIndex0 + 1) % SampledLocations.Num() : FMath::Min(OutIndex0 + 1, MaxIndex);
}

FVector FFlightPathData::SampleLocation(float Distance) const
{
    if (SampledLocations.Num() == 0)
    {
        return FVector::ZeroVector;
    }

    int32 Index0, Index1;
    float Alpha;
    GetSampleIndices(Distance, Index0, Index1, Alpha);

    return FMath::Lerp(SampledLocations[Index0], SampledLocations[Index1], Alpha);
}

FQuat FFlightPathData::SampleRotation(float Distance) const
{
    if (SampledRotations.Num() == 0)
    {
        return FQuat::Identity;
    }

    int32 Index0, Index1;
    float Alpha;
    GetSampleIndices(Distance, Index0, Index1, Alpha);

    return FQuat::Slerp(SampledRotations[Index0], SampledRotations[Index1], Alpha);
}

FVector FFlightPathData::SampleTangent(float Distance) const
{
    if (SampledTangents.Num() == 0)
    {
        return FVector::ForwardVector;
    }

    int32 Index0, Index1;
    float Alpha;
    GetSampleIndices(Distance, Index0, Index1, Alpha);

    // Lerp tangents and normalize
    FVector Tangent = FMath::Lerp(SampledTangents[Index0], SampledTangents[Index1], Alpha);
    return Tangent.GetSafeNormal();
}

//-----------------------------------------------------------------------------
// UFlightWaypointPathRegistry
//-----------------------------------------------------------------------------

void UFlightWaypointPathRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("FlightWaypointPathRegistry initialized"));
}

void UFlightWaypointPathRegistry::Deinitialize()
{
    PathRegistry.Empty();
    Super::Deinitialize();
}

FGuid UFlightWaypointPathRegistry::RegisterPath(AFlightWaypointPath* Path, float SampleInterval)
{
    if (!Path)
    {
        UE_LOG(LogTemp, Warning, TEXT("FlightWaypointPathRegistry: Attempted to register null path"));
        return FGuid();
    }

    // Generate unique ID for this path
    FGuid PathId = FGuid::NewGuid();

    // Build the LUT
    FFlightPathData& PathData = PathRegistry.Add(PathId);
    BuildPathLUT(PathData, Path, SampleInterval);

    UE_LOG(LogTemp, Verbose, TEXT("FlightWaypointPathRegistry: Registered path %s with %d samples (length: %.1f)"),
        *PathId.ToString(), PathData.SampledLocations.Num(), PathData.TotalLength);

    return PathId;
}

void UFlightWaypointPathRegistry::UnregisterPath(const FGuid& PathId)
{
    if (PathRegistry.Remove(PathId) > 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("FlightWaypointPathRegistry: Unregistered path %s"), *PathId.ToString());
    }
}

const FFlightPathData* UFlightWaypointPathRegistry::FindPath(const FGuid& PathId) const
{
    return PathRegistry.Find(PathId);
}

bool UFlightWaypointPathRegistry::IsPathRegistered(const FGuid& PathId) const
{
    return PathRegistry.Contains(PathId);
}

void UFlightWaypointPathRegistry::RefreshPath(const FGuid& PathId, AFlightWaypointPath* Path)
{
    if (FFlightPathData* PathData = PathRegistry.Find(PathId))
    {
        const float OldInterval = PathData->SampleInterval;
        BuildPathLUT(*PathData, Path, OldInterval);
        UE_LOG(LogTemp, Verbose, TEXT("FlightWaypointPathRegistry: Refreshed path %s"), *PathId.ToString());
    }
}

void UFlightWaypointPathRegistry::BuildPathLUT(FFlightPathData& OutData, AFlightWaypointPath* Path, float SampleInterval)
{
    OutData.SampledLocations.Reset();
    OutData.SampledRotations.Reset();
    OutData.SampledTangents.Reset();
    OutData.SampleInterval = FMath::Max(SampleInterval, 1.f);

    USplineComponent* Spline = Path ? Path->GetSplineComponent() : nullptr;
    if (!Spline)
    {
        OutData.TotalLength = 0.f;
        OutData.bClosedLoop = false;
        return;
    }

    OutData.TotalLength = Spline->GetSplineLength();
    OutData.bClosedLoop = Spline->IsClosedLoop();

    if (OutData.TotalLength <= 0.f)
    {
        return;
    }

    // Calculate number of samples needed
    const int32 NumSamples = FMath::CeilToInt32(OutData.TotalLength / OutData.SampleInterval) + 1;
    OutData.SampledLocations.Reserve(NumSamples);
    OutData.SampledRotations.Reserve(NumSamples);
    OutData.SampledTangents.Reserve(NumSamples);

    // Sample the spline
    for (int32 i = 0; i < NumSamples; ++i)
    {
        const float Distance = FMath::Min(i * OutData.SampleInterval, OutData.TotalLength);

        const FVector Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        const FQuat Rotation = Spline->GetQuaternionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

        OutData.SampledLocations.Add(Location);
        OutData.SampledRotations.Add(Rotation);
        OutData.SampledTangents.Add(Tangent);
    }
}
