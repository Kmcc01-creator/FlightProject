// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "UI/SwarmOrchestrator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "FlightScriptingLibrary.h"
#include "UI/FlightReflectedUI.h"
#include "UI/FlightVexUI.h"
#include "Vex/FlightVexParser.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Schema/FlightRequirementSchema.h"
#include "Core/FlightHlslReflection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
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
static const FString OrchestratorSchemaEditsRelativeDir = TEXT("Saved/Flight/Schema/ui_edits");
static const FString OrchestratorSchemaEditsHistoryRelativeDir = TEXT("Saved/Flight/Schema/ui_edits/history");
static const FString OrchestratorSchemaEditsJournalRelativePath = TEXT("Saved/Flight/Schema/ui_edits/schema_edits.jl");
static const FString DefaultVexUiScript = TEXT(
	"tab(\"Overview\")\n"
	"text(\"VEX UI Bridge\")\n"
	"section(\"Compile\") {\n"
	"text(@status_text)\n"
	"text(@issue_count)\n"
	"}\n"
	"tab(\"Controls\")\n"
	"section(\"Simulation\") {\n"
	"slider(@spawn_rate, 0, 5000)\n"
	"}\n"
	"section(\"Diagnostics\") {\n"
	"table(@issues, \"line,severity,message\")\n"
	"}\n"
	"tab(\"Schema\")\n"
	"section(\"Manifest\") {\n"
	"schema_table(\"vexSymbolRequirements\")\n"
	"}\n"
	"section(\"Edit\") {\n"
	"schema_form(\"vexSymbolRequirements\", \"0\")\n"
	"}\n"
);

namespace
{
	FString BuildTimestampStamp(const FDateTime& UtcNow)
	{
		return FString::Printf(
			TEXT("%04d%02d%02dT%02d%02d%02d_%03dZ"),
			UtcNow.GetYear(),
			UtcNow.GetMonth(),
			UtcNow.GetDay(),
			UtcNow.GetHour(),
			UtcNow.GetMinute(),
			UtcNow.GetSecond(),
			UtcNow.GetMillisecond());
	}

