// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vex/FlightVexSchema.h"
#include "Vex/FlightCompileArtifacts.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexOptics.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMVerseNativeTypeDesc.h"
#include "VerseVM/VVMWriteBarrier.h"
#endif

#include "UFlightVerseSubsystem.generated.h"

// Forward declarations in correct namespace
namespace Flight::Vex { class FVexSimdExecutor; }
namespace Flight::Swarm { struct FDroidState; }

UENUM(BlueprintType)
enum class EFlightVerseCompileState : uint8
{
	GeneratedOnly UMETA(DisplayName = "GeneratedOnly"),
	VmCompiled UMETA(DisplayName = "VmCompiled"),
	VmCompileFailed UMETA(DisplayName = "VmCompileFailed")
};

/**
 * UFlightVerseSubsystem
 *
 * Bridges the VEX frontend with the Unreal Verse VM.
 * Handles live-compilation and execution of VEX-generated Verse code.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightVerseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Garbage Collection integration for Verse VM closures */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/**
	 * Compiles VEX source for a specific behavior ID.
	 * Returns true only when an executable behavior is produced.
	 */
	bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors, UScriptStruct* TargetStruct = nullptr);

	/**
	 * Executes the currently loaded Verse behavior on a drone state.
	 * Uses Verse VM when available, otherwise native fallback execution.
	 */
	void ExecuteBehavior(uint32 BehaviorID, Flight::Swarm::FDroidState& DroidState);

	/**
	 * Executes a literal Tier 1 script AST directly on memory using schema offsets.
	 * This is the zero-cost path for non-Verse VM execution.
	 */
	void ExecuteOnSchema(const Flight::Vex::FVexProgramAst& Program, void* StructPtr, const Flight::Vex::FVexTypeSchema& Schema);

	/**
	 * Executes a behavior on an arbitrary C++ struct.
	 */
	void ExecuteBehaviorOnStruct(uint32 BehaviorID, void* StructPtr, const void* TypeKey);

	/**
	 * Executes a behavior on multiple entities in bulk.
	 * Optimized path for Tier 1 (Literal) scripts using SIMD where possible.
	 */
	void ExecuteBehaviorBulk(uint32 BehaviorID, TArrayView<Flight::Swarm::FDroidState> DroidStates);

	/**
	 * Executes a behavior directly on Mass fragments.
	 * Bypasses gather/scatter for eligible Tier 1 scripts.
	 */
	void ExecuteBehaviorDirect(
		uint32 BehaviorID,
		TArrayView<struct FFlightTransformFragment> Transforms,
		TArrayView<struct FFlightDroidStateFragment> DroidStates);

	/** Returns compile-state metadata for a behavior ID. */
	EFlightVerseCompileState GetBehaviorCompileState(uint32 BehaviorID) const;

	/** Returns true when a behavior has an executable runtime path. */
	bool HasExecutableBehavior(uint32 BehaviorID) const;

	/** Returns the most recent compile diagnostics string for a behavior ID. */
	FString GetBehaviorCompileDiagnostics(uint32 BehaviorID) const;

	/** Returns the latest compile artifact report for a behavior ID, if available. */
	const Flight::Vex::FFlightCompileArtifactReport* GetBehaviorCompileArtifactReport(uint32 BehaviorID) const;

	/** Returns the latest compile artifact report as JSON for a behavior ID, if available. */
	FString GetBehaviorCompileArtifactReportJson(uint32 BehaviorID) const;

	struct FVerseBehavior
	{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		TWriteBarrier<Verse::VProcedure> Procedure;
#endif
		Flight::Vex::EVexTier Tier = Flight::Vex::EVexTier::Full;
		TSharedPtr<Flight::Vex::FVexSimdExecutor> SimdPlan;
		FString SimdCompileDiagnostics;
		float ExecutionRateHz = 0.0f;
		uint32 FrameInterval = 1;
		bool bIsAsync = false;
		bool bHasExecutableProcedure = false;
		bool bUsesNativeFallback = false;
		bool bUsesVmEntryPoint = false;
		EFlightVerseCompileState CompileState = EFlightVerseCompileState::VmCompileFailed;
		Flight::Vex::FVexProgramAst NativeProgram;
		FString GeneratedVerseCode;
		FString LastCompileDiagnostics;
		Flight::Vex::FFlightCompileArtifactReport CompileArtifactReport;
		bool bHasCompileArtifactReport = false;
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
		TWriteBarrier<Verse::VNativeFunction> VmEntryPoint;
#endif
	};

	/** Map of BehaviorID to compiled Verse behavior */
	TMap<uint32, FVerseBehavior> Behaviors;

private:
	/** Builds Verse descriptors for all reflected types */
	void RegisterNativeComponents();

	/** Registers built-in VEX functions as native Verse functions */
	void RegisterNativeVerseFunctions();

	/** Registers VEX @symbols to C++ FDroidState fields */
	void RegisterNativeVerseSymbols();

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	/** Core execution logic that assumes the caller has already called EnterVM() */
	void ExecuteBehaviorInContext(uint32 BehaviorID, const FVerseBehavior& Behavior, void* StructPtr, const void* TypeKey, Verse::FAllocationContext& AllocContext);
#endif

	/** A deferred closure representing a behavior execution pending async completion. */
	struct FVexDeferredBehavior
	{
		uint32 BehaviorID;
		void* StatePtr;
		const void* TypeKey;
	};

	/** Registers a behavior for deferred batch execution (Async Fusion). */
	void DeferBehavior(uint32 BehaviorID, void* StructPtr, const void* TypeKey);

	/** Flushes all deferred behaviors in a single fused VVM transaction. */
	void FlushDeferredBehaviors();

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	bool TryValidateVerseSource(const FString& VerseSource, FString& OutDiagnostics) const;
	bool TryCreateVmProcedure(uint32 BehaviorID, FVerseBehavior& Behavior, TSharedPtr<Flight::Vex::FVexIrProgram> IrProgram, FString& OutDiagnostics);
	static Verse::FOpResult VmBehaviorEntryThunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);
	Verse::FOpResult ExecuteVmEntryThunk(Verse::FRunningContext Context, Verse::VNativeFunction::Args Arguments);

	// VEX IR -> VVM Native Thunks (Now generalized)
	static Verse::FOpResult GetStateValue(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);
	static Verse::FOpResult SetStateValue(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);

	/** Generic dispatcher for registered native thunks */
	static Verse::FOpResult RegistryThunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);

	/** Async completion entrypoint for io_uring bridge */
	void CompleteGpuWait(int64 RequestId);

	// Active execution payload consumed by VM thunk invocation.
	uint32 ActiveVmBehaviorID = 0;
	void* ActiveVmStatePtr = nullptr;
	const void* ActiveVmTypeKey = nullptr;
#endif

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	/** Map of reflected type name to its Verse Native Descriptor */
	TMap<FString, FVniTypeDesc> NativeTypeDescriptors;

	/** Pending GPU readbacks waiting for fulfillment (GC Rooted) */
	TMap<int64, Verse::TWriteBarrier<Verse::VPlaceholder>> PendingReadbacks;

	/** The Verse VM context for this world */
	Verse::FRunningContext VerseContext;
#endif
	/** Cached symbol definitions from the schema manifest */
	TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions;

private:
	/** Collection of behaviors pending fused batch reduction */
	TArray<FVexDeferredBehavior> DeferredBehaviors;
};
