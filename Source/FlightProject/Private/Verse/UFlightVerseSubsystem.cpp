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

DEFINE_LOG_CATEGORY_STATIC(LogFlightVerseSubsystem, Log, All);

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
	// Initialize behavior state pessimistically; flip to executable only on true VM compile success.
	FVerseBehavior& Behavior = Behaviors.FindOrAdd(BehaviorID);
	Behavior.CompileState = EFlightVerseCompileState::VmCompileFailed;
	Behavior.bHasExecutableProcedure = false;
	Behavior.GeneratedVerseCode.Reset();
	Behavior.LastCompileDiagnostics.Reset();

	const Flight::Vex::FVexParseResult Result = Flight::Vex::ParseAndValidate(VexSource, SymbolDefinitions, false);

	if (Result.Issues.Num() > 0)
	{
		for (const auto& Issue : Result.Issues)
		{
			const TCHAR* SeverityLabel = Issue.Severity == Flight::Vex::EVexIssueSeverity::Error ? TEXT("Error") : TEXT("Warning");
			OutErrors += FString::Printf(TEXT("VEX %s: %s at %d:%d\n"), SeverityLabel, *Issue.Message, Issue.Line, Issue.Column);
		}
		if (!Result.bSuccess)
		{
			Behavior.LastCompileDiagnostics = OutErrors;
			return false;
		}
	}

	// Enforce required-symbol contract in compile pipeline until parser-side strict mode lands.
	TSet<FString> UsedSymbols;
	for (const FString& Symbol : Result.Program.UsedSymbols)
	{
		UsedSymbols.Add(Symbol);
	}

	TArray<FString> MissingRequiredSymbols;
	for (const Flight::Vex::FVexSymbolDefinition& Def : SymbolDefinitions)
	{
		if (Def.bRequired && !UsedSymbols.Contains(Def.SymbolName))
		{
			MissingRequiredSymbols.Add(Def.SymbolName);
		}
	}
	MissingRequiredSymbols.Sort();

	if (MissingRequiredSymbols.Num() > 0)
	{
		OutErrors += FString::Printf(
			TEXT("VEX Contract Error: missing required symbols: %s\n"),
			*FString::Join(MissingRequiredSymbols, TEXT(", ")));
		Behavior.LastCompileDiagnostics = OutErrors;
		return false;
	}

	// Store behavior metadata regardless of VM availability.
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

	// Generate the Verse source code from the VEX AST
	TMap<FString, FString> VerseBySymbol;
	for (const auto& Def : SymbolDefinitions)
	{
		VerseBySymbol.Add(Def.SymbolName, Def.SymbolName.Mid(1)); // Fallback: just remove '@'
	}
	
	const FString VerseCode = Flight::Vex::LowerToVerse(Result.Program, VerseBySymbol);
	Behavior.GeneratedVerseCode = VerseCode;
	Behavior.bHasExecutableProcedure = false;
	Behavior.CompileState = EFlightVerseCompileState::GeneratedOnly;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	Behavior.LastCompileDiagnostics =
		TEXT("Verse VM compile path is not implemented yet; behavior was generated but is not executable.");
	OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"),
		BehaviorID, Behavior.ExecutionRateHz, Behavior.FrameInterval, Behavior.bIsAsync ? TEXT("[Async]") : TEXT(""));
	OutErrors += Behavior.LastCompileDiagnostics + TEXT("\n");
	OutErrors += TEXT("Generated Verse:\n") + VerseCode;
	return false;
#else
	Behavior.LastCompileDiagnostics =
		TEXT("Verse VM is unavailable in this build; behavior was generated but is not executable.");
	OutErrors += FString::Printf(TEXT("Behavior %u: @rate(%.1fHz, %u frames) %s\n"),
		BehaviorID, Behavior.ExecutionRateHz, Behavior.FrameInterval, Behavior.bIsAsync ? TEXT("[Async]") : TEXT(""));
	OutErrors += Behavior.LastCompileDiagnostics + TEXT("\n");
	OutErrors += TEXT("Generated Verse:\n") + VerseCode;
	return false;
#endif
}

void UFlightVerseSubsystem::ExecuteBehavior(uint32 BehaviorID, Flight::Swarm::FDroidState& DroidState)
{
	(void)DroidState;

	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	if (!Behavior)
	{
		return;
	}

	if (!Behavior->bHasExecutableProcedure)
	{
		return;
	}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	/*
	VerseContext.EnterVM([&]()
	{
		// ... Execute Behavior->Procedure ...
	});
	*/
#else
	UE_LOG(LogFlightVerseSubsystem, Warning, TEXT("ExecuteBehavior called but Verse VM is unavailable in this build."));
#endif
}

EFlightVerseCompileState UFlightVerseSubsystem::GetBehaviorCompileState(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? Behavior->CompileState : EFlightVerseCompileState::VmCompileFailed;
}

bool UFlightVerseSubsystem::HasExecutableBehavior(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? Behavior->bHasExecutableProcedure : false;
}

FString UFlightVerseSubsystem::GetBehaviorCompileDiagnostics(uint32 BehaviorID) const
{
	const FVerseBehavior* Behavior = Behaviors.Find(BehaviorID);
	return Behavior ? Behavior->LastCompileDiagnostics : FString();
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
	UE_LOG(LogFlightVerseSubsystem, Verbose, TEXT("UFlightVerseSubsystem: native Verse registration is pending implementation."));
}
