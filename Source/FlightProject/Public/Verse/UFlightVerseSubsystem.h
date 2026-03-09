// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vex/FlightVexParser.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMVerseNativeTypeDesc.h"
#endif

#include "UFlightVerseSubsystem.generated.h"

// Forward declaration in correct namespace
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

	/**
	 * Compiles VEX source for a specific behavior ID.
	 * Returns true only when an executable behavior is produced.
	 */
	bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors);

	/**
	 * Executes the currently loaded Verse behavior on a drone state.
	 * Uses Verse VM when available, otherwise native fallback execution.
	 */
	void ExecuteBehavior(uint32 BehaviorID, Flight::Swarm::FDroidState& DroidState);

	/** Returns compile-state metadata for a behavior ID. */
	EFlightVerseCompileState GetBehaviorCompileState(uint32 BehaviorID) const;

	/** Returns true when a behavior has an executable runtime path. */
	bool HasExecutableBehavior(uint32 BehaviorID) const;

	/** Returns the most recent compile diagnostics string for a behavior ID. */
	FString GetBehaviorCompileDiagnostics(uint32 BehaviorID) const;

		struct FVerseBehavior
		{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
			TWriteBarrier<Verse::VProcedure> Procedure;
#endif
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

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	bool TryValidateVerseSource(const FString& VerseSource, FString& OutDiagnostics) const;
	bool TryCreateVmProcedure(uint32 BehaviorID, FVerseBehavior& Behavior, FString& OutDiagnostics);
	static Verse::FOpResult VmBehaviorEntryThunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments);
	Verse::FOpResult ExecuteVmEntryThunk(Verse::FRunningContext Context, Verse::VNativeFunction::Args Arguments);

	// Active execution payload consumed by VM thunk invocation.
	uint32 ActiveVmBehaviorID = 0;
	Flight::Swarm::FDroidState* ActiveVmDroidState = nullptr;
#endif

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	/** Map of reflected type name to its Verse Native Descriptor */
	TMap<FString, FVniTypeDesc> NativeTypeDescriptors;

	/** Pending GPU readbacks waiting for fulfillment */
	TMap<int64, Verse::VPlaceholder*> PendingReadbacks;

	/** The Verse VM context for this world */
	Verse::FRunningContext VerseContext;
#endif
	/** Cached symbol definitions from the schema manifest */
	TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions;
};
