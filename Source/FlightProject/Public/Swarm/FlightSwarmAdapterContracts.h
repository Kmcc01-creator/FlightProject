#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Vex/FlightVexSymbolRegistry.h"

namespace Flight::Swarm::AdapterContracts
{

inline const FName SpawnBatchKey(TEXT("Swarm.SpawnBatch"));

struct FFlightSpawnAnchorBatchContract
{
	int32 DroneCount = 0;
	float PhaseOffsetDeg = 0.0f;
	float PhaseSpreadDeg = 360.0f;
	float DesiredSpeed = -1.0f;
	bool bLooping = true;

	FLIGHT_REFLECT_BODY(FFlightSpawnAnchorBatchContract);
};

} // namespace Flight::Swarm::AdapterContracts

namespace Flight::Reflection
{

FLIGHT_REFLECT_FIELDS_ATTR(
	Flight::Swarm::AdapterContracts::FFlightSpawnAnchorBatchContract,
	FLIGHT_FIELD_ATTR(int32, DroneCount, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Swarm">, ::Flight::Reflection::Attr::VexSymbol<"@swarm_drone_count">),
	FLIGHT_FIELD_ATTR(float, PhaseOffsetDeg, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Swarm">, ::Flight::Reflection::Attr::VexSymbol<"@swarm_phase_offset_deg">),
	FLIGHT_FIELD_ATTR(float, PhaseSpreadDeg, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Swarm">, ::Flight::Reflection::Attr::VexSymbol<"@swarm_phase_spread_deg">),
	FLIGHT_FIELD_ATTR(float, DesiredSpeed, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Swarm">, ::Flight::Reflection::Attr::VexSymbol<"@swarm_desired_speed">),
	FLIGHT_FIELD_ATTR(bool, bLooping, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Swarm">, ::Flight::Reflection::Attr::VexSymbol<"@swarm_looping">)
)

} // namespace Flight::Reflection
