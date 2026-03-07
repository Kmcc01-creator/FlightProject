// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Spatial/FlightGpuPerceptionField.h"
#include "IoUring/FlightGpuPerceptionSubsystem.h"
#include "Engine/World.h"

namespace Flight::Spatial
{

FGpuPerceptionField::FGpuPerceptionField(UWorld* InWorld)
{
	if (InWorld)
	{
		PerceptionSubsystem = InWorld->GetSubsystem<UFlightGpuPerceptionSubsystem>();
	}
}

void FGpuPerceptionField::SampleAsync(const FVector& Position, TFunction<void(const FVector&)> Callback)
{
	if (!PerceptionSubsystem.IsValid())
	{
		Callback(FVector::ZeroVector);
		return;
	}

	// Prepare a single-entity request
	FFlightPerceptionRequest Request;
	Request.EntityPositions.Add(FVector4f((FVector3f)Position, 1000.0f)); // Default 1000cm radius

	// Submit to subsystem
	PerceptionSubsystem->SubmitPerceptionRequest(Request,
		[Callback](const FFlightPerceptionResult& Result)
		{
			if (Result.bSuccess && Result.ObstacleCounts.Num() > 0)
			{
				// Return a vector where X = obstacle count, Y = 1 if blocked, Z = 0
				float Count = static_cast<float>(Result.ObstacleCounts[0]);
				Callback(FVector(Count, Count > 0 ? 1.0f : 0.0f, 0.0f));
			}
			else
			{
				Callback(FVector::ZeroVector);
			}
		});
}

} // namespace Flight::Spatial
