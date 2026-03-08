// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Core/FlightReactive.h"
#include "ShaderParameterMacros.h"
#include "UniformBuffer.h"

namespace Flight::Swarm
{
inline constexpr uint32 GridHashSize = 1048576; // Must match GRID_HASH_SIZE in FlightSwarmCommon.ush
inline constexpr float GridCellSize = 500.0f;
inline constexpr uint32 MaxEvents = 1024; // Max active transient events per frame

/** VEX Behavioral Event Types */
namespace Events
{
	static const uint32 None       = 0;
	static const uint32 Explosion  = 1;
	static const uint32 Alert      = 2;
	static const uint32 Attraction = 3;
	static const uint32 Repulsion  = 4;
}

/** 
 * FTransientEvent
 * A lightweight world event injected from C++/Verse to affect Swarm logic.
 */
struct FTransientEvent
{
	FVector3f Position;
	float Radius = 0.0f;
	uint32 Type = 0;
	float Intensity = 0.0f;
	FVector2f Padding; // RHI Padding
};

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

/** Parameters for the Light Lattice (Cellular Propagation) */
struct FSwarmLatticeParams
{
	int32 GridResolution = 64; // e.g., 64x64x64
	float GridExtent = 10000.0f; // Total world size covered by the lattice
	float DecayRate = 0.95f; // Energy loss per propagation step
	float PropagationStrength = 0.15f; // Speed of light diffusion

	FLIGHT_REFLECT_BODY(FSwarmLatticeParams);
};

/** Parameters for Volumetric Clouds (Density Fields) */
struct FSwarmCloudParams
{
	int32 GridResolution = 64;
	float GridExtent = 10000.0f;
	float DecayRate = 0.98f;      // How fast the cloud dissipates
	float DiffusionStrength = 0.1f; // How fast the cloud spreads
	float InjectionAmount = 1.0f;   // How much each entity contributes

	FLIGHT_REFLECT_BODY(FSwarmCloudParams);
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

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Swarm::FSwarmLatticeParams,
	FLIGHT_FIELD_ATTR(int32, GridResolution, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::ClampedValue<32, 256>),
	FLIGHT_FIELD_ATTR(float, GridExtent, ::Flight::Reflection::Attr::EditAnywhere),
	FLIGHT_FIELD_ATTR(float, DecayRate, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::ClampedValue<0, 1>),
	FLIGHT_FIELD_ATTR(float, PropagationStrength, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::ClampedValue<0, 1>)
)

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Swarm::FSwarmCloudParams,
	FLIGHT_FIELD_ATTR(int32, GridResolution, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::ClampedValue<32, 256>),
	FLIGHT_FIELD_ATTR(float, GridExtent, ::Flight::Reflection::Attr::EditAnywhere),
	FLIGHT_FIELD_ATTR(float, DecayRate, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::ClampedValue<0, 1>),
	FLIGHT_FIELD_ATTR(float, DiffusionStrength, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::ClampedValue<0, 1>),
	FLIGHT_FIELD_ATTR(float, InjectionAmount, ::Flight::Reflection::Attr::EditAnywhere)
)

/**
 * FSwarmGlobalCommand
 * 
 * The "Command UBO" packed for the GPU.
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSwarmGlobalCommand, FLIGHTGPUCOMPUTE_API)
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
	// SCSL: Light Lattice
	SHADER_PARAMETER(uint32, LatticeResolution)
	SHADER_PARAMETER(float, LatticeExtent)
	SHADER_PARAMETER(float, LatticeDecay)
	SHADER_PARAMETER(float, LatticePropStrength)
	// SCSL: Volumetric Cloud
	SHADER_PARAMETER(uint32, CloudResolution)
	SHADER_PARAMETER(float, CloudExtent)
	SHADER_PARAMETER(float, CloudDecay)
	SHADER_PARAMETER(float, CloudDiffusion)
	SHADER_PARAMETER(float, CloudInjection)
	SHADER_PARAMETER(uint32, NumEvents)
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

	FLIGHT_REFLECT_BODY(FDroidState);
};

} // namespace Flight::Swarm

FLIGHT_REFLECT_FIELDS_VEX(Flight::Swarm::FDroidState, 
	::Flight::Reflection::TAttributeSet<::Flight::Reflection::Attr::VersePackage<"Flight.Swarm">>,
	FLIGHT_FIELD_ATTR(FVector3f, Position, 
		::Flight::Reflection::Attr::VexSymbol<"@position">,
		::Flight::Reflection::Attr::VexResidency<EFlightVexSymbolResidency::Shared>,
		::Flight::Reflection::Attr::HlslIdentifier<"PosI">,
		::Flight::Reflection::Attr::VerseIdentifier<"Droid.Position">,
		::Flight::Reflection::Attr::VisibleAnywhere
	),
	FLIGHT_FIELD_ATTR(float, Shield, 
		::Flight::Reflection::Attr::VexSymbol<"@shield">,
		::Flight::Reflection::Attr::VexResidency<EFlightVexSymbolResidency::GpuOnly>,
		::Flight::Reflection::Attr::HlslIdentifier<"ShieldI">,
		::Flight::Reflection::Attr::VerseIdentifier<"Droid.Shield">,
		::Flight::Reflection::Attr::EditAnywhere
	),
	FLIGHT_FIELD_ATTR(FVector3f, Velocity, 
		::Flight::Reflection::Attr::VexSymbol<"@velocity">,
		::Flight::Reflection::Attr::VexResidency<EFlightVexSymbolResidency::Shared>,
		::Flight::Reflection::Attr::HlslIdentifier<"VelI">,
		::Flight::Reflection::Attr::VerseIdentifier<"Droid.Velocity">,
		::Flight::Reflection::Attr::EditAnywhere
	),
	FLIGHT_FIELD_ATTR(uint32, Status, 
		::Flight::Reflection::Attr::VexSymbol<"@status">,
		::Flight::Reflection::Attr::VexResidency<EFlightVexSymbolResidency::GpuOnly>,
		::Flight::Reflection::Attr::HlslIdentifier<"StatusI">,
		::Flight::Reflection::Attr::VerseIdentifier<"Droid.Status">,
		::Flight::Reflection::Attr::EditAnywhere
	)
)
