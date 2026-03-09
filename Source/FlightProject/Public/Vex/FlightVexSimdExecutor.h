// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexParser.h"
#include "Swarm/SwarmSimulationTypes.h"
#include "Vex/FlightVexIr.h"

struct FFlightTransformFragment;
struct FFlightDroidStateFragment;

namespace Flight::Vex
{

/**
 * FVexSimdExecutor
 * 
 * Compiles a Tier 1 (Literal) VEX program into a vectorized execution plan.
 * Uses Unreal's FVector4f and math intrinsics to process entities in batches of 4.
 */
class FVexSimdExecutor
{
public:
	FVexSimdExecutor() = default;

	/**
	 * Returns true only when the AST is compatible with the current SIMD backend.
	 * This is intentionally stricter than generic Tier 1 classification.
	 */
	static bool CanCompileProgram(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, FString* OutReason = nullptr);

	/**
	 * Compile a program for SIMD execution.
	 * Only succeeds if the program is Tier 1 (no branching, no async).
	 */
	static TSharedPtr<FVexSimdExecutor> Compile(TSharedPtr<FVexIrProgram> IrProgram);

	/** Execute the plan across a bulk set of droid states. */
	void Execute(TArrayView<Flight::Swarm::FDroidState> DroidStates) const;

	/**
	 * Execute the plan directly on Mass fragments (SoA).
	 * Avoids gather/scatter to POD structures.
	 */
	void ExecuteDirect(
		TArrayView<FFlightTransformFragment> Transforms,
		TArrayView<FFlightDroidStateFragment> DroidStates) const;

	TSharedPtr<FVexIrProgram> Ir;
};

} // namespace Flight::Vex
