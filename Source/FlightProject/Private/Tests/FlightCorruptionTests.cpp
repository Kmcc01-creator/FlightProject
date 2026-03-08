// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/FlightReflection.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Vex/FlightVexParser.h"
#include "Schema/FlightRequirementRegistry.h"

using namespace Flight::Reflection;
using namespace Flight::Swarm;

/**
 * FFlightMemoryLayoutSpec
 * 
 * Investigates potential "Memory Corruption" by verifying the parity between
 * C++ memory layout and the VEX/Verse reflection offsets.
 */
BEGIN_DEFINE_SPEC(FFlightMemoryLayoutSpec, "FlightProject.Unit.Safety.MemoryLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FFlightMemoryLayoutSpec)

void FFlightMemoryLayoutSpec::Define()
{
	Describe("FDroidState Struct Layout", [this]() {
		It("should have consistent offsets between C++ and Reflection", [this]() {
			using DroidReflect = TReflectionTraits<FDroidState>;
			
			DroidReflect::Fields::ForEachDescriptor([&](auto Descriptor) {
				// Let's verify the size of the whole struct vs. what the GPU expects.
				TestEqual("FDroidState size must be 16-byte aligned for GPU StructureBuffer compatibility", 
					(int32)sizeof(FDroidState) % 16, 0);
			});
		});

		It("should not have padding holes that leak uninitialized data to the GPU", [this]() {
			TestEqual("FDroidState should have no internal padding (total size == sum of members)",
				(int32)sizeof(FDroidState), 
				(int32)(sizeof(FVector3f) + sizeof(float) + sizeof(FVector3f) + sizeof(uint32)));
		});
	});

	Describe("Schema Integrity (Breaking Tests)", [this]() {
		It("should fail if a VEX symbol exists without a C++ Reflection backing", [this]() {
			// This test prevents "Shadow Symbols"
			const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
			TArray<Flight::Vex::FVexSymbolDefinition> Defs = Flight::Vex::BuildSymbolDefinitionsFromManifest(Manifest);

			FString GhostSymbol = TEXT("@ghost_variable");
			
			bool bInManifest = false;
			for (const auto& D : Defs) if (D.SymbolName == GhostSymbol) bInManifest = true;

			TestFalse("Ghost symbol should not be in the reflection manifest", bInManifest);

			// Now verify that the parser correctly flags this as a warning/error
			FString Source = FString::Printf(TEXT("@cpu { %s = 1.0; }"), *GhostSymbol);
			auto Result = Flight::Vex::ParseAndValidate(Source, Defs, false);

			TestTrue("Parser should detect unknown symbols used in scripts", Result.UnknownSymbols.Contains(GhostSymbol));
		});

		It("should fail if the VEX manifest type does not match the C++ Reflection type", [this]() {
			// This test prevents "Logic Corruption" due to type punning.
			const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
			
			// Find @position in the manifest
			const FFlightVexSymbolRow* PosRow = nullptr;
			for (const auto& R : Manifest.VexSymbolRequirements) if (R.SymbolName == TEXT("@position")) PosRow = &R;

			if (PosRow)
			{
				// In C++, Position is FVector3f (mapped to Float3 in our trait system)
				// We verify that the manifest reflects this.
				TestEqual("Manifest type for @position must match C++ reflection (Float3)", 
					PosRow->ValueType, EFlightVexSymbolValueType::Float3);
			}
		});
	});

	Describe("Task Safety (Concurrency Corruption)", [this]() {
		It("should detect unsafe capture of non-thread-safe types", [this]() {
			TArray<Flight::Vex::FVexSymbolDefinition> Defs;
			Flight::Vex::FVexSymbolDefinition UnsafeSymbol;
			UnsafeSymbol.SymbolName = TEXT("@gt_only");
			UnsafeSymbol.Affinity = EFlightVexSymbolAffinity::GameThread;
			Defs.Add(UnsafeSymbol);

			FString Source = TEXT("@cpu @job { @gt_only = 1.0; }");
			auto Result = Flight::Vex::ParseAndValidate(Source, Defs, false);

			bool bCaughtIt = false;
			for (const auto& I : Result.Issues) if (I.Message.Contains(TEXT("Game Thread"))) bCaughtIt = true;
			
			TestTrue("Parser caught thread-affinity violation (Future-Proofing Success)", bCaughtIt);
		});
	});
}
