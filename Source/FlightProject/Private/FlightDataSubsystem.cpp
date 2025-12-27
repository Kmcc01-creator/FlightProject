#include "FlightDataSubsystem.h"

#include "FlightProject.h"
#include "FlightProjectDeveloperSettings.h"

#include "Engine/DataTable.h"

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

bool UFlightDataSubsystem::LoadLightingConfig()
{
    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    UDataTable* Table = Settings->LightingConfigTable.LoadSynchronous();
    if (!Table)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: LightingConfigTable not set in project settings."));
        return false;
    }

    const FName RowName = Settings->LightingConfigRow;
    if (RowName.IsNone())
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: LightingConfigRow not specified."));
        return false;
    }

    const FFlightLightingConfigRow* Row = Table->FindRow<FFlightLightingConfigRow>(RowName, TEXT("LoadLightingConfig"));
    if (!Row)
    {
        UE_LOG(LogFlightProject, Error, TEXT("FlightDataSubsystem: Row '%s' not found in LightingConfigTable."), *RowName.ToString());
        return false;
    }

    LightingConfig = *Row;
    return true;
}

bool UFlightDataSubsystem::LoadAutopilotConfig()
{
    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    UDataTable* Table = Settings->AutopilotConfigTable.LoadSynchronous();
    if (!Table)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: AutopilotConfigTable not set in project settings."));
        return false;
    }

    const FName RowName = Settings->AutopilotConfigRow;
    if (RowName.IsNone())
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: AutopilotConfigRow not specified."));
        return false;
    }

    const FFlightAutopilotConfigRow* Row = Table->FindRow<FFlightAutopilotConfigRow>(RowName, TEXT("LoadAutopilotConfig"));
    if (!Row)
    {
        UE_LOG(LogFlightProject, Error, TEXT("FlightDataSubsystem: Row '%s' not found in AutopilotConfigTable."), *RowName.ToString());
        return false;
    }

    AutopilotConfig = *Row;
    return true;
}

bool UFlightDataSubsystem::LoadSpatialLayout()
{
    SpatialLayoutRows.Reset();

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    UDataTable* Table = Settings->SpatialLayoutTable.LoadSynchronous();
    if (!Table)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: SpatialLayoutTable not set in project settings."));
        return false;
    }

    const FName ActiveScenario = Settings->SpatialLayoutScenario;

    for (const auto& Pair : Table->GetRowMap())
    {
        const FFlightSpatialLayoutRow* Row = reinterpret_cast<const FFlightSpatialLayoutRow*>(Pair.Value);
        if (!Row)
        {
            continue;
        }

        // Filter by scenario if specified
        if (!ActiveScenario.IsNone() && Row->Scenario != ActiveScenario)
        {
            continue;
        }

        FFlightSpatialLayoutRow RowCopy = *Row;
        RowCopy.RowName = Pair.Key;
        SpatialLayoutRows.Add(MoveTemp(RowCopy));
    }

    if (SpatialLayoutRows.Num() == 0)
    {
        if (!ActiveScenario.IsNone())
        {
            UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: No spatial layout rows matched scenario '%s'."), *ActiveScenario.ToString());
        }
        return false;
    }

    // Sort by row name for deterministic ordering
    SpatialLayoutRows.Sort([](const FFlightSpatialLayoutRow& A, const FFlightSpatialLayoutRow& B)
    {
        return A.RowName.LexicalLess(B.RowName);
    });

    return true;
}

bool UFlightDataSubsystem::LoadProceduralAnchors()
{
    ProceduralAnchors.Reset();

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    UDataTable* Table = Settings->ProceduralAnchorTable.LoadSynchronous();
    if (!Table)
    {
        // Procedural anchors are optional
        return false;
    }

    for (const auto& Pair : Table->GetRowMap())
    {
        const FFlightProceduralAnchorRow* Row = reinterpret_cast<const FFlightProceduralAnchorRow*>(Pair.Value);
        if (!Row)
        {
            continue;
        }

        FFlightProceduralAnchorRow RowCopy = *Row;
        if (RowCopy.AnchorId.IsNone())
        {
            RowCopy.AnchorId = Pair.Key;
        }

        ProceduralAnchors.FindOrAdd(RowCopy.AnchorId).Add(MoveTemp(RowCopy));
    }

    return ProceduralAnchors.Num() > 0;
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
