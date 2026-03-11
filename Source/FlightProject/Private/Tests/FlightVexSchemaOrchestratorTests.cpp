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
	const Flight::Reflection::FTypeRegistry::FTypeInfo* TypeInfo = Flight::Reflection::FTypeRegistry::Get().Find(TEXT("FSchemaOrchestratorTestState"));
	TestNotNull(TEXT("Reflection registry should expose orchestrator test state metadata"), TypeInfo);
	if (TypeInfo)
	{
		TestEqual(TEXT("Reflection registry should retain the shared runtime key"), TypeInfo->RuntimeKey, TypeKey);
		TestEqual(TEXT("Reflection registry should classify the test type as auto VEX-capable"), TypeInfo->VexCapability, Flight::Reflection::EVexCapability::VexCapableAuto);
		TestNotNull(TEXT("Reflection registry should expose a lazy VEX schema registration hook"), reinterpret_cast<const void*>(TypeInfo->EnsureVexSchemaRegisteredFn));
		TestNotNull(TEXT("Reflection registry should expose an auto schema provider callback"), reinterpret_cast<const void*>(TypeInfo->ProvideVexSchemaFn));
	}

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

	if (TypeInfo)
	{
		const Flight::Vex::FVexTypeSchema* Reflected = Flight::Vex::FVexSymbolRegistry::Get().GetSchemaForReflectedType(*TypeInfo);
		TestNotNull(TEXT("VEX registry should resolve schemas directly from reflected type metadata"), Reflected);
		if (Reflected)
		{
			TestEqual(TEXT("Reflected-type schema resolution should preserve the shared runtime key"), Reflected->TypeId.RuntimeKey, TypeKey);
			TestEqual(TEXT("Reflected-type schema resolution should preserve its layout hash"), Reflected->LayoutHash, Schema.LayoutHash);
			TestTrue(TEXT("Reflected-type schema resolution should expose the health symbol"), Reflected->HasSymbol(TEXT("@health")));
		}
	}

	Flight::Vex::TTypeVexRegistry<FSchemaOrchestratorTestState>::Register();
	const Flight::Vex::FVexTypeSchema* Registered = Flight::Vex::FVexSymbolRegistry::Get().GetSchema(TypeKey);
	TestNotNull(TEXT("Registered schema should be discoverable by runtime key"), Registered);
	if (Registered)
	{
		TestEqual(TEXT("Registered schema should preserve its layout hash"), Registered->LayoutHash, Schema.LayoutHash);
		TestNotNull(TEXT("Registered schema should expose the canonical health record"), Registered->FindSymbolRecord(TEXT("@health")));
	}

	return true;
}
