// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexSymbolRegistry.h"
#include "Core/FlightReflection.h"

/**
 * FGeneralVexTestState
 * A non-swarm struct used to verify generalized VEX system authoring.
 */
struct FGeneralVexTestState
{
	float Health = 100.0f;
	float Ammo = 50.0f;
	float Score = 0.0f;

	FLIGHT_REFLECT_BODY(FGeneralVexTestState);
};

FLIGHT_REFLECT_FIELDS_ATTR(FGeneralVexTestState,
	FLIGHT_FIELD_ATTR(float, Health, ::Flight::Reflection::Attr::VexSymbol<"@health">),
	FLIGHT_FIELD_ATTR(float, Ammo, ::Flight::Reflection::Attr::VexSymbol<"@ammo">),
	FLIGHT_FIELD_ATTR(float, Score, ::Flight::Reflection::Attr::VexSymbol<"@score">)
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightVexGeneralizationTest, "FlightProject.Vex.Generalization.ExecuteOnCustomStruct", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexGeneralizationTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			World = Context.World();
			break;
		}
	}

	if (!World) return false;

	UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
	if (!VerseSubsystem) return false;

	// 1. Register the custom struct symbols
	Flight::Vex::TTypeVexRegistry<FGeneralVexTestState>::Register();

	// 2. Compile a VEX script that operates on this struct
	// Note: @score = @health + @ammo
	const FString VexSource = TEXT("@score = @health + @ammo; @health -= 10.0;");
	FString Errors;
	const uint32 BehaviorID = 9999; // Arbitrary test ID

	const void* TypeKey = Flight::Vex::TTypeVexRegistry<FGeneralVexTestState>::GetTypeKey();

	if (!VerseSubsystem->CompileVex(BehaviorID, VexSource, Errors, (UScriptStruct*)TypeKey))
	{
		AddError(FString::Printf(TEXT("VEX Compile Failed: %s"), *Errors));
		return false;
	}

	// 3. Prepare an instance
	FGeneralVexTestState TestState;
	TestState.Health = 80.0f;
	TestState.Ammo = 20.0f;
	TestState.Score = 0.0f;

	// 4. Execute
	VerseSubsystem->ExecuteBehaviorOnStruct(BehaviorID, &TestState, TypeKey);

	// 5. Verify results
	TestEqual(TEXT("Score should be Health + Ammo (80 + 20)"), TestState.Score, 100.0f);
	TestEqual(TEXT("Health should be decremented (80 - 10)"), TestState.Health, 70.0f);

	return true;
}
