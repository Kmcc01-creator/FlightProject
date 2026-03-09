// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "FlightScriptingLibrary.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Schema/FlightRequirementSchema.h"
#include "Vex/FlightVexParser.h"

namespace
{
	UWorld* FindAutomationWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
			{
				return Context.World();
			}
		}

		return nullptr;
	}

	FString NormalizeDeclType(const FString& TypeName)
	{
		if (TypeName == TEXT("float") || TypeName == TEXT("float2") || TypeName == TEXT("float3")
			|| TypeName == TEXT("float4") || TypeName == TEXT("int") || TypeName == TEXT("bool"))
		{
			return TypeName;
		}
		return TEXT("float");
	}

	FString BuildReadProbe(const FString& TypeName, const FString& SymbolName, const int32 ProbeIndex)
	{
		return FString::Printf(TEXT("%s _probe_%d = %s;"), *NormalizeDeclType(TypeName), ProbeIndex, *SymbolName);
	}

	bool HasIssueContaining(const TArray<Flight::Vex::FVexIssue>& Issues, const FString& Needle)
	{
		for (const Flight::Vex::FVexIssue& Issue : Issues)
		{
			if (Issue.Message.Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}

	FString BuildRequiredCoverageBody(const TArray<Flight::Vex::FVexSymbolDefinition>& Definitions)
	{
		FString Body;
		int32 ProbeIndex = 0;
		for (const Flight::Vex::FVexSymbolDefinition& Def : Definitions)
		{
			if (!Def.bRequired)
			{
				continue;
			}

			if (Def.bWritable)
			{
				Body += FString::Printf(TEXT("%s = %s;\n"), *Def.SymbolName, *Def.SymbolName);
			}
			else
			{
				Body += BuildReadProbe(Def.ValueType, Def.SymbolName, ProbeIndex++) + TEXT("\n");
			}
		}
		return Body;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FFlightVexVerticalSliceComplexTest,
	"FlightProject.Integration.Vex.VerticalSlice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightVexVerticalSliceComplexTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();

	for (const FFlightVexSymbolRow& Row : Manifest.VexSymbolRequirements)
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("Symbol.Contract.%s"), *Row.SymbolName));
		OutTestCommands.Add(FString::Printf(TEXT("CONTRACT|%s"), *Row.SymbolName));

		if (Row.Residency != EFlightVexSymbolResidency::Shared)
		{
			OutBeautifiedNames.Add(FString::Printf(TEXT("Symbol.InvalidResidency.%s"), *Row.SymbolName));
			OutTestCommands.Add(FString::Printf(TEXT("NEG_RESIDENCY|%s"), *Row.SymbolName));
		}

		if (Row.Affinity == EFlightVexSymbolAffinity::GameThread)
		{
			OutBeautifiedNames.Add(FString::Printf(TEXT("Symbol.InvalidAffinity.%s"), *Row.SymbolName));
			OutTestCommands.Add(FString::Printf(TEXT("NEG_AFFINITY|%s"), *Row.SymbolName));
		}
	}
}

