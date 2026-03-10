#include "FlightDataSubsystem.h"

#include "FlightProject.h"
#include "FlightProjectDeveloperSettings.h"
#include "FlightDataResolver.h"

using namespace Flight::Functional;
using namespace Flight::Data;

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
    bBehaviorCompilePolicyLoaded = LoadBehaviorCompilePolicies();

    UE_LOG(LogFlightProject, Log, TEXT("FlightDataSubsystem: Initialized - Lighting=%d, Autopilot=%d, SpatialLayout=%d (%d rows), ProceduralAnchors=%d, BehaviorCompilePolicies=%d (%d rows)"),
        bLightingLoaded, bAutopilotLoaded, bSpatialLayoutLoaded, SpatialLayoutRows.Num(), bProceduralAnchorsLoaded, bBehaviorCompilePolicyLoaded, BehaviorCompilePolicies.Num());
}

bool UFlightDataSubsystem::LoadLightingConfig()
{
    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    return ResolveRow<FFlightLightingConfigRow>(
        Settings->GetLightingConfigSource(),
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

    return ResolveRow<FFlightAutopilotConfigRow>(
        Settings->GetAutopilotConfigSource(),
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

bool UFlightDataSubsystem::LoadSpatialLayout()
{
    SpatialLayoutRows.Reset();

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    const FName ActiveScenario = Settings->SpatialLayoutScenario;

    return ResolveRows<FFlightSpatialLayoutRow>(
        Settings->GetSpatialLayoutSource(),
        TEXT("SpatialLayoutTable"),
        [ActiveScenario](const FFlightSpatialLayoutRow& Row)
        {
            return ActiveScenario.IsNone() || Row.Scenario == ActiveScenario;
        },
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
    return ResolveRows<FFlightProceduralAnchorRow>(
        Settings->GetProceduralAnchorSource(),
        TEXT("ProceduralAnchorTable"),
        [](const FFlightProceduralAnchorRow&)
        {
            return true;
        },
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

bool UFlightDataSubsystem::LoadBehaviorCompilePolicies()
{
    BehaviorCompilePolicies.Reset();

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        return false;
    }

    const FFlightDataTableSource Source = Settings->GetBehaviorCompilePolicySource();
    if (!Source.IsConfigured())
    {
        return true;
    }

    return ResolveRows<FFlightBehaviorCompilePolicyRow>(
        Source,
        TEXT("BehaviorCompilePolicyTable"),
        [](const FFlightBehaviorCompilePolicyRow&)
        {
            return true;
        },
        [](FFlightBehaviorCompilePolicyRow& Row, FName RowName)
        {
            Row.RowName = RowName;
        })
    .Map([](TArray<FFlightBehaviorCompilePolicyRow> Rows)
    {
        Rows.Sort([](const FFlightBehaviorCompilePolicyRow& Left, const FFlightBehaviorCompilePolicyRow& Right)
        {
            const int32 LeftSpecificity = Left.GetSpecificityScore();
            const int32 RightSpecificity = Right.GetSpecificityScore();
            if (LeftSpecificity != RightSpecificity)
            {
                return LeftSpecificity > RightSpecificity;
            }
            if (Left.Priority != Right.Priority)
            {
                return Left.Priority > Right.Priority;
            }
            return Left.RowName.LexicalLess(Right.RowName);
        });
        return Rows;
    })
    .Match(
        [this](TArray<FFlightBehaviorCompilePolicyRow> Rows)
        {
            BehaviorCompilePolicies = MoveTemp(Rows);
            return true;
        },
        [](const FString& Error)
        {
            UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: %s"), *Error);
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

const FFlightBehaviorCompilePolicyRow* UFlightDataSubsystem::FindBehaviorCompilePolicy(
    const uint32 BehaviorId,
    const FName CohortName,
    const FName ProfileName) const
{
    if (!bBehaviorCompilePolicyLoaded || BehaviorCompilePolicies.IsEmpty())
    {
        return nullptr;
    }

    return SelectBestBehaviorCompilePolicy(BehaviorCompilePolicies, BehaviorId, CohortName, ProfileName);
}

void UFlightDataSubsystem::ReloadAllConfigs()
{
    UE_LOG(LogFlightProject, Log, TEXT("FlightDataSubsystem: Reloading all configurations..."));

    bLightingLoaded = LoadLightingConfig();
    bAutopilotLoaded = LoadAutopilotConfig();
    bSpatialLayoutLoaded = LoadSpatialLayout();
    bProceduralAnchorsLoaded = LoadProceduralAnchors();
    bBehaviorCompilePolicyLoaded = LoadBehaviorCompilePolicies();

    UE_LOG(LogFlightProject, Log, TEXT("FlightDataSubsystem: Reload complete. Lighting=%d, Autopilot=%d, SpatialLayout=%d (%d rows), ProceduralAnchors=%d, BehaviorCompilePolicies=%d (%d rows)"),
        bLightingLoaded, bAutopilotLoaded, bSpatialLayoutLoaded, SpatialLayoutRows.Num(), bProceduralAnchorsLoaded, bBehaviorCompilePolicyLoaded, BehaviorCompilePolicies.Num());
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
    else if (ConfigName.Equals(TEXT("BehaviorCompilePolicies"), ESearchCase::IgnoreCase))
    {
        bBehaviorCompilePolicyLoaded = LoadBehaviorCompilePolicies();
        return bBehaviorCompilePolicyLoaded;
    }

    UE_LOG(LogFlightProject, Warning, TEXT("FlightDataSubsystem: Unknown config name '%s'. Valid: Lighting, Autopilot, SpatialLayout, ProceduralAnchors, BehaviorCompilePolicies"), *ConfigName);
    return false;
}

bool UFlightDataSubsystem::IsFullyLoaded() const
{
    return bLightingLoaded && bAutopilotLoaded && bSpatialLayoutLoaded && bProceduralAnchorsLoaded && bBehaviorCompilePolicyLoaded;
}
