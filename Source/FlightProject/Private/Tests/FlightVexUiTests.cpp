// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UI/FlightVexUI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexUiCompileTest,
	"FlightProject.Vex.UI.Compile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexUiCompileTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString Script =
		TEXT("text(\"Swarm Dashboard\")\n")
		TEXT("text(@agent_count)\n")
		TEXT("tab(\"Metrics\")\n")
		TEXT("section(\"Rates\") {\n")
		TEXT("slider(@spawn_rate, 0, 500)\n")
		TEXT("}\n")
		TEXT("table(@rows_csv, \"id,speed,status\")\n")
		TEXT("schema_table(\"vexSymbolRequirements\")\n")
		TEXT("schema_form(\"vexSymbolRequirements\", \"0\")\n");

	const Flight::VexUI::FVexUiCompileResult Result = Flight::VexUI::FVexUiCompiler::Compile(Script);
	TestTrue(TEXT("VEX UI script should compile"), Result.bSuccess);
	TestEqual(TEXT("Expected 6 UI nodes"), Result.Nodes.Num(), 6);
	if (Result.Nodes.Num() == 6)
	{
		TestEqual(TEXT("Node 0 should be static text"), Result.Nodes[0].Type, Flight::VexUI::EVexUiNodeType::TextStatic);
		TestEqual(TEXT("Node 1 should be symbol text"), Result.Nodes[1].Type, Flight::VexUI::EVexUiNodeType::TextSymbol);
		TestEqual(TEXT("Node 2 should be slider"), Result.Nodes[2].Type, Flight::VexUI::EVexUiNodeType::Slider);
		TestEqual(TEXT("Node 3 should be table"), Result.Nodes[3].Type, Flight::VexUI::EVexUiNodeType::TableSymbol);
		TestEqual(TEXT("Node 4 should be schema table"), Result.Nodes[4].Type, Flight::VexUI::EVexUiNodeType::SchemaTable);
		TestEqual(TEXT("Node 5 should be schema form"), Result.Nodes[5].Type, Flight::VexUI::EVexUiNodeType::SchemaForm);
		TestEqual(TEXT("Node 2 should be assigned to Metrics tab"), Result.Nodes[2].Tab, FString(TEXT("Metrics")));
		TestEqual(TEXT("Node 3 should be assigned to Metrics tab"), Result.Nodes[3].Tab, FString(TEXT("Metrics")));
		TestEqual(TEXT("Node 4 should be assigned to Metrics tab"), Result.Nodes[4].Tab, FString(TEXT("Metrics")));
		TestEqual(TEXT("Node 5 should be assigned to Metrics tab"), Result.Nodes[5].Tab, FString(TEXT("Metrics")));
		TestEqual(TEXT("Node 2 should be assigned to section path"), Result.Nodes[2].SectionPath, FString(TEXT("Rates")));
		TestEqual(TEXT("Node 3 should have three columns"), Result.Nodes[3].Columns.Num(), 3);
		TestTrue(TEXT("Schema table should auto-populate columns"), Result.Nodes[4].Columns.Num() > 0);
		TestTrue(TEXT("Schema table should auto-populate rows"), Result.Nodes[4].StaticRows.Num() > 0);
		TestTrue(TEXT("Schema form should capture one row"), Result.Nodes[5].StaticRows.Num() == 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexUiIncludeComposeTest,
	"FlightProject.Vex.UI.Compose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexUiIncludeComposeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString RootScript =
		TEXT("tab(\"Overview\")\n")
		TEXT("text(\"Root\")\n")
		TEXT("section(\"Top\") {\n")
		TEXT("include(\"shared_metrics\")\n")
		TEXT("}\n");

	TMap<FString, FString> IncludeLibrary;
	IncludeLibrary.Add(
		TEXT("shared_metrics"),
		TEXT("tab(\"Metrics\")\n")
		TEXT("text(@agent_count)\n")
		TEXT("slider(@spawn_rate, 0, 500)\n"));

	const Flight::VexUI::FVexUiCompileResult Result = Flight::VexUI::FVexUiCompiler::CompileWithIncludes(RootScript, IncludeLibrary);
	TestTrue(TEXT("Composed script should compile"), Result.bSuccess);
	TestEqual(TEXT("Composed script should produce 3 UI nodes"), Result.Nodes.Num(), 3);
	if (Result.Nodes.Num() == 3)
	{
		TestEqual(TEXT("Root text should stay in Overview tab"), Result.Nodes[0].Tab, FString(TEXT("Overview")));
		TestEqual(TEXT("Included text should be in Metrics tab"), Result.Nodes[1].Tab, FString(TEXT("Metrics")));
		TestEqual(TEXT("Included slider should be in Metrics tab"), Result.Nodes[2].Tab, FString(TEXT("Metrics")));
		TestEqual(TEXT("Included nodes should inherit open section context"), Result.Nodes[1].SectionPath, FString(TEXT("Top")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexUiStoreTest,
	"FlightProject.Vex.UI.Store",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexUiStoreTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	Flight::VexUI::FVexUiStore Store;
	Flight::VexUI::FVexUiStore::FNumberSignal& SpawnRate = Store.Number(TEXT("@spawn_rate"), 25.0f);
	Flight::VexUI::FVexUiStore::FTextSignal& StatusText = Store.Text(TEXT("@status_text"), TEXT("Idle"));
	Flight::VexUI::FVexUiStore::FBoolSignal& AlertsEnabled = Store.Bool(TEXT("@alerts"), false);
	Flight::VexUI::FVexUiStore::FTableSignal& TableRows = Store.Table(TEXT("@rows"));

	bool bSawSpawnRateUpdate = false;
	float ObservedSpawnRate = 0.0f;
	const auto SpawnRateSub = SpawnRate.SubscribeLambda([&](const float&, const float NewValue)
	{
		bSawSpawnRateUpdate = true;
		ObservedSpawnRate = NewValue;
	});

	SpawnRate.Set(120.0f);
	StatusText.Set(TEXT("Live"));
	AlertsEnabled.Set(true);
	TArray<Flight::VexUI::FVexUiTableRow> Rows;
	{
		Flight::VexUI::FVexUiTableRow Row;
		Row.Fields.Add(TEXT("id"), TEXT("drone_1"));
		Row.Fields.Add(TEXT("status"), TEXT("active"));
		Rows.Add(Row);
	}
	TableRows.Set(Rows);

	Flight::VexUI::FVexUiStore::FSchemaTableSignal& SchemaTable = Store.SchemaTable(TEXT("vexSymbolRequirements"));
	TestTrue(TEXT("Schema table should be loaded"), SchemaTable.Get().Rows.Num() > 0);
	bool bCommitHookCalled = false;
	Store.SetSchemaCommitHook([&bCommitHookCalled](const Flight::VexUI::FVexUiEditableCell&, const Flight::VexUI::FVexUiSchemaTableView&, FString&)
	{
		bCommitHookCalled = true;
		return true;
	});

	FString EditError;
	Flight::VexUI::FVexUiEditableCell ValidEdit;
	ValidEdit.TableName = TEXT("vexSymbolRequirements");
	ValidEdit.RowIndex = 0;
	ValidEdit.ColumnName = TEXT("symbolName");
	ValidEdit.NewValue = TEXT("@status");
	const bool bValidEditApplied = Store.TryApplySchemaEdit(ValidEdit, EditError);

	Flight::VexUI::FVexUiEditableCell InvalidEdit;
	InvalidEdit.TableName = TEXT("vexSymbolRequirements");
	InvalidEdit.RowIndex = 0;
	InvalidEdit.ColumnName = TEXT("writable");
	InvalidEdit.NewValue = TEXT("not_bool");
	const bool bInvalidEditApplied = Store.TryApplySchemaEdit(InvalidEdit, EditError);

	bool bRejectHookCalled = false;
	Store.SetSchemaCommitHook([&bRejectHookCalled](const Flight::VexUI::FVexUiEditableCell&, const Flight::VexUI::FVexUiSchemaTableView&, FString& OutError)
	{
		bRejectHookCalled = true;
		OutError = TEXT("rejected by test hook");
		return false;
	});
	Flight::VexUI::FVexUiEditableCell RejectedEdit;
	RejectedEdit.TableName = TEXT("vexSymbolRequirements");
	RejectedEdit.RowIndex = 0;
	RejectedEdit.ColumnName = TEXT("symbolName");
	RejectedEdit.NewValue = TEXT("@velocity");
	const bool bRejectedByHook = Store.TryApplySchemaEdit(RejectedEdit, EditError);

	TestTrue(TEXT("Spawn-rate signal should notify subscribers"), bSawSpawnRateUpdate);
	TestEqual(TEXT("Spawn-rate value should update"), ObservedSpawnRate, 120.0f);
	TestEqual(TEXT("Text signal should update"), StatusText.Get(), FString(TEXT("Live")));
	TestTrue(TEXT("Bool signal should update"), AlertsEnabled.Get());
	TestEqual(TEXT("Table signal should update"), TableRows.Get().Num(), 1);
	TestEqual(TEXT("Table row field should exist"), TableRows.Get()[0].Fields.FindRef(TEXT("id")), FString(TEXT("drone_1")));
	TestTrue(TEXT("Valid schema edit should apply"), bValidEditApplied);
	TestFalse(TEXT("Invalid schema edit should be rejected"), bInvalidEditApplied);
	TestTrue(TEXT("Commit hook should be invoked for successful edit"), bCommitHookCalled);
	TestTrue(TEXT("Reject hook should be invoked"), bRejectHookCalled);
	TestFalse(TEXT("Hook-rejected edit should fail"), bRejectedByHook);

	SpawnRate.Unsubscribe(SpawnRateSub);
	return true;
}