bool FFlightVexVerticalSliceComplexTest::RunTest(const FString& Parameters)
{
	TArray<FString> Parts;
	Parameters.ParseIntoArray(Parts, TEXT("|"), true);
	if (Parts.Num() != 2)
	{
		AddError(FString::Printf(TEXT("Malformed vertical-slice test command: '%s'"), *Parameters));
		return false;
	}

	const FString TestType = Parts[0];
	const FString SymbolName = Parts[1];

	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();
	const TArray<Flight::Vex::FVexSymbolDefinition> Definitions = Flight::Vex::BuildSymbolDefinitionsFromManifest(Manifest);

	const FFlightVexSymbolRow* Row = nullptr;
	for (const FFlightVexSymbolRow& Candidate : Manifest.VexSymbolRequirements)
	{
		if (Candidate.SymbolName == SymbolName)
		{
			Row = &Candidate;
			break;
		}
	}

	if (!Row)
	{
		AddError(FString::Printf(TEXT("Missing manifest row for symbol '%s'"), *SymbolName));
		return false;
	}

	const Flight::Vex::FVexSymbolDefinition* Def = nullptr;
	for (const Flight::Vex::FVexSymbolDefinition& Candidate : Definitions)
	{
		if (Candidate.SymbolName == SymbolName)
		{
			Def = &Candidate;
			break;
		}
	}

	if (!Def)
	{
		AddError(FString::Printf(TEXT("Missing parser definition for symbol '%s'"), *SymbolName));
		return false;
	}

	if (TestType == TEXT("CONTRACT"))
	{
		const FString ValidContext = Row->Residency == EFlightVexSymbolResidency::GpuOnly
			? TEXT("@gpu")
			: TEXT("@cpu");
		const FString ValidSource = FString::Printf(
			TEXT("%s { %s }\n"),
			*ValidContext,
			*BuildReadProbe(Def->ValueType, SymbolName, 0));

		const Flight::Vex::FVexParseResult ParseResult = Flight::Vex::ParseAndValidate(ValidSource, Definitions, false);
		TestTrue(FString::Printf(TEXT("Valid contract source should parse for %s"), *SymbolName), ParseResult.bSuccess);

		TMap<FString, FString> HlslBySymbol;
		TMap<FString, FString> VerseBySymbol;
		for (const FFlightVexSymbolRow& VexRow : Manifest.VexSymbolRequirements)
		{
			HlslBySymbol.Add(VexRow.SymbolName, VexRow.HlslIdentifier);
			VerseBySymbol.Add(VexRow.SymbolName, VexRow.VerseIdentifier);
		}

		const FString HlslOutput = Flight::Vex::LowerToHLSL(ParseResult.Program, HlslBySymbol);
		const FString VerseOutput = Flight::Vex::LowerToVerse(ParseResult.Program, VerseBySymbol);
		if (!Row->HlslIdentifier.IsEmpty())
		{
			TestTrue(FString::Printf(TEXT("HLSL lowering should contain mapped identifier for %s"), *SymbolName),
				HlslOutput.Contains(Row->HlslIdentifier));
		}
		if (!Row->VerseIdentifier.IsEmpty())
		{
			TestTrue(FString::Printf(TEXT("Verse lowering should contain mapped identifier for %s"), *SymbolName),
				VerseOutput.Contains(Row->VerseIdentifier));
		}

		UWorld* World = FindAutomationWorld();
		if (!World)
		{
			AddError(TEXT("No valid world context available for vertical-slice compile check"));
			return false;
		}

		const int32 BehaviorId = static_cast<int32>(FCrc::StrCrc32(*FString::Printf(TEXT("VerticalSlice.%s"), *SymbolName)));
		const FString CompileSource = FString::Printf(
			TEXT("@rate(20Hz) {\n%s%s\n}\n"),
			*BuildRequiredCoverageBody(Definitions),
			*BuildReadProbe(Def->ValueType, SymbolName, 777));

		FString OutErrors;
		const bool bCompiled = UFlightScriptingLibrary::CompileVex(World, BehaviorId, CompileSource, OutErrors);
		TestTrue(TEXT("Compile should produce executable native fallback behavior"), bCompiled);
		TestEqual(TEXT("Compile state should be VmCompiled when native fallback is active"), UFlightScriptingLibrary::GetBehaviorCompileState(World, BehaviorId), EFlightVerseCompileState::VmCompiled);
		TestTrue(TEXT("Behavior should be executable via fallback runtime"), UFlightScriptingLibrary::IsBehaviorExecutable(World, BehaviorId));
		const FString CompileDiagnostics = UFlightScriptingLibrary::GetBehaviorCompileDiagnostics(World, BehaviorId);
		TestTrue(TEXT("Diagnostics should report executable runtime path"),
			CompileDiagnostics.Contains(TEXT("native fallback")) || CompileDiagnostics.Contains(TEXT("Verse VM procedure")));

		const TArray<int32> RegisteredIds = UFlightScriptingLibrary::GetRegisteredBehaviorIDs(World);
		TestTrue(TEXT("Behavior metadata should be registered after compile"), RegisteredIds.Contains(BehaviorId));
		TestEqual(TEXT("Rate metadata should be registered"), UFlightScriptingLibrary::GetBehaviorExecutionRate(World, BehaviorId), 20.0f);
		TestEqual(TEXT("Frame interval metadata should be 0 for Hz-based rate"), UFlightScriptingLibrary::GetBehaviorFrameInterval(World, BehaviorId), 0);

		return ParseResult.bSuccess;
	}

	if (TestType == TEXT("NEG_RESIDENCY"))
	{
		const FString InvalidContext = Row->Residency == EFlightVexSymbolResidency::GpuOnly
			? TEXT("@cpu")
			: TEXT("@gpu");
		const FString InvalidSource = FString::Printf(
			TEXT("%s { %s }\n"),
			*InvalidContext,
			*BuildReadProbe(Def->ValueType, SymbolName, 1));
		const Flight::Vex::FVexParseResult ParseResult = Flight::Vex::ParseAndValidate(InvalidSource, Definitions, false);
		TestTrue(FString::Printf(TEXT("Should detect invalid residency for %s"), *SymbolName),
			HasIssueContaining(ParseResult.Issues, TEXT("cannot be used in")));
		return true;
	}

	if (TestType == TEXT("NEG_AFFINITY"))
	{
		const FString InvalidSource = FString::Printf(
			TEXT("@cpu @job { %s }\n"),
			*BuildReadProbe(Def->ValueType, SymbolName, 2));
		const Flight::Vex::FVexParseResult ParseResult = Flight::Vex::ParseAndValidate(InvalidSource, Definitions, false);
		TestTrue(FString::Printf(TEXT("Should detect game-thread affinity misuse for %s"), *SymbolName),
			HasIssueContaining(ParseResult.Issues, TEXT("Game Thread")));
		return true;
	}

	AddError(FString::Printf(TEXT("Unknown vertical-slice test type '%s'"), *TestType));
	return false;
}
