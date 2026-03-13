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

struct FVexSchemaBindingResult;

struct FVexSimdFieldAdapterKey
{
	EVexValueType ValueType = EVexValueType::Unknown;
	EVexVectorStorageClass StorageClass = EVexVectorStorageClass::None;
	bool bWritable = false;
};

struct FVexCompiledSimdFieldBinding
{
	FString SymbolName;
	FVexSimdFieldAdapterKey AdapterKey;
	EVexStorageKind StorageKind = EVexStorageKind::None;
	int32 MemberOffset = INDEX_NONE;
	FName FragmentType = NAME_None;
};

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

	/** Returns true when this build includes the explicit AVX2/FMA kernel backend. */
	static bool HasExplicitAvx256x8KernelSupport();

	/** Returns true when the current host can dispatch the explicit AVX2/FMA kernel backend. */
	static bool IsExplicitAvx256x8RuntimeSupported();

	/**
	 * Returns true only when the AST is compatible with the current SIMD backend.
	 * This is intentionally stricter than generic Tier 1 classification.
	 */
	static bool CanCompileProgram(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, FString* OutReason = nullptr);

	/**
	 * Compile a program for SIMD execution.
	 * Only succeeds if the program is Tier 1 (no branching, no async).
	 */
	static TSharedPtr<FVexSimdExecutor> Compile(TSharedPtr<FVexIrProgram> IrProgram, const FVexSchemaBindingResult* SchemaBinding = nullptr);

	/** Returns true when the current SIMD plan can execute through the explicit AVX2/FMA bulk kernel. */
	bool SupportsExplicitAvx256x8() const;

	/** Returns true when the current SIMD plan can execute through the explicit AVX2/FMA direct fragment kernel. */
	bool SupportsExplicitAvx256x8Direct() const;

	/** Execute the plan across a bulk set of droid states. */
	void Execute(TArrayView<Flight::Swarm::FDroidState> DroidStates) const;

	/** Execute the plan across a bulk set of droid states via the explicit AVX2/FMA kernel when available. */
	bool ExecuteExplicitAvx256x8(TArrayView<Flight::Swarm::FDroidState> DroidStates) const;

	/** Execute the plan directly on Mass fragments via the explicit AVX2/FMA kernel when available. */
	bool ExecuteExplicitAvx256x8Direct(
		TArrayView<FFlightTransformFragment> Transforms,
		TArrayView<FFlightDroidStateFragment> DroidStates) const;

	/**
	 * Execute the plan directly on Mass fragments (SoA).
	 * Avoids gather/scatter to POD structures.
	 */
	void ExecuteDirect(
		TArrayView<FFlightTransformFragment> Transforms,
		TArrayView<FFlightDroidStateFragment> DroidStates) const;

	TSharedPtr<FVexIrProgram> Ir;

private:
	const FVexCompiledSimdFieldBinding* FindCompiledFieldBinding(const FString& SymbolName) const;

	TMap<FString, FVexCompiledSimdFieldBinding> CompiledFieldBindings;
};

} // namespace Flight::Vex
