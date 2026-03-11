#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "UObject/Interface.h"

#include "FlightSchemaProviderAdapter.generated.h"

namespace Flight::Adapters
{

struct FLIGHTPROJECT_API FFlightSchemaProviderDescriptor
{
	const void* RuntimeTypeKey = nullptr;
	FName TypeName = NAME_None;
	Flight::Reflection::EVexCapability ExpectedCapability = Flight::Reflection::EVexCapability::NotVexCapable;
	TArray<FName> ContractKeys;
	bool bSupportsBatchResolution = false;

	bool IsValid() const
	{
		return RuntimeTypeKey != nullptr || TypeName != NAME_None;
	}
};

} // namespace Flight::Adapters

UINTERFACE()
class FLIGHTPROJECT_API UFlightSchemaProviderAdapter : public UInterface
{
	GENERATED_BODY()
};

class FLIGHTPROJECT_API IFlightSchemaProviderAdapter
{
	GENERATED_BODY()

public:
	virtual void GetSchemaProviderDescriptors(TArray<Flight::Adapters::FFlightSchemaProviderDescriptor>& OutDescriptors) const = 0;
};
