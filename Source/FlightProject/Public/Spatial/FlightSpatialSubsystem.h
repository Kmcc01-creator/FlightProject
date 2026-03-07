// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/FlightSpatialField.h"
#include "MassProcessor.h"
#include "FlightSpatialSubsystem.generated.h"

/**
 * FMassSpatialForceProcessor
 * 
 * Aggregates forces from the spatial subsystem and applies them to entities.
 */
UCLASS()
class FLIGHTPROJECT_API UMassSpatialForceProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassSpatialForceProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

/**
 * UFlightSpatialSubsystem
...
 * Registry and management for spatial fields.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightSpatialSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Register a new spatial field provider */
	void RegisterField(TSharedPtr<Flight::Spatial::IFlightSpatialField> Field)
	{
		if (Field.IsValid())
		{
			Fields.Add(Field->GetFieldName(), Field);
		}
	}

	/** Unregister a field by name */
	void UnregisterField(FName FieldName)
	{
		Fields.Remove(FieldName);
	}

	/** Get all registered fields */
	const TMap<FName, TSharedPtr<Flight::Spatial::IFlightSpatialField>>& GetFields() const { return Fields; }

	/** 
	 * Aggregate forces at a position based on a layer mask.
	 * Synchronous path for high-performance loops.
	 */
	FVector GetAggregateForce(const FVector& Position, uint32 LayerMask = 0xFFFFFFFF) const
	{
		FVector TotalForce = FVector::ZeroVector;
		for (auto& [Name, Field] : Fields)
		{
			if (Field->GetFieldType() == Flight::Spatial::ESpatialFieldType::Force)
			{
				// In a real implementation, we'd check against LayerMask here
				TotalForce += Field->SampleSync(Position);
			}
		}
		return TotalForce;
	}

private:
	TMap<FName, TSharedPtr<Flight::Spatial::IFlightSpatialField>> Fields;
};
