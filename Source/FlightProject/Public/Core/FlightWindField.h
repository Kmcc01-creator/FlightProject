// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightSpatialField.h"

namespace Flight::Spatial
{

/**
 * FProceduralWindField
 * 
 * A procedural wind field using sine-wave patterns.
 * Demonstrates the synchronous sampling path.
 */
class FProceduralWindField : public IFlightSpatialField
{
public:
	FProceduralWindField(FVector InDirection, float InStrength, float InFrequency)
		: Direction(InDirection.GetSafeNormal())
		, Strength(InStrength)
		, Frequency(InFrequency)
	{}

	virtual FName GetFieldName() const override { return TEXT("ProceduralWind"); }
	virtual ESpatialFieldType GetFieldType() const override { return ESpatialFieldType::Force; }

	virtual FVector SampleSync(const FVector& Position) const override
	{
		// Simple time-varying sine wind
		float Time = static_cast<float>(FPlatformTime::Seconds());
		float Phase = (Position.X + Position.Y + Position.Z) * Frequency;
		float Variation = FMath::Sin(Time + Phase) * 0.5f + 0.5f;
		
		return Direction * (Strength * Variation);
	}

	virtual void SampleAsync(const FVector& Position, TFunction<void(const FVector&)> Callback) override
	{
		// Synchronous fields just invoke the callback immediately
		Callback(SampleSync(Position));
	}

private:
	FVector Direction;
	float Strength;
	float Frequency;
};

} // namespace Flight::Spatial
