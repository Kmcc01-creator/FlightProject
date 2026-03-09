// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReactive.h"
#include "UI/FlightReactiveSlate.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

namespace Flight::VexUI
{
enum class EVexUiNodeType : uint8
{
	TextStatic,
	TextSymbol,
	Slider,
	TableSymbol,
	SchemaTable,
	SchemaForm
};

struct FLIGHTPROJECT_API FVexUiTableRow
{
	TMap<FString, FString> Fields;
	TArray<FString> ValidationIssues;

	bool operator==(const FVexUiTableRow& Other) const
	{
		return Fields.OrderIndependentCompareEqual(Other.Fields) && ValidationIssues == Other.ValidationIssues;
	}
};

struct FLIGHTPROJECT_API FVexUiSchemaColumn
{
	FString Name;
	FString Type;
	bool bRequired = false;

	bool operator==(const FVexUiSchemaColumn& Other) const
	{
		return Name == Other.Name && Type == Other.Type && bRequired == Other.bRequired;
	}
};

struct FLIGHTPROJECT_API FVexUiSchemaTableView
{
	FString Name;
	FString KeyColumn;
	TArray<FVexUiSchemaColumn> Columns;
	TArray<FVexUiTableRow> Rows;

	bool operator==(const FVexUiSchemaTableView& Other) const
	{
		return Name == Other.Name
			&& KeyColumn == Other.KeyColumn
			&& Columns == Other.Columns
			&& Rows == Other.Rows;
	}
};

struct FLIGHTPROJECT_API FVexUiEditableCell
{
	FString TableName;
	int32 RowIndex = INDEX_NONE;
	FString ColumnName;
	FString NewValue;
};

class FLIGHTPROJECT_API FVexUiSchemaViewModel
{
public:
	static bool TryBuildTable(const FString& TableName, FVexUiSchemaTableView& OutTable, FString& OutError);
	static bool ResolveRowIndex(const FVexUiSchemaTableView& Table, const FString& RowKey, int32& OutRowIndex, FString& OutError);
	static bool ValidateEditIntent(const FVexUiSchemaTableView& Table, int32 RowIndex, const FString& ColumnName, const FString& NewValue, FString& OutError);
	static bool TryApplyEdit(FVexUiSchemaTableView& InOutTable, int32 RowIndex, const FString& ColumnName, const FString& NewValue, FString& OutError);
};

struct FLIGHTPROJECT_API FVexUiNode
{
	EVexUiNodeType Type = EVexUiNodeType::TextStatic;
	FString Text;
	FString Symbol;
	FString Tab;
	FString SectionPath;
	FString SchemaTableName;
	FString SchemaRowKey;
	TArray<FString> Columns;
	TArray<FVexUiTableRow> StaticRows;
	float MinValue = 0.0f;
	float MaxValue = 1.0f;
};

struct FLIGHTPROJECT_API FVexUiCompileResult
{
	bool bSuccess = false;
	TArray<FVexUiNode> Nodes;
	TArray<FString> Errors;
};

class FLIGHTPROJECT_API FVexUiCompiler
{
public:
	// Minimal UI DSL:
	//   text("Hello")
	//   text(@symbol)
	//   slider(@symbol, 0, 100)
	//   tab("Name")
	//   section("Metrics") {
	//     ...
	//   }
	//   table(@rows_symbol, "colA,colB,colC")
	//   schema_table("vexSymbolRequirements")
	//   schema_form("vexSymbolRequirements", "0")
	//   include("script_id")
	static FVexUiCompileResult Compile(const FString& Script);
	static FVexUiCompileResult CompileWithIncludes(const FString& Script, const TMap<FString, FString>& IncludeLibrary);
};

class FLIGHTPROJECT_API FVexUiStore : public TSharedFromThis<FVexUiStore>
{
public:
	using FNumberSignal = Flight::Reactive::TReactiveValue<float>;
	using FTextSignal = Flight::Reactive::TReactiveValue<FString>;
	using FBoolSignal = Flight::Reactive::TReactiveValue<bool>;
	using FTableSignal = Flight::Reactive::TReactiveValue<TArray<FVexUiTableRow>>;
	using FSchemaTableSignal = Flight::Reactive::TReactiveValue<FVexUiSchemaTableView>;
	using FSchemaCommitHook = TFunction<bool(const FVexUiEditableCell& Edit, const FVexUiSchemaTableView& NextTable, FString& OutError)>;

	FNumberSignal& Number(const FString& Symbol, float DefaultValue = 0.0f);
	FTextSignal& Text(const FString& Symbol, const FString& DefaultValue = FString());
	FBoolSignal& Bool(const FString& Symbol, bool bDefaultValue = false);
	FTableSignal& Table(const FString& Symbol, const TArray<FVexUiTableRow>& DefaultRows = {});

	FNumberSignal* FindNumber(const FString& Symbol);
	FTextSignal* FindText(const FString& Symbol);
	FBoolSignal* FindBool(const FString& Symbol);
	FTableSignal* FindTable(const FString& Symbol);
	FSchemaTableSignal& SchemaTable(const FString& TableName);
	FSchemaTableSignal* FindSchemaTable(const FString& TableName);
	bool TryApplySchemaEdit(const FVexUiEditableCell& Edit, FString& OutError);
	void SetSchemaCommitHook(FSchemaCommitHook InHook);

private:
	TMap<FString, TSharedPtr<FNumberSignal>> NumberSignals;
	TMap<FString, TSharedPtr<FTextSignal>> TextSignals;
	TMap<FString, TSharedPtr<FBoolSignal>> BoolSignals;
	TMap<FString, TSharedPtr<FTableSignal>> TableSignals;
	TMap<FString, TSharedPtr<FSchemaTableSignal>> SchemaTableSignals;
	FSchemaCommitHook SchemaCommitHook;
};

class FLIGHTPROJECT_API SVexPanel : public Flight::ReactiveUI::SReactiveWidget
{
public:
	SLATE_BEGIN_ARGS(SVexPanel)
		: _Script(TEXT(""))
	{}
		SLATE_ARGUMENT(FString, Script)
		SLATE_ARGUMENT(TSharedPtr<FVexUiStore>, Store)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	const FVexUiCompileResult& GetCompileResult() const { return CompileResult; }

private:
	FText ResolveSymbolText(const FString& Symbol) const;
	void BuildNodeInto(TSharedRef<SVerticalBox> TargetBox, const FVexUiNode& Node);
	void BuildFromNodes();

	TSharedPtr<FVexUiStore> Store;
	TSharedPtr<SVerticalBox> RootBox;
	FVexUiCompileResult CompileResult;
};
} // namespace Flight::VexUI
