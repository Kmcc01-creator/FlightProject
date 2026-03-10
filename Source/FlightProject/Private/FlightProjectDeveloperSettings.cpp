#include "FlightProjectDeveloperSettings.h"

UFlightProjectDeveloperSettings::UFlightProjectDeveloperSettings()
{
    CategoryName = TEXT("Project");
    SectionName = TEXT("Flight");

    DefaultMassBatchSize = 256;
    ComputeShaderDirectory = TEXT("/Shaders");
    DefaultTargetAltitude = 500.f;
    LowAltitudeThreshold = 100.f;
    HighAltitudeThreshold = 600.f;

    // Current default data ingress assets are configured in Project Settings.
    LightingConfigRow = TEXT("NightDemo");
    AutopilotConfigRow = TEXT("NightDemo");
    SpatialLayoutScenario = TEXT("NightRange");
}

FFlightDataRowSource UFlightProjectDeveloperSettings::GetLightingConfigSource() const
{
	FFlightDataRowSource Source;
	Source.DataTable = LightingConfigTable;
	Source.RowName = LightingConfigRow;
	return Source;
}

FFlightDataRowSource UFlightProjectDeveloperSettings::GetAutopilotConfigSource() const
{
	FFlightDataRowSource Source;
	Source.DataTable = AutopilotConfigTable;
	Source.RowName = AutopilotConfigRow;
	return Source;
}

FFlightDataTableSource UFlightProjectDeveloperSettings::GetSpatialLayoutSource() const
{
	FFlightDataTableSource Source;
	Source.DataTable = SpatialLayoutTable;
	return Source;
}

FFlightDataTableSource UFlightProjectDeveloperSettings::GetProceduralAnchorSource() const
{
	FFlightDataTableSource Source;
	Source.DataTable = ProceduralAnchorTable;
	return Source;
}

FFlightDataTableSource UFlightProjectDeveloperSettings::GetBehaviorCompilePolicySource() const
{
	FFlightDataTableSource Source;
	Source.DataTable = BehaviorCompilePolicyTable;
	return Source;
}
