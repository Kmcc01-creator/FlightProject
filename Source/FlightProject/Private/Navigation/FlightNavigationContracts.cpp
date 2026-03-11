// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Navigation/FlightNavigationContracts.h"

#include "Mass/FlightMassFragments.h"
#include "Vex/FlightVexSchemaOrchestrator.h"
#include "Vex/FlightVexSymbolRegistry.h"

namespace Flight::Navigation
{

::Flight::Reflection::FVexSchemaProviderResult RegisterNavigationCommitSchema()
{
	using namespace Flight::Vex;

	FVexTypeSchema Schema;
	Schema.TypeId.RuntimeKey = Flight::Reflection::GetRuntimeTypeKey<FFlightNavigationCommitContract>();
	Schema.TypeId.StableName = TEXT("FFlightNavigationCommitContract");
	Schema.TypeName = TEXT("FFlightNavigationCommitContract");
	Schema.Size = sizeof(FFlightPathFollowFragment);
	Schema.Alignment = alignof(FFlightPathFollowFragment);

	FVexSymbolRecord PathProgress;
	PathProgress.SymbolName = TEXT("@nav_path_progress");
	PathProgress.ValueType = EVexValueType::Float;
	PathProgress.Storage.Kind = EVexStorageKind::MassFragmentField;
	PathProgress.Storage.FragmentType = TEXT("FFlightPathFollowFragment");
	PathProgress.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightPathFollowFragment, CurrentDistance));
	PathProgress.Storage.ElementStride = sizeof(float);
	Schema.SymbolRecords.Add(PathProgress.SymbolName, MoveTemp(PathProgress));

	FVexSymbolRecord DesiredSpeed;
	DesiredSpeed.SymbolName = TEXT("@nav_desired_speed");
	DesiredSpeed.ValueType = EVexValueType::Float;
	DesiredSpeed.Storage.Kind = EVexStorageKind::MassFragmentField;
	DesiredSpeed.Storage.FragmentType = TEXT("FFlightPathFollowFragment");
	DesiredSpeed.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightPathFollowFragment, DesiredSpeed));
	DesiredSpeed.Storage.ElementStride = sizeof(float);
	Schema.SymbolRecords.Add(DesiredSpeed.SymbolName, MoveTemp(DesiredSpeed));

	FVexSymbolRecord Looping;
	Looping.SymbolName = TEXT("@nav_looping");
	Looping.ValueType = EVexValueType::Bool;
	Looping.Storage.Kind = EVexStorageKind::MassFragmentField;
	Looping.Storage.FragmentType = TEXT("FFlightPathFollowFragment");
	Looping.Storage.MemberOffset = static_cast<int32>(STRUCT_OFFSET(FFlightPathFollowFragment, bLooping));
	Looping.Storage.ElementStride = sizeof(bool);
	Schema.SymbolRecords.Add(Looping.SymbolName, MoveTemp(Looping));

	Schema.RebuildLegacyViews();
	Schema.LayoutHash = Flight::Vex::FVexSchemaOrchestrator::ComputeSchemaLayoutHash(Schema);
	Flight::Vex::FVexSymbolRegistry::Get().RegisterSchema(MoveTemp(Schema));
	return Flight::Reflection::FVexSchemaProviderResult::Success(TEXT("Navigation commit schema provider registered the Mass-backed path-follow schema."));
}

} // namespace Flight::Navigation
