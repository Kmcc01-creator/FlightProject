#include "FlightDataSubsystem.h"

#include "FlightProject.h"
#include "FlightProjectDeveloperSettings.h"

#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void UFlightDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    bLightingLoaded = LoadLightingConfig();
    if (!bLightingLoaded)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: Falling back to hard-coded lighting defaults."));
    }

    bAutopilotLoaded = LoadAutopilotConfig();
    if (!bAutopilotLoaded)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: Falling back to hard-coded autopilot defaults."));
    }

    bSpatialLayoutLoaded = LoadSpatialLayout();
    if (!bSpatialLayoutLoaded)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: Level layout CSV failed to load; test range actors will be absent."));
    }
}

bool UFlightDataSubsystem::LoadLightingConfig()
{
    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    return LoadTableRow(
        Settings->LightingConfigPath,
        Settings->LightingConfigRow,
        FFlightLightingConfigRow::StaticStruct(),
        [this](const uint8* RowData)
        {
            LightingConfig = *reinterpret_cast<const FFlightLightingConfigRow*>(RowData);
        });
}

bool UFlightDataSubsystem::LoadAutopilotConfig()
{
    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    return LoadTableRow(
        Settings->AutopilotConfigPath,
        Settings->AutopilotConfigRow,
        FFlightAutopilotConfigRow::StaticStruct(),
        [this](const uint8* RowData)
        {
            AutopilotConfig = *reinterpret_cast<const FFlightAutopilotConfigRow*>(RowData);
        });
}

bool UFlightDataSubsystem::LoadSpatialLayout()
{
    SpatialLayoutRows.Reset();

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    if (Settings->SpatialLayoutConfigPath.IsEmpty())
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: Spatial layout path not configured."));
        return false;
    }

    const FString ContentDir = FPaths::ProjectContentDir();
    const FString AbsolutePath = FPaths::Combine(ContentDir, Settings->SpatialLayoutConfigPath);

    FString CSVString;
    if (!FFileHelper::LoadFileToString(CSVString, *AbsolutePath))
    {
        UE_LOG(LogFlightProject, Error, TEXT("FlightDataSubsystem: Failed to load spatial layout CSV '%s'."), *AbsolutePath);
        return false;
    }

    UDataTable* TempTable = NewObject<UDataTable>(this);
    TempTable->RowStruct = FFlightSpatialLayoutRow::StaticStruct();

    const TArray<FString> ImportErrors = TempTable->CreateTableFromCSVString(CSVString);
    if (ImportErrors.Num() > 0)
    {
        for (const FString& Error : ImportErrors)
        {
            UE_LOG(LogFlightProject, Error, TEXT("FlightDataSubsystem: Spatial layout import error for '%s': %s"), *AbsolutePath, *Error);
        }
        return false;
    }

    const FName ActiveScenario = Settings->SpatialLayoutScenario;
    int32 RowsAdded = 0;

    for (const auto& Pair : TempTable->GetRowMap())
    {
        const FFlightSpatialLayoutRow* Row = reinterpret_cast<const FFlightSpatialLayoutRow*>(Pair.Value);
        if (!Row)
        {
            continue;
        }

        if (!ActiveScenario.IsNone() && Row->Scenario != ActiveScenario)
        {
            continue;
        }

        FFlightSpatialLayoutRow RowCopy = *Row;
        RowCopy.RowName = Pair.Key;
        SpatialLayoutRows.Add(MoveTemp(RowCopy));
        ++RowsAdded;
    }

    if (RowsAdded == 0)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: No spatial layout rows matched scenario '%s'."), *ActiveScenario.ToString());
        return false;
    }

    SpatialLayoutRows.Sort([](const FFlightSpatialLayoutRow& A, const FFlightSpatialLayoutRow& B)
    {
        return A.RowName.LexicalLess(B.RowName);
    });

    return true;
}

bool UFlightDataSubsystem::LoadTableRow(const FString& RelativePath, const FName& RowName, UScriptStruct* RowStruct, TFunctionRef<void(const uint8*)> OnRowLoaded)
{
    if (RelativePath.IsEmpty() || RowName.IsNone() || RowStruct == nullptr)
    {
        return false;
    }

    const FString ContentDir = FPaths::ProjectContentDir();
    const FString AbsolutePath = FPaths::Combine(ContentDir, RelativePath);

    FString CSVString;
    if (!FFileHelper::LoadFileToString(CSVString, *AbsolutePath))
    {
        UE_LOG(LogFlightProject, Error, TEXT("FlightDataSubsystem: Failed to load CSV file '%s'."), *AbsolutePath);
        return false;
    }

    UDataTable* TempTable = NewObject<UDataTable>(this);
    TempTable->RowStruct = RowStruct;

    const TArray<FString> ImportErrors = TempTable->CreateTableFromCSVString(CSVString);
    if (ImportErrors.Num() > 0)
    {
        for (const FString& Error : ImportErrors)
        {
            UE_LOG(LogFlightProject, Error, TEXT("FlightDataSubsystem: CSV import error for '%s': %s"), *AbsolutePath, *Error);
        }
        return false;
    }

    if (const uint8* Row = TempTable->FindRowUnchecked(RowName))
    {
        OnRowLoaded(Row);
        return true;
    }

    UE_LOG(LogFlightProject, Error, TEXT("FlightDataSubsystem: Row '%s' not found in '%s'."), *RowName.ToString(), *AbsolutePath);
    return false;
}