	FString MakeAbsoluteProjectPath(const FString& RelativePath)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RelativePath);
	}

	bool EnsureParentDirectory(const FString& AbsolutePath)
	{
		const FString DirectoryPath = FPaths::GetPath(AbsolutePath);
		if (DirectoryPath.IsEmpty())
		{
			return true;
		}
		return IFileManager::Get().MakeDirectory(*DirectoryPath, true);
	}

	bool SaveUtf8Text(const FString& AbsolutePath, const FString& Text, FString& OutError, uint32 WriteFlags = 0)
	{
		if (!EnsureParentDirectory(AbsolutePath))
		{
			OutError = FString::Printf(TEXT("failed creating directory for '%s'"), *AbsolutePath);
			return false;
		}

		if (!FFileHelper::SaveStringToFile(
			Text,
			*AbsolutePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(),
			WriteFlags))
		{
			OutError = FString::Printf(TEXT("failed writing file '%s'"), *AbsolutePath);
			return false;
		}
		return true;
	}

	bool SerializeJsonObject(const TSharedRef<FJsonObject>& Object, FString& OutJson, FString& OutError)
	{
		OutJson.Reset();
		const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(Object, JsonWriter))
		{
			OutError = TEXT("failed to serialize json payload");
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> MakeSchemaRowObject(const Flight::VexUI::FVexUiTableRow& Row)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();

		TSharedPtr<FJsonObject> FieldsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Field : Row.Fields)
		{
			FieldsObject->SetStringField(Field.Key, Field.Value);
		}
		Object->SetObjectField(TEXT("fields"), FieldsObject);

		TArray<TSharedPtr<FJsonValue>> Issues;
		Issues.Reserve(Row.ValidationIssues.Num());
		for (const FString& Issue : Row.ValidationIssues)
		{
			Issues.Add(MakeShared<FJsonValueString>(Issue));
		}
		Object->SetArrayField(TEXT("validationIssues"), Issues);
		return Object;
	}

	TSharedRef<FJsonObject> BuildSchemaSnapshotObject(
		const Flight::VexUI::FVexUiEditableCell& Edit,
		const Flight::VexUI::FVexUiSchemaTableView& NextTable,
		const FString& GeneratedAtUtcIso)
	{
		TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetStringField(TEXT("table"), NextTable.Name);
		RootObject->SetStringField(TEXT("keyColumn"), NextTable.KeyColumn);
		RootObject->SetStringField(TEXT("generatedAtUtc"), GeneratedAtUtcIso);

		TSharedPtr<FJsonObject> EditObject = MakeShared<FJsonObject>();
		EditObject->SetStringField(TEXT("tableName"), Edit.TableName);
		EditObject->SetNumberField(TEXT("rowIndex"), Edit.RowIndex);
		EditObject->SetStringField(TEXT("columnName"), Edit.ColumnName);
		EditObject->SetStringField(TEXT("newValue"), Edit.NewValue);
		RootObject->SetObjectField(TEXT("lastEdit"), EditObject);

		TArray<TSharedPtr<FJsonValue>> ColumnArray;
		ColumnArray.Reserve(NextTable.Columns.Num());
		for (const Flight::VexUI::FVexUiSchemaColumn& Column : NextTable.Columns)
		{
			TSharedPtr<FJsonObject> ColumnObject = MakeShared<FJsonObject>();
			ColumnObject->SetStringField(TEXT("name"), Column.Name);
			ColumnObject->SetStringField(TEXT("type"), Column.Type);
			ColumnObject->SetBoolField(TEXT("required"), Column.bRequired);
			ColumnArray.Add(MakeShared<FJsonValueObject>(ColumnObject));
		}
		RootObject->SetArrayField(TEXT("columns"), ColumnArray);

		TArray<TSharedPtr<FJsonValue>> RowArray;
		RowArray.Reserve(NextTable.Rows.Num());
		for (const Flight::VexUI::FVexUiTableRow& Row : NextTable.Rows)
		{
			RowArray.Add(MakeShared<FJsonValueObject>(MakeSchemaRowObject(Row)));
		}
		RootObject->SetArrayField(TEXT("rows"), RowArray);
		return RootObject;
	}

	bool PersistSchemaEditSnapshot(
		const Flight::VexUI::FVexUiEditableCell& Edit,
		const Flight::VexUI::FVexUiSchemaTableView& NextTable,
		FString& OutError)
	{
		const FDateTime UtcNow = FDateTime::UtcNow();
		const FString TimestampStamp = BuildTimestampStamp(UtcNow);
		const FString GeneratedAtUtcIso = UtcNow.ToIso8601();
		const FString SafeTableName = FPaths::MakeValidFileName(Edit.TableName, TCHAR('_'));
		const FString LatestRelativePath = FString::Printf(TEXT("%s/%s.json"), *OrchestratorSchemaEditsRelativeDir, *SafeTableName);
		const FString LatestAbsolutePath = MakeAbsoluteProjectPath(LatestRelativePath);
		const FString HistoryRelativePath = FString::Printf(
			TEXT("%s/%s_%s_%llu.json"),
			*OrchestratorSchemaEditsHistoryRelativeDir,
			*SafeTableName,
			*TimestampStamp,
			static_cast<unsigned long long>(FPlatformTime::Cycles64()));
		const FString HistoryAbsolutePath = MakeAbsoluteProjectPath(HistoryRelativePath);
		const FString JournalAbsolutePath = MakeAbsoluteProjectPath(OrchestratorSchemaEditsJournalRelativePath);

		const TSharedRef<FJsonObject> SnapshotObject = BuildSchemaSnapshotObject(Edit, NextTable, GeneratedAtUtcIso);

		FString SnapshotJson;
		if (!SerializeJsonObject(SnapshotObject, SnapshotJson, OutError))
		{
			return false;
		}

		if (!SaveUtf8Text(LatestAbsolutePath, SnapshotJson, OutError))
		{
			return false;
		}
		if (!SaveUtf8Text(HistoryAbsolutePath, SnapshotJson, OutError))
		{
			return false;
		}

		TSharedRef<FJsonObject> JournalEntry = MakeShared<FJsonObject>();
		JournalEntry->SetStringField(TEXT("generatedAtUtc"), GeneratedAtUtcIso);
		JournalEntry->SetStringField(TEXT("table"), Edit.TableName);
		JournalEntry->SetNumberField(TEXT("rowIndex"), Edit.RowIndex);
		JournalEntry->SetStringField(TEXT("column"), Edit.ColumnName);
		JournalEntry->SetStringField(TEXT("newValue"), Edit.NewValue);
		JournalEntry->SetStringField(TEXT("latestPath"), LatestRelativePath);
		JournalEntry->SetStringField(TEXT("historyPath"), HistoryRelativePath);
		JournalEntry->SetNumberField(TEXT("rowCount"), NextTable.Rows.Num());

		FString JournalLine;
		if (!SerializeJsonObject(JournalEntry, JournalLine, OutError))
		{
			return false;
		}
		JournalLine += LINE_TERMINATOR;
		if (!SaveUtf8Text(JournalAbsolutePath, JournalLine, OutError, FILEWRITE_Append))
		{
			return false;
		}

		const FString ExportedManifest = UFlightScriptingLibrary::ExportRequirementManifest();
		if (ExportedManifest.IsEmpty())
		{
			UE_LOG(
				LogSwarmOrchestrator,
				Warning,
				TEXT("Schema snapshot persisted (latest='%s', history='%s'), but manifest export failed."),
				*LatestAbsolutePath,
				*HistoryAbsolutePath);
		}

		UE_LOG(
			LogSwarmOrchestrator,
			Log,
			TEXT("Persisted schema edit snapshots latest='%s' history='%s' journal='%s' (table=%s row=%d column=%s)."),
			*LatestAbsolutePath,
			*HistoryAbsolutePath,
			*JournalAbsolutePath,
			*Edit.TableName,
			Edit.RowIndex,
			*Edit.ColumnName);
		return true;
	}
}

