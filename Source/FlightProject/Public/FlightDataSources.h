#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "FlightDataSources.generated.h"

UENUM(BlueprintType)
enum class EFlightDataSourceKind : uint8
{
	DataTable,
};

USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightDataRowSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Data")
	EFlightDataSourceKind SourceKind = EFlightDataSourceKind::DataTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Data")
	TSoftObjectPtr<UDataTable> DataTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Data")
	FName RowName = NAME_None;

	bool IsConfigured() const
	{
		return SourceKind == EFlightDataSourceKind::DataTable && !DataTable.IsNull();
	}

	void AppendConfiguredPaths(TArray<FString>& OutPaths) const
	{
		if (SourceKind == EFlightDataSourceKind::DataTable && !DataTable.IsNull())
		{
			OutPaths.Add(DataTable.ToString());
		}
	}
};

USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightDataTableSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Data")
	EFlightDataSourceKind SourceKind = EFlightDataSourceKind::DataTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Data")
	TSoftObjectPtr<UDataTable> DataTable;

	bool IsConfigured() const
	{
		return SourceKind == EFlightDataSourceKind::DataTable && !DataTable.IsNull();
	}

	void AppendConfiguredPaths(TArray<FString>& OutPaths) const
	{
		if (SourceKind == EFlightDataSourceKind::DataTable && !DataTable.IsNull())
		{
			OutPaths.Add(DataTable.ToString());
		}
	}
};
