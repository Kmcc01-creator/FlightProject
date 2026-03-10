// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/FlightReflection.h"
#include "Vex/FlightVexSchemaOrchestrator.h"
#include "Vex/FlightVexSymbolRegistry.h"

namespace
{

struct FSchemaOrchestratorTestState
{
	float Health = 0.0f;
	bool bEnabled = false;

	FLIGHT_REFLECT_BODY(FSchemaOrchestratorTestState);
};

} // namespace

namespace Flight::Reflection
{

FLIGHT_REFLECT_FIELDS_ATTR(FSchemaOrchestratorTestState,
	FLIGHT_FIELD_ATTR(
		float,
		Health,
		::Flight::Reflection::Attr::VexSymbol<"@health">,
		::Flight::Reflection::Attr::HlslIdentifier<"health_local">,
		::Flight::Reflection::Attr::VerseIdentifier<"state.health">,
		::Flight::Reflection::Attr::VexResidency<EFlightVexSymbolResidency::Shared>
	),
	FLIGHT_FIELD_ATTR(
		bool,
		bEnabled,
		::Flight::Reflection::Attr::VexSymbol<"@enabled">,
		::Flight::Reflection::Attr::VerseIdentifier<"state.enabled">,
		::Flight::Reflection::Attr::VexResidency<EFlightVexSymbolResidency::CpuOnly>
	)
)

} // namespace Flight::Reflection

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexSchemaOrchestratorTest,
	"FlightProject.Vex.Schema.Orchestrator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexSchemaOrchestratorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const void* TypeKey = Flight::Vex::TTypeVexRegistry<FSchemaOrchestratorTestState>::GetTypeKey();
	const Flight::Vex::FVexTypeSchema Schema =
		Flight::Vex::FVexSchemaOrchestrator::BuildSchemaFromReflection<FSchemaOrchestratorTestState>(TypeKey);

	TestTrue(TEXT("Schema type id should be valid"), Schema.TypeId.IsValid());
	TestEqual(TEXT("Schema should retain the runtime type key"), Schema.TypeId.RuntimeKey, TypeKey);
	TestEqual(TEXT("Schema should preserve the reflected type name"), Schema.TypeName, FString(TEXT("FSchemaOrchestratorTestState")));
	TestEqual(TEXT("Schema should record the struct size"), Schema.Size, static_cast<uint32>(sizeof(FSchemaOrchestratorTestState)));
	TestTrue(TEXT("Schema layout hash should be populated"), Schema.LayoutHash != 0);
	TestEqual(TEXT("Schema should build one canonical symbol record per reflected VEX field"), Schema.SymbolRecords.Num(), 2);

	const Flight::Vex::FVexSymbolRecord* HealthRecord = Schema.FindSymbolRecord(TEXT("@health"));
	TestNotNull(TEXT("Schema should contain canonical health symbol record"), HealthRecord);
	if (HealthRecord)
	{
		TestEqual(TEXT("Canonical health record should preserve its storage kind"), HealthRecord->Storage.Kind, Flight::Vex::EVexStorageKind::AosOffset);
	}

	const Flight::Vex::FVexLogicalSymbolSchema* HealthLogical = Schema.FindLogicalSymbol(TEXT("@health"));
	TestNotNull(TEXT("Schema should contain logical health symbol"), HealthLogical);
	if (HealthLogical)
	{
		TestEqual(TEXT("Health should resolve to float type"), HealthLogical->ValueType, Flight::Vex::EVexValueType::Float);
		TestEqual(TEXT("Health should preserve the HLSL binding"), HealthLogical->BackendBinding.HlslIdentifier, FString(TEXT("health_local")));
		TestEqual(TEXT("Health should preserve the Verse binding"), HealthLogical->BackendBinding.VerseIdentifier, FString(TEXT("state.health")));
		TestEqual(TEXT("Health should use AoS offset storage in a standard-layout test struct"), HealthLogical->Storage.Kind, Flight::Vex::EVexStorageKind::AosOffset);
		TestEqual(TEXT("Health offset should match STRUCT_OFFSET"), HealthLogical->Storage.MemberOffset, static_cast<int32>(STRUCT_OFFSET(FSchemaOrchestratorTestState, Health)));
	}

	const Flight::Vex::FVexLogicalSymbolSchema* EnabledLogical = Schema.FindLogicalSymbol(TEXT("@enabled"));
	TestNotNull(TEXT("Schema should contain logical enabled symbol"), EnabledLogical);
	if (EnabledLogical)
	{
		TestEqual(TEXT("Enabled should resolve to bool type"), EnabledLogical->ValueType, Flight::Vex::EVexValueType::Bool);
		TestEqual(TEXT("Enabled should preserve CPU residency"), EnabledLogical->Residency, EFlightVexSymbolResidency::CpuOnly);
	}

	Flight::Vex::TTypeVexRegistry<FSchemaOrchestratorTestState>::Register();
	const Flight::Vex::FVexTypeSchema* Registered = Flight::Vex::FVexSymbolRegistry::Get().GetSchema(TypeKey);
	TestNotNull(TEXT("Registered schema should be discoverable by runtime key"), Registered);
	if (Registered)
	{
		TestEqual(TEXT("Registered schema should preserve its layout hash"), Registered->LayoutHash, Schema.LayoutHash);
		TestTrue(TEXT("Registered schema should expose the health symbol"), Registered->HasSymbol(TEXT("@health")));
		TestNotNull(TEXT("Registered schema should expose the canonical health record"), Registered->FindSymbolRecord(TEXT("@health")));
	}

	return true;
}
