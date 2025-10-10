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
    LightingConfigPath = TEXT("Data/FlightLightingConfig.csv");
    LightingConfigRow = TEXT("NightDemo");
    AutopilotConfigPath = TEXT("Data/FlightAutopilotConfig.csv");
    AutopilotConfigRow = TEXT("NightDemo");
    SpatialLayoutConfigPath = TEXT("Data/FlightSpatialLayout.csv");
    SpatialLayoutScenario = TEXT("NightRange");
}
