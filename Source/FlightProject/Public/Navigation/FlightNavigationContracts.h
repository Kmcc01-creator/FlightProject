// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Vex/FlightVexSymbolRegistry.h"

namespace Flight::Navigation
{

namespace Contracts
{
	inline const FName IntentKey(TEXT("Navigation.Intent"));
	inline const FName CandidateKey(TEXT("Navigation.Candidate"));
	inline const FName FieldSampleKey(TEXT("Navigation.FieldSample"));
	inline const FName CommitKey(TEXT("Navigation.Commit"));
}

struct FFlightNavigationIntentContract
{
	FName TargetAnchor = NAME_None;
	float PreferredAltitude = 1500.0f;
	float ClearanceRadius = 300.0f;
	float RouteBias = 0.0f;
	bool bAllowDegraded = true;

	FLIGHT_REFLECT_BODY(FFlightNavigationIntentContract);
};

struct FFlightNavigationCandidateContract
{
	FGuid RouteId;
	FName RouteClass = NAME_None;
	float EstimatedCost = 0.0f;
	int32 LegalityState = 0;
	FName RejectionReason = NAME_None;

	FLIGHT_REFLECT_BODY(FFlightNavigationCandidateContract);
};

struct FFlightNavigationFieldSampleContract
{
	float Hazard = 0.0f;
	float Clearance = 0.0f;
	float Density = 0.0f;
	FVector3f FlowDirection = FVector3f::ZeroVector;
	bool bVisible = true;

	FLIGHT_REFLECT_BODY(FFlightNavigationFieldSampleContract);
};

struct FFlightNavigationCommitContract
{
	float PathProgress = 0.0f;
	float DesiredSpeed = 1500.0f;
	bool bLooping = true;

	FLIGHT_REFLECT_BODY(FFlightNavigationCommitContract);
};

FLIGHTPROJECT_API ::Flight::Reflection::FVexSchemaProviderResult RegisterNavigationCommitSchema();

} // namespace Flight::Navigation

namespace Flight::Reflection
{

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Navigation::FFlightNavigationIntentContract,
	FLIGHT_FIELD_ATTR(FName, TargetAnchor, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">),
	FLIGHT_FIELD_ATTR(float, PreferredAltitude, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_preferred_altitude">),
	FLIGHT_FIELD_ATTR(float, ClearanceRadius, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_clearance_radius">),
	FLIGHT_FIELD_ATTR(float, RouteBias, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_route_bias">),
	FLIGHT_FIELD_ATTR(bool, bAllowDegraded, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_allow_degraded">)
)

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Navigation::FFlightNavigationCandidateContract,
	FLIGHT_FIELD_ATTR(FGuid, RouteId, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">),
	FLIGHT_FIELD_ATTR(FName, RouteClass, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">),
	FLIGHT_FIELD_ATTR(float, EstimatedCost, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">),
	FLIGHT_FIELD_ATTR(int32, LegalityState, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">),
	FLIGHT_FIELD_ATTR(FName, RejectionReason, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">)
)

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Navigation::FFlightNavigationFieldSampleContract,
	FLIGHT_FIELD_ATTR(float, Hazard, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_hazard">),
	FLIGHT_FIELD_ATTR(float, Clearance, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_clearance">),
	FLIGHT_FIELD_ATTR(float, Density, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_density">),
	FLIGHT_FIELD_ATTR(FVector3f, FlowDirection, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_flow_direction">),
	FLIGHT_FIELD_ATTR(bool, bVisible, ::Flight::Reflection::Attr::EditAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_visible">)
)

using FFlightNavigationCommitReflectAttrs = ::Flight::Reflection::TAttributeSet<>;

FLIGHT_REFLECT_FIELDS_VEX_MANUAL(Flight::Navigation::FFlightNavigationCommitContract,
	FFlightNavigationCommitReflectAttrs,
	::Flight::Navigation::RegisterNavigationCommitSchema,
	FLIGHT_FIELD_ATTR(float, PathProgress, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_path_progress">),
	FLIGHT_FIELD_ATTR(float, DesiredSpeed, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_desired_speed">),
	FLIGHT_FIELD_ATTR(bool, bLooping, ::Flight::Reflection::Attr::VisibleAnywhere, ::Flight::Reflection::Attr::Category<"Navigation">, ::Flight::Reflection::Attr::VexSymbol<"@nav_looping">)
)

} // namespace Flight::Reflection
