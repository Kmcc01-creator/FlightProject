// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Core/FlightReactive.h"
#include "ShaderParameterMacros.h"
#include "UniformBuffer.h"

namespace Flight::Swarm
{

/**
 * FSwarmSimulationParams
 * 
 * Reflected and reactive parameters for the SPH simulation.
 * These can be live-tweaked in the editor or UI.
 */
struct FSwarmSimulationParams
{
	float SmoothingRadius = 500.0f;
	float GasConstant = 2000.0f;
	float RestDensity = 1.0f;
	float Viscosity = 0.1f;
	float AvoidanceStrength = 1000.0f;
	float MaxAcceleration = 5000.0f;
	float ShieldDecayRate = 0.05f;

	FLIGHT_REFLECT_BODY(FSwarmSimulationParams);
};

} // namespace Flight::Swarm

// Register reflection traits
FLIGHT_REFLECT_FIELDS_ATTR(Flight::Swarm::FSwarmSimulationParams,
	FLIGHT_FIELD_ATTR(float, SmoothingRadius, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::ClampedValue<10, 2000>),
	FLIGHT_FIELD_ATTR(float, GasConstant, ::Flight::Reflection::Attr::EditAnywhere),
	FLIGHT_FIELD_ATTR(float, RestDensity, ::Flight::Reflection::Attr::EditAnywhere),
	FLIGHT_FIELD_ATTR(float, Viscosity, ::Flight::Reflection::Attr::EditAnywhere),
	FLIGHT_FIELD_ATTR(float, AvoidanceStrength, ::Flight::Reflection::Attr::EditAnywhere),
	FLIGHT_FIELD_ATTR(float, MaxAcceleration, ::Flight::Reflection::Attr::EditAnywhere),
	FLIGHT_FIELD_ATTR(float, ShieldDecayRate, ::Flight::Reflection::Attr::EditAnywhere)
)

/**
 * FSwarmGlobalCommand
 * 
 * The "Command UBO" packed for the GPU.
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSwarmGlobalCommand, FLIGHTPROJECT_API)
	SHADER_PARAMETER(FVector4f, PlayerPosRadius)
	SHADER_PARAMETER(FVector4f, BeaconPosStrength)
	SHADER_PARAMETER(float, SmoothingRadius)
	SHADER_PARAMETER(float, GasConstant)
	SHADER_PARAMETER(float, RestDensity)
	SHADER_PARAMETER(float, Viscosity)
	SHADER_PARAMETER(float, AvoidanceStrength)
	SHADER_PARAMETER(float, MaxAcceleration)
	SHADER_PARAMETER(float, DeltaTime)
	SHADER_PARAMETER(uint32, CommandType)
	SHADER_PARAMETER(uint32, FrameIndex)
	SHADER_PARAMETER(uint32, PredictionHorizon)
	SHADER_PARAMETER(uint32, Padding0)
	SHADER_PARAMETER(uint32, Padding1)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

namespace Flight::Swarm
{
/**
 * FDroidState
 * 
 * Persistent per-drone state on the GPU.
 */
struct alignas(16) FDroidState
{
	FVector3f Position;
	float Shield;
	FVector3f Velocity;
	uint32 Status;
};

} // namespace Flight::Swarm
