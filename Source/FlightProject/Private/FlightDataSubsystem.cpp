#include "FlightDataSubsystem.h"

#include "FlightProject.h"
#include "FlightProjectDeveloperSettings.h"

#include "Engine/DataTable.h"

using namespace Flight::Functional;

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
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: Spatial layout not loaded; test range actors will be absent."));
    }

    bProceduralAnchorsLoaded = LoadProceduralAnchors();

    UE_LOG(LogFlightProject, Log, TEXT("FlightDataSubsystem: Initialized - Lighting=%d, Autopilot=%d, SpatialLayout=%d (%d rows), ProceduralAnchors=%d"),
        bLightingLoaded, bAutopilotLoaded, bSpatialLayoutLoaded, SpatialLayoutRows.Num(), bProceduralAnchorsLoaded);
}

// Helper: Load a single-row config from a DataTable using TResult
template<typename TRow>
static TResult<TRow, FString> LoadConfigRow(
    const TSoftObjectPtr<UDataTable>& TableRef,
    FName RowName,
    const TCHAR* TableName,
    const TCHAR* Context)
{
    // Chain: Settings -> Table -> RowName -> Row
    return TResult<UDataTable*, FString>::Ok(TableRef.LoadSynchronous())
        .AndThen([TableName](UDataTable* Table) -> TResult<UDataTable*, FString>
        {
            if (!Table)
            {
                return Err<UDataTable*>(FString::Printf(TEXT("%s not set in project settings"), TableName));
            }
            return Ok(Table);
        })
        .AndThen([RowName, TableName, Context](UDataTable* Table) -> TResult<TRow, FString>
        {
            if (RowName.IsNone())
            {
                return Err<TRow>(FString::Printf(TEXT("%s row name not specified"), TableName));
            }

            const TRow* Row = Table->FindRow<TRow>(RowName, Context);
            if (!Row)
            {
                return Err<TRow>(FString::Printf(TEXT("Row '%s' not found in %s"), *RowName.ToString(), TableName));
            }

            return Ok(*Row);
        });
}

bool UFlightDataSubsystem::LoadLightingConfig()
{
    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    return LoadConfigRow<FFlightLightingConfigRow>(
        Settings->LightingConfigTable,
        Settings->LightingConfigRow,
        TEXT("LightingConfigTable"),
        TEXT("LoadLightingConfig"))
    .Match(
        [this](FFlightLightingConfigRow Row)
        {
            LightingConfig = MoveTemp(Row);
            return true;
        },
        [](const FString& Error)
        {
            UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: %s"), *Error);
            return false;
        });
}

bool UFlightDataSubsystem::LoadAutopilotConfig()
{
    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    return LoadConfigRow<FFlightAutopilotConfigRow>(
        Settings->AutopilotConfigTable,
        Settings->AutopilotConfigRow,
        TEXT("AutopilotConfigTable"),
        TEXT("LoadAutopilotConfig"))
    .Match(
        [this](FFlightAutopilotConfigRow Row)
        {
            AutopilotConfig = MoveTemp(Row);
            return true;
        },
        [](const FString& Error)
        {
            UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: %s"), *Error);
            return false;
        });
}

// Helper: Load multi-row table with filtering
template<typename TRow>
static TResult<TArray<TRow>, FString> LoadTableRows(
    const TSoftObjectPtr<UDataTable>& TableRef,
    const TCHAR* TableName,
    TFunction<bool(const TRow&)> RowFilter,
    TFunction<void(TRow&, FName)> OnRow)
{
    UDataTable* Table = TableRef.LoadSynchronous();
    if (!Table)
    {
        return Err<TArray<TRow>>(FString::Printf(TEXT("%s not set in project settings"), TableName));
    }

    TArray<TRow> Result;
    for (const auto& Pair : Table->GetRowMap())
    {
        const TRow* Row = reinterpret_cast<const TRow*>(Pair.Value);
        if (!Row)
        {
            continue;
        }

        if (RowFilter && !RowFilter(*Row))
        {
            continue;
        }

        TRow RowCopy = *Row;
        if (OnRow)
        {
            OnRow(RowCopy, Pair.Key);
        }
        Result.Add(MoveTemp(RowCopy));
    }

    if (Result.Num() == 0)
    {
        return Err<TArray<TRow>>(FString::Printf(TEXT("No rows matched filter in %s"), TableName));
    }

    return Ok(MoveTemp(Result));
}

bool UFlightDataSubsystem::LoadSpatialLayout()
{
    SpatialLayoutRows.Reset();

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    const FName ActiveScenario = Settings->SpatialLayoutScenario;

    return LoadTableRows<FFlightSpatialLayoutRow>(
        Settings->SpatialLayoutTable,
        TEXT("SpatialLayoutTable"),
        // Filter: match scenario (or all if no scenario specified)
        [ActiveScenario](const FFlightSpatialLayoutRow& Row)
        {
            return ActiveScenario.IsNone() || Row.Scenario == ActiveScenario;
        },
        // OnRow: capture the row name
        [](FFlightSpatialLayoutRow& Row, FName RowName)
        {
            Row.RowName = RowName;
        })
    .Map([](TArray<FFlightSpatialLayoutRow> Rows)
    {
        // Sort by row name for deterministic ordering
        Rows.Sort([](const FFlightSpatialLayoutRow& A, const FFlightSpatialLayoutRow& B)
        {
            return A.RowName.LexicalLess(B.RowName);
        });
        return Rows;
    })
    .Match(
        [this](TArray<FFlightSpatialLayoutRow> Rows)
        {
            SpatialLayoutRows = MoveTemp(Rows);
            return true;
        },
        [Settings](const FString& Error)
        {
            if (!Settings->SpatialLayoutScenario.IsNone())
            {
                UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: %s for scenario '%s'"),
                    *Error, *Settings->SpatialLayoutScenario.ToString());
            }
            return false;
        });
}