void SSwarmOrchestrator::Construct(const FArguments& InArgs)
{
	VexUiStore = MakeShared<Flight::VexUI::FVexUiStore>();
	VexUiStore->Text(TEXT("@status_text"), TEXT("Status: Idle"));
	VexUiStore->Text(TEXT("@issue_count"), TEXT("Issues: 0"));
	VexUiStore->Number(TEXT("@spawn_rate"), SimParams.AvoidanceStrength);
	VexUiStore->Table(TEXT("@issues"));
	VexUiStore->SetSchemaCommitHook(
		[](const Flight::VexUI::FVexUiEditableCell& Edit, const Flight::VexUI::FVexUiSchemaTableView& NextTable, FString& OutError)
		{
			return PersistSchemaEditSnapshot(Edit, NextTable, OutError);
		});

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
				+ SVerticalBox::Slot().AutoHeight().Padding(5)
				[
					SNew(STextBlock).Text(LOCTEXT("VexUiTitle", "VEX UI (Reactive Scaffold)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(5)
				[
					SAssignNew(VexUiPanel, Flight::VexUI::SVexPanel)
					.Script(DefaultVexUiScript)
					.Store(VexUiStore)
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
		RefreshVexUiStore(ParseResult);
		
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

void SSwarmOrchestrator::RefreshVexUiStore(const Flight::Vex::FVexParseResult& ParseResult)
{
	if (!VexUiStore.IsValid())
	{
		return;
	}

	const FString StatusLine = ParseResult.bSuccess
		? TEXT("Status: Compile OK")
		: TEXT("Status: Compile Failed");
	VexUiStore->Text(TEXT("@status_text"), TEXT("Status: Idle")).Set(StatusLine);

	const int32 TotalIssues = ParseResult.Issues.Num() + ParseResult.UnknownSymbols.Num();
	VexUiStore->Text(TEXT("@issue_count"), TEXT("Issues: 0"))
		.Set(FString::Printf(TEXT("Issues: %d"), TotalIssues));

	VexUiStore->Number(TEXT("@spawn_rate"), SimParams.AvoidanceStrength).Set(SimParams.AvoidanceStrength);

	TArray<Flight::VexUI::FVexUiTableRow> IssueRows;
	IssueRows.Reserve(ParseResult.Issues.Num() + ParseResult.UnknownSymbols.Num());
	for (const Flight::Vex::FVexIssue& Issue : ParseResult.Issues)
	{
		Flight::VexUI::FVexUiTableRow Row;
		Row.Fields.Add(TEXT("line"), FString::FromInt(Issue.Line));
		Row.Fields.Add(TEXT("severity"), Issue.Severity == Flight::Vex::EVexIssueSeverity::Error ? TEXT("error") : TEXT("warning"));
		Row.Fields.Add(TEXT("message"), Issue.Message);
		IssueRows.Add(MoveTemp(Row));
	}

	for (const FString& UnknownSymbol : ParseResult.UnknownSymbols)
	{
		Flight::VexUI::FVexUiTableRow Row;
		Row.Fields.Add(TEXT("line"), TEXT("-"));
		Row.Fields.Add(TEXT("severity"), TEXT("warning"));
		Row.Fields.Add(TEXT("message"), FString::Printf(TEXT("Unknown symbol %s"), *UnknownSymbol));
		IssueRows.Add(MoveTemp(Row));
	}

	VexUiStore->Table(TEXT("@issues")).Set(IssueRows);
}

#undef LOCTEXT_NAMESPACE
