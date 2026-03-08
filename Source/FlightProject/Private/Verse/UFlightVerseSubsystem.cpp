// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexTypes.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Swarm/SwarmSimulationTypes.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "uLang/Toolchain/Toolchain.h"
#include "VerseVM/VVMInterpreter.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMPlaceholder.h"
#endif

void UFlightVerseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cache symbol definitions for VEX parsing
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
	SymbolDefinitions = Flight::Vex::BuildSymbolDefinitionsFromManifest(Manifest);

	// Build Verse projections for our components
	RegisterNativeComponents();

	// Register built-in functions
	RegisterNativeVerseFunctions();
}

void UFlightVerseSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

bool UFlightVerseSubsystem::CompileVex(uint32 BehaviorID, const FString& VexSource, FString& OutErrors)
{
	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, SymbolDefinitions, false);

	if (Result.Issues.Num() > 0)
	{
		for (const auto& Issue : Result.Issues)
		{
			OutErrors += FString::Printf(TEXT("VEX Error: %s at %d:%d\n"), *Issue.Message, Issue.Line, Issue.Column);
		}
		if (!Result.bSuccess) return false;
	}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	// Store behavior metadata
	FVerseBehavior& Behavior = Behaviors.FindOrAdd(BehaviorID);
	Behavior.ExecutionRateHz = Result.Program.ExecutionRateHz;
	Behavior.FrameInterval = Result.Program.FrameInterval;
	
	// Check for @async
	Behavior.bIsAsync = false;
	for (const auto& S : Result.Program.Statements) 
	{ 
		if (S.Kind == Flight::Vex::EVexStatementKind::TargetDirective && S.SourceSpan == TEXT("@async")) 
		{ 
			Behavior.bIsAsync = true; break; 
		} 
	}
#endif

	// Generate the Verse source code from the VEX AST
	TMap<FString, FString> VerseBySymbol;
	for (const auto& Def : SymbolDefinitions)
	{
		VerseBySymbol.Add(Def.SymbolName, Def.SymbolName.Mid(1)); // Fallback: just remove '@'
	}
	
	const FString VerseCode = Flight::Vex::LowerToVerse(Result.Program, VerseBySymbol);

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	// 1. Create uLang Toolchain
	uLang::SToolchainParams Params;
	// uLang::TSRef<uLang::CToolchain> Toolchain = uLang::CreateToolchain(Params);

	// 2. Parse and Compile
	OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"), 
		BehaviorID, Behaviors[BehaviorID].ExecutionRateHz, Behaviors[BehaviorID].FrameInterval, Behaviors[BehaviorID].bIsAsync ? TEXT("[Async]") : TEXT(""));
	OutErrors += TEXT("Generated Verse:\n") + VerseCode;
#endif

	return true;
}

void UFlightVerseSubsystem::ExecuteBehavior(uint32 BehaviorID, Flight::Swarm::FDroidState& DroidState)
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior) return;

	/*
	VerseContext.EnterVM([&]()
	{
		// ... Execute Behavior->Procedure ...
	});
	*/
#endif
}

void UFlightVerseSubsystem::RegisterNativeComponents()
{
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
namespace
{
	Verse::FOpResult WaitOnGpu_Thunk(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments)
	{
		using namespace Verse;
		if (Arguments.Num() < 1) return FOpResult(FOpResult::Error);

		VPlaceholder& Placeholder = VPlaceholder::New(Context, 0);
		return FOpResult(FOpResult::Return, VValue::Placeholder(Placeholder));
	}
}
#endif

void UFlightVerseSubsystem::RegisterNativeVerseFunctions()
{
}