bool UFlightDataSubsystem::LoadProceduralAnchors()
{
    ProceduralAnchors.Reset();

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    // Load all rows, then group by AnchorId
    return LoadTableRows<FFlightProceduralAnchorRow>(
        Settings->ProceduralAnchorTable,
        TEXT("ProceduralAnchorTable"),
        nullptr,  // No filter - load all
        [](FFlightProceduralAnchorRow& Row, FName RowName)
        {
            if (Row.AnchorId.IsNone())
            {
                Row.AnchorId = RowName;
            }
        })
    .Map([](TArray<FFlightProceduralAnchorRow> Rows)
    {
        // Group by AnchorId
        TMap<FName, TArray<FFlightProceduralAnchorRow>> Grouped;
        for (FFlightProceduralAnchorRow& Row : Rows)
        {
            Grouped.FindOrAdd(Row.AnchorId).Add(MoveTemp(Row));
        }
        return Grouped;
    })
    .Match(
        [this](TMap<FName, TArray<FFlightProceduralAnchorRow>> Grouped)
        {
            ProceduralAnchors = MoveTemp(Grouped);
            return true;
        },
        [](const FString&)
        {
            // Procedural anchors are optional - silently return false
            return false;
        });
}

const FFlightProceduralAnchorRow* UFlightDataSubsystem::FindProceduralAnchorConfig(FName AnchorId, EFlightProceduralAnchorType AnchorType) const
{
    if (!bProceduralAnchorsLoaded)
    {
        return nullptr;
    }

    const FName LookupKey = AnchorId.IsNone() ? NAME_None : AnchorId;

    if (!LookupKey.IsNone())
    {
        if (const TArray<FFlightProceduralAnchorRow>* Rows = ProceduralAnchors.Find(LookupKey))
        {
            for (const FFlightProceduralAnchorRow& Row : *Rows)
            {
                if (Row.MatchesAnchor(LookupKey, AnchorType))
                {
                    return &Row;
                }
            }
        }
    }

    if (LookupKey.IsNone())
    {
        for (const auto& Pair : ProceduralAnchors)
        {
            for (const FFlightProceduralAnchorRow& Row : Pair.Value)
            {
                if (Row.AnchorType == AnchorType)
                {
                    return &Row;
                }
            }
        }
    }

    return nullptr;
}

void UFlightDataSubsystem::ReloadAllConfigs()
{
    UE_LOG(LogFlightProject, Log, TEXT("FlightDataSubsystem: Reloading all configurations..."));

    bLightingLoaded = LoadLightingConfig();
    bAutopilotLoaded = LoadAutopilotConfig();
    bSpatialLayoutLoaded = LoadSpatialLayout();
    bProceduralAnchorsLoaded = LoadProceduralAnchors();

    UE_LOG(LogFlightProject, Log, TEXT("FlightDataSubsystem: Reload complete. Lighting=%d, Autopilot=%d, SpatialLayout=%d (%d rows), ProceduralAnchors=%d"),
        bLightingLoaded, bAutopilotLoaded, bSpatialLayoutLoaded, SpatialLayoutRows.Num(), bProceduralAnchorsLoaded);
}

bool UFlightDataSubsystem::ReloadConfig(const FString& ConfigName)
{
    if (ConfigName.Equals(TEXT("Lighting"), ESearchCase::IgnoreCase))
    {
        bLightingLoaded = LoadLightingConfig();
        return bLightingLoaded;
    }
    else if (ConfigName.Equals(TEXT("Autopilot"), ESearchCase::IgnoreCase))
    {
        bAutopilotLoaded = LoadAutopilotConfig();
        return bAutopilotLoaded;
    }
    else if (ConfigName.Equals(TEXT("SpatialLayout"), ESearchCase::IgnoreCase))
    {
        bSpatialLayoutLoaded = LoadSpatialLayout();
        return bSpatialLayoutLoaded;
    }
    else if (ConfigName.Equals(TEXT("ProceduralAnchors"), ESearchCase::IgnoreCase))
    {
        bProceduralAnchorsLoaded = LoadProceduralAnchors();
        return bProceduralAnchorsLoaded;
    }

    UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: Unknown config name '%s'. Valid: Lighting, Autopilot, SpatialLayout, ProceduralAnchors"), *ConfigName);
    return false;
}

bool UFlightDataSubsystem::IsFullyLoaded() const
{
    return bLightingLoaded && bAutopilotLoaded && bSpatialLayoutLoaded && bProceduralAnchorsLoaded;
}
