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

    // Data Table references are set in Project Settings
    LightingConfigRow = TEXT("NightDemo");
    AutopilotConfigRow = TEXT("NightDemo");
    SpatialLayoutScenario = TEXT("NightRange");
}
