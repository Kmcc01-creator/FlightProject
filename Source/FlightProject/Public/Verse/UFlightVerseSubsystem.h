// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vex/FlightVexParser.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMVerseNativeTypeDesc.h"
#endif

#include "UFlightVerseSubsystem.generated.h"

// Forward declaration in correct namespace
namespace Flight::Swarm { struct FDroidState; }

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
	 * Compiles VEX source into Verse bytecode for a specific behavior ID.
	 * This involves VEX parsing -> Verse Lowering -> uLang Compilation.
	 */
	bool CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors);

	/**
	 * Executes the currently loaded Verse behavior on a drone state.
	 * Uses the Verse VM interpreter.
	 */
	void ExecuteBehavior(uint32 BehaviorID, Flight::Swarm::FDroidState& DroidState);

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	struct FVerseBehavior
	{
		TWriteBarrier<Verse::VProcedure> Procedure;
		float ExecutionRateHz = 0.0f;
		uint32 FrameInterval = 1;
		bool bIsAsync = false;
	};

	/** Map of BehaviorID to compiled Verse behavior */
	TMap<uint32, FVerseBehavior> Behaviors;
#endif

private:
	/** Builds Verse descriptors for all reflected types */
	void RegisterNativeComponents();

	/** Registers built-in VEX functions as native Verse functions */
	void RegisterNativeVerseFunctions();

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
