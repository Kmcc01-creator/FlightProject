// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "FlightSwarmShaders.h"

#if WITH_FLIGHT_COMPUTE_SHADERS

IMPLEMENT_GLOBAL_SHADER(FFlightSwarmClearGridCS, "/FlightProject/Private/FlightSwarmSpatial.usf", "ClearGridMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmBuildGridCS, "/FlightProject/Private/FlightSwarmSpatial.usf", "BuildGridMain", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightRadixCountCS, "/FlightProject/Private/FlightRadixSort.usf", "RadixCountMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightRadixScanCS, "/FlightProject/Private/FlightRadixSort.usf", "RadixScanMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightRadixScatterCS, "/FlightProject/Private/FlightRadixSort.usf", "RadixScatterMain", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightSwarmDensityCS, "/FlightProject/Private/FlightSwarmDensity.usf", "DensityMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmForceCS, "/FlightProject/Private/FlightSwarmForce.usf", "ForceMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmIntegrationCS, "/FlightProject/Private/FlightSwarmIntegration.usf", "IntegrationMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmPredictiveCS, "/FlightProject/Private/FlightSwarmPredictive.usf", "PredictiveRolloutMain", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightSwarmSplattingCS, "/FlightProject/Private/FlightSwarmSplatting.usf", "SplattingMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightSwarmSplatResolveCS, "/FlightProject/Private/FlightSwarmSplatting.usf", "SplatResolveMain", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightLightInjectionCS, "/FlightProject/Private/FlightLightLattice.usf", "LightInjectionMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightLightPropagationCS, "/FlightProject/Private/FlightLightLattice.usf", "LightPropagationMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightLightConvertCS, "/FlightProject/Private/FlightLightLattice.usf", "LatticeConvertMain", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FFlightCloudInjectionCS, "/FlightProject/Private/FlightCloudSim.usf", "CloudInjectionMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFlightCloudSimCS, "/FlightProject/Private/FlightCloudSim.usf", "CloudSimMain", SF_Compute);

#endif
