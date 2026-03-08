// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "UI/SwarmOrchestrator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "UI/FlightReflectedUI.h"
#include "Vex/FlightVexParser.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Schema/FlightRequirementSchema.h"
#include "Core/FlightHlslReflection.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "SwarmOrchestrator"

DEFINE_LOG_CATEGORY_STATIC(LogSwarmOrchestrator, Log, All);

static const FString DefaultScript = TEXT(
	"// Simple Swarm Behavior\n"
	"float3 toPlayer = @position - {0, 0, 0};\n"
	"float dist = length(toPlayer);\n"
	"if (dist < 1000.0) {\n"
	"    @velocity += normalize(toPlayer) * 10.0;\n"
	"}\n"
	"\n"
	"// Sample Light field and modulate color\n"
	"float3 irradiance = sample_lattice(@position);\n"
	"@shield = length(irradiance);\n"
    "\n"
    "// Inject into cloud\n"
    "inject_cloud(@position, 1.0);\n"
);

static const FString OrchestratorInjectedRelativePath = TEXT("Shaders/Private/Generated/OrchestratorInjected.ush");
static const FString OrchestratorReflectedTypesRelativePath = TEXT("Shaders/Private/Generated/OrchestratorReflectedTypes.ush");

void SSwarmOrchestrator::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		
		// Left Panel: The Code Editor
		+ SSplitter::Slot()
		.Value(0.4f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(5)
			[
				SNew(STextBlock).Text(LOCTEXT("VexEditorTitle", "VEX Source (SCSL Paradigm)"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(5)
			[
				SAssignNew(CodeEditor, SMultiLineEditableTextBox)
				.Text(FText::FromString(DefaultScript))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(5)
			[
				SNew(SButton).Text(LOCTEXT("CompileBtn", "Compile & Inject SCSL"))
				.OnClicked(this, &SSwarmOrchestrator::OnCompileClicked)
			]
		]

		// Center Panel: The Previews (HLSL vs Verse)
		+ SSplitter::Slot()
		.Value(0.4f)
		[
			SNew(SVerticalBox)
			
			// HLSL Preview (GPU)
			+ SVerticalBox::Slot().FillHeight(0.5f).Padding(5)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[ SNew(STextBlock).Text(LOCTEXT("HLSLTitle", "Generated HLSL (GPU Simulation Path)")) ]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(HLSLPreview, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				]
			]

			// Verse Preview (Logic)
			+ SVerticalBox::Slot().FillHeight(0.5f).Padding(5)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[ SNew(STextBlock).Text(LOCTEXT("VerseTitle", "Generated Verse (Gameplay Logic Path)")) ]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(VersePreview, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				]
			]
		]

		// Right Panel: Parameters
		+ SSplitter::Slot()
		.Value(0.2f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(5)
				[
					SNew(STextBlock).Text(LOCTEXT("SimParamsTitle", "Simulation Parameters"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(SimParamsPanel, SReflectedStatePanel, SimParams)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(5)
				[
					SNew(STextBlock).Text(LOCTEXT("LatticeParamsTitle", "Light Lattice (SCSL)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(LatticeParamsPanel, SReflectedStatePanel, LatticeParams)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(5)
				[
					SNew(STextBlock).Text(LOCTEXT("CloudParamsTitle", "Volumetric Cloud (SCSL)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(CloudParamsPanel, SReflectedStatePanel, CloudParams)
				]
			]
		]
	];

	// Initial parse
	OnCompileClicked();
}

void SSwarmOrchestrator::BuildVexReplacementMaps(
	TMap<FString, FString>& OutHlslMap,
	TMap<FString, FString>& OutVerseMap,
	TArray<Flight::Vex::FVexSymbolDefinition>& OutDefinitions)
{
	using namespace Flight::Schema;
	FManifestData Manifest = Flight::Schema::BuildManifestData();

	for (const FFlightVexSymbolRow& Row : Manifest.VexSymbolRequirements)
	{
		OutHlslMap.Add(Row.SymbolName, Row.HlslIdentifier);
		OutVerseMap.Add(Row.SymbolName, Row.VerseIdentifier);
	}

	OutDefinitions = Flight::Vex::BuildSymbolDefinitionsFromManifest(Manifest);
}

FReply SSwarmOrchestrator::OnCompileClicked()
{
	if (CodeEditor.IsValid())
	{
		FString Source = CodeEditor->GetText().ToString();

		TMap<FString, FString> HlslBySymbol;
		TMap<FString, FString> VerseBySymbol;
		TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions;
		BuildVexReplacementMaps(HlslBySymbol, VerseBySymbol, SymbolDefinitions);

		const Flight::Vex::FVexParseResult ParseResult = Flight::Vex::ParseAndValidate(Source, SymbolDefinitions, false);
		
		FString GeneratedHLSL;
		FString GeneratedVerse;

		if (ParseResult.bSuccess)
		{
			GeneratedHLSL = Flight::Vex::LowerToHLSL(ParseResult.Program, HlslBySymbol);
			GeneratedVerse = Flight::Vex::LowerToVerse(ParseResult.Program, VerseBySymbol);
		}
		else
		{
			// Fallback to source on error so user can see it
			GeneratedHLSL = Source;
			GeneratedVerse = Source;
		}

		if (ParseResult.Issues.Num() > 0)
		{
			for (const Flight::Vex::FVexIssue& Issue : ParseResult.Issues)
			{
				if (Issue.Severity == Flight::Vex::EVexIssueSeverity::Error)
				{
					UE_LOG(
						LogSwarmOrchestrator,
						Error,
						TEXT("Swarm Orchestrator VEX [error L%d:C%d] %s"),
						Issue.Line,
						Issue.Column,
						*Issue.Message);
				}
				else
				{
					UE_LOG(
						LogSwarmOrchestrator,
						Warning,
						TEXT("Swarm Orchestrator VEX [warning L%d:C%d] %s"),
						Issue.Line,
						Issue.Column,
						*Issue.Message);
				}
			}

			const FString FormattedIssues = Flight::Vex::FormatIssues(ParseResult.Issues);
			if (!FormattedIssues.IsEmpty())
			{
				const FString IssueBanner = FString::Printf(TEXT("// VEX Diagnostics:\n// %s\n"), *FormattedIssues.Replace(TEXT("\n"), TEXT("\n// ")));
				GeneratedHLSL = IssueBanner + GeneratedHLSL;
				GeneratedVerse = IssueBanner + GeneratedVerse;
			}
		}

		if (ParseResult.UnknownSymbols.Num() > 0)
		{
			const FString UnknownList = FString::Join(ParseResult.UnknownSymbols, TEXT(", "));
			UE_LOG(
				LogSwarmOrchestrator,
				Warning,
				TEXT("Swarm Orchestrator: Unknown VEX symbols encountered: %s"),
				*UnknownList);
			const FString WarningLine = FString::Printf(TEXT("// WARNING: Unknown VEX symbols: %s\n"), *UnknownList);
			GeneratedHLSL = WarningLine + GeneratedHLSL;
			GeneratedVerse = WarningLine + GeneratedVerse;
		}

		HLSLPreview->SetText(FText::FromString(GeneratedHLSL));
		VersePreview->SetText(FText::FromString(GeneratedVerse));

		if (!ParseResult.bSuccess)
		{
			UE_LOG(LogSwarmOrchestrator, Warning, TEXT("Swarm Orchestrator: aborting shader injection due to VEX parse errors."));
			return FReply::Handled();
		}

		uint32 ExpectedLayoutHash = 0;
		const FString ReflectedTypesSource = BuildReflectedTypeInclude(ExpectedLayoutHash);
		if (!ValidateReflectedLayoutContract(ReflectedTypesSource, ExpectedLayoutHash))
		{
			GeneratedHLSL = TEXT("// ERROR: Reflected layout contract validation failed.\n") + GeneratedHLSL;
			HLSLPreview->SetText(FText::FromString(GeneratedHLSL));
			return FReply::Handled();
		}

		const FString InjectedShaderPath = FPaths::Combine(FPaths::ProjectDir(), OrchestratorInjectedRelativePath);
		const FString ReflectedTypesPath = FPaths::Combine(FPaths::ProjectDir(), OrchestratorReflectedTypesRelativePath);
		const bool bWroteInjected = FFileHelper::SaveStringToFile(GeneratedHLSL, *InjectedShaderPath);
		const bool bWroteTypes = FFileHelper::SaveStringToFile(ReflectedTypesSource, *ReflectedTypesPath);
		if (!bWroteInjected || !bWroteTypes)
		{
			UE_LOG(LogSwarmOrchestrator, Error, TEXT("Swarm Orchestrator: failed writing generated shader include files."));
			return FReply::Handled();
		}

		// Trigger Unreal shader recompile
		if (GEngine)
		{
			// Enable shader development mode if not already on
			GEngine->Exec(nullptr, TEXT("r.ShaderDevelopmentMode 1"));
			// Recompile changed shaders (this will pick up our .ush change)
			GEngine->Exec(nullptr, TEXT("recompileshaders changed"));
			
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("Swarm Orchestrator: HLSL Injected and Recompile Triggered"));
		}
	}
	return FReply::Handled();
}

FString SSwarmOrchestrator::BuildReflectedTypeInclude(uint32& OutLayoutHash)
{
	FString Source = TEXT("// Generated by SwarmOrchestrator\n\n");
	Source += ::Flight::Reflection::HLSL::GenerateStructHLSL<Flight::Swarm::FDroidState>(TEXT("FDroidState"));
	Source += TEXT("\n");
	
	OutLayoutHash = ::Flight::Reflection::HLSL::ComputeLayoutHash<Flight::Swarm::FDroidState>();
	Source += FString::Printf(TEXT("#define FDROIDSTATE_LAYOUT_HASH 0x%08X\n"), OutLayoutHash);
	
	return Source;
}

bool SSwarmOrchestrator::ValidateReflectedLayoutContract(const FString& InSource, uint32 InExpectedHash)
{
	uint32 ObservedHash = 0;
	if (::Flight::Reflection::HLSL::ExtractLayoutHashFromContract(InSource, TEXT("FDROIDSTATE_LAYOUT_HASH"), ObservedHash))
	{
		return ObservedHash == InExpectedHash;
	}
	return false;
}

FString SSwarmOrchestrator::ParseVexToHLSL(const FString& InVexCode)
{
	TMap<FString, FString> HlslBySymbol;
	TMap<FString, FString> VerseBySymbol;
	TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions;
	BuildVexReplacementMaps(HlslBySymbol, VerseBySymbol, SymbolDefinitions);

	const Flight::Vex::FVexParseResult ParseResult = Flight::Vex::ParseAndValidate(InVexCode, SymbolDefinitions, false);
	if (ParseResult.bSuccess)
	{
		return Flight::Vex::LowerToHLSL(ParseResult.Program, HlslBySymbol);
	}
	return InVexCode;
}

FString SSwarmOrchestrator::ParseVexToVerse(const FString& InVexCode)
{
	TMap<FString, FString> HlslBySymbol;
	TMap<FString, FString> VerseBySymbol;
	TArray<Flight::Vex::FVexSymbolDefinition> SymbolDefinitions;
	BuildVexReplacementMaps(HlslBySymbol, VerseBySymbol, SymbolDefinitions);

	const Flight::Vex::FVexParseResult ParseResult = Flight::Vex::ParseAndValidate(InVexCode, SymbolDefinitions, false);
	if (ParseResult.bSuccess)
	{
		return Flight::Vex::LowerToVerse(ParseResult.Program, VerseBySymbol);
	}
	return InVexCode;
}

#undef LOCTEXT_NAMESPACE
