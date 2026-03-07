// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "FlightSwarmShaders.h"

#if WITH_FLIGHT_COMPUTE_SHADERS

// Pass 0: Spatial
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmClearGridCS, 
	"/FlightProject/Private/FlightSwarmSpatial.usf", 
	"ClearGridMain", 
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightSwarmBuildGridCS, 
	"/FlightProject/Private/FlightSwarmSpatial.usf", 
	"BuildGridMain", 
	SF_Compute);

// Pass 1: Density
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmDensityCS, 
	"/FlightProject/Private/FlightSwarmDensity.usf", 
	"DensityMain", 
	SF_Compute);

// Pass 2: Force
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmForceCS, 
	"/FlightProject/Private/FlightSwarmForce.usf", 
	"ForceMain", 
	SF_Compute);

// Pass 3: Integration
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmIntegrationCS, 
	"/FlightProject/Private/FlightSwarmIntegration.usf", 
	"IntegrationMain", 
	SF_Compute);

// Pass 5: Predictive
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmPredictiveCS, 
	"/FlightProject/Private/FlightSwarmPredictive.usf", 
	"PredictiveRolloutMain", 
	SF_Compute);

#endif // WITH_FLIGHT_COMPUTE_SHADERS
