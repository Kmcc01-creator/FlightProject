// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "UI/FlightVexUI.h"

#include "Schema/FlightRequirementRegistry.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"

#include "Core/FlightLogging.h"

namespace Flight::VexUI
{
namespace
{
	FString Trimmed(const FString& In)
	{
		FString Out = In;
		Out.TrimStartAndEndInline();
		return Out;
	}

	bool ParseInvocation(const FString& Line, const FString& Name, FString& OutArgs)
	{
		const FString Prefix = Name + TEXT("(");
		if (!Line.StartsWith(Prefix) || !Line.EndsWith(TEXT(")")))
		{
			return false;
		}
		OutArgs = Line.Mid(Prefix.Len(), Line.Len() - Prefix.Len() - 1);
		OutArgs.TrimStartAndEndInline();
		return true;
	}

	bool ParseSectionOpen(const FString& Line, FString& OutSectionName)
	{
		FString PrefixArgs;
		if (!Line.EndsWith(TEXT("{")))
		{
			return false;
		}

		const FString Left = Trimmed(Line.LeftChop(1));
		if (!ParseInvocation(Left, TEXT("section"), PrefixArgs))
		{
			return false;
		}

		if (!PrefixArgs.StartsWith(TEXT("\"")) || !PrefixArgs.EndsWith(TEXT("\"")) || PrefixArgs.Len() < 2)
		{
			return false;
		}

		OutSectionName = PrefixArgs.Mid(1, PrefixArgs.Len() - 2);
		return true;
	}

	bool ParseQuoted(const FString& In, FString& Out)
	{
		if (!In.StartsWith(TEXT("\"")) || !In.EndsWith(TEXT("\"")) || In.Len() < 2)
		{
			return false;
		}
		Out = In.Mid(1, In.Len() - 2);
		return true;
	}

	TArray<FString> ParseCallArgs(const FString& In)
	{
		TArray<FString> Parts;
		FString Current;
		bool bInQuotes = false;

		for (int32 Index = 0; Index < In.Len(); ++Index)
		{
			const TCHAR C = In[Index];
			if (C == TCHAR('"'))
			{
				bInQuotes = !bInQuotes;
				Current.AppendChar(C);
				continue;
			}

			if (C == TCHAR(',') && !bInQuotes)
			{
				Current.TrimStartAndEndInline();
				Parts.Add(Current);
				Current.Reset();
				continue;
			}

			Current.AppendChar(C);
		}

		Current.TrimStartAndEndInline();
		if (!Current.IsEmpty() || In.EndsWith(TEXT(",")))
		{
			Parts.Add(Current);
		}

		return Parts;
	}

	TArray<FString> ParseSimpleCsv(const FString& In)
	{
		TArray<FString> Parts;
		In.ParseIntoArray(Parts, TEXT(","), true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
		}
		return Parts;
	}

	FString ComposeSectionPath(const TArray<FString>& SectionStack)
	{
		return SectionStack.Num() > 0 ? FString::Join(SectionStack, TEXT(" / ")) : FString();
	}

	FString BoolToString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString CVarValueTypeToString(const EFlightCVarValueType Type)
	{
		switch (Type)
		{
		case EFlightCVarValueType::String:
			return TEXT("String");
		case EFlightCVarValueType::Int:
			return TEXT("Int");
		case EFlightCVarValueType::Float:
			return TEXT("Float");
		case EFlightCVarValueType::Bool:
			return TEXT("Bool");
		default:
			return TEXT("Unknown");
		}
	}

	FString VexValueTypeToString(const EFlightVexSymbolValueType Type)
	{
		switch (Type)
		{
		case EFlightVexSymbolValueType::Float:
			return TEXT("float");
		case EFlightVexSymbolValueType::Float2:
			return TEXT("float2");
		case EFlightVexSymbolValueType::Float3:
			return TEXT("float3");
		case EFlightVexSymbolValueType::Float4:
			return TEXT("float4");
		case EFlightVexSymbolValueType::Int:
			return TEXT("int");
		case EFlightVexSymbolValueType::Bool:
			return TEXT("bool");
		default:
			return TEXT("unknown");
		}
	}

	FString VexResidencyToString(const EFlightVexSymbolResidency Residency)
	{
		switch (Residency)
		{
		case EFlightVexSymbolResidency::Shared:
			return TEXT("Shared");
		case EFlightVexSymbolResidency::GpuOnly:
			return TEXT("GpuOnly");
		case EFlightVexSymbolResidency::CpuOnly:
			return TEXT("CpuOnly");
		default:
			return TEXT("Unknown");
		}
	}

	FString VexAffinityToString(const EFlightVexSymbolAffinity Affinity)
	{
		switch (Affinity)
		{
		case EFlightVexSymbolAffinity::Any:
			return TEXT("Any");
		case EFlightVexSymbolAffinity::GameThread:
			return TEXT("GameThread");
		case EFlightVexSymbolAffinity::RenderThread:
			return TEXT("RenderThread");
		case EFlightVexSymbolAffinity::WorkerThread:
			return TEXT("WorkerThread");
		default:
			return TEXT("Unknown");
		}
	}

	void AddIssueIf(bool bCondition, const FString& Message, TArray<FString>& InOutIssues)
	{
		if (bCondition)
		{
			InOutIssues.Add(Message);
		}
	}

	bool IsBoolType(const FString& Type)
	{
		return Type.Equals(TEXT("bool"), ESearchCase::IgnoreCase);
	}

	bool IsIntegerType(const FString& Type)
	{
		return Type.Equals(TEXT("int"), ESearchCase::IgnoreCase);
	}

	bool IsFloatType(const FString& Type)
	{
		return Type.Equals(TEXT("float"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("number"), ESearchCase::IgnoreCase);
	}

	bool IsNumericType(const FString& Type)
	{
		return IsIntegerType(Type) || IsFloatType(Type);
	}

	bool ParseBoolValue(const FString& InValue, bool& OutBool)
	{
		const FString TrimmedValue = InValue.TrimStartAndEnd();
		if (TrimmedValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || TrimmedValue == TEXT("1"))
		{
			OutBool = true;
			return true;
		}
		if (TrimmedValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) || TrimmedValue == TEXT("0"))
		{
			OutBool = false;
			return true;
		}
		return false;
	}

	const FVexUiSchemaColumn* FindSchemaColumnByName(const FVexUiSchemaTableView& Table, const FString& ColumnName)
	{
		for (const FVexUiSchemaColumn& Column : Table.Columns)
		{
			if (Column.Name == ColumnName)
			{
				return &Column;
			}
		}
		return nullptr;
	}

	TArray<FString> ComputeRowValidationIssues(const FVexUiSchemaTableView& Table, const FVexUiTableRow& Row)
	{
		TArray<FString> Issues;
		for (const FVexUiSchemaColumn& Column : Table.Columns)
		{
			const FString* ValuePtr = Row.Fields.Find(Column.Name);
			const FString Value = ValuePtr ? ValuePtr->TrimStartAndEnd() : FString();

			if (Column.bRequired && Value.IsEmpty())
			{
				Issues.Add(FString::Printf(TEXT("%s is required"), *Column.Name));
				continue;
			}

			if (Value.IsEmpty())
			{
				continue;
			}

			if (IsBoolType(Column.Type))
			{
				bool bParsed = false;
				if (!ParseBoolValue(Value, bParsed))
				{
					Issues.Add(FString::Printf(TEXT("%s expects bool"), *Column.Name));
				}
			}
			else if (IsIntegerType(Column.Type))
			{
				int64 Parsed = 0;
				if (!LexTryParseString<int64>(Parsed, *Value))
				{
					Issues.Add(FString::Printf(TEXT("%s expects int"), *Column.Name));
				}
			}
			else if (IsFloatType(Column.Type))
			{
				double Parsed = 0.0;
				if (!LexTryParseString<double>(Parsed, *Value))
				{
					Issues.Add(FString::Printf(TEXT("%s expects float"), *Column.Name));
				}
			}
		}

		return Issues;
	}
}

bool FVexUiSchemaViewModel::TryBuildTable(const FString& TableName, FVexUiSchemaTableView& OutTable, FString& OutError)
{
	const FString Normalized = TableName.TrimStartAndEnd();
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();

	OutTable = FVexUiSchemaTableView{};
	OutTable.Name = Normalized;

	auto AddColumn = [&OutTable](const TCHAR* Name, const TCHAR* Type, const bool bRequired)
	{
		FVexUiSchemaColumn Column;
		Column.Name = Name;
		Column.Type = Type;
		Column.bRequired = bRequired;
		OutTable.Columns.Add(MoveTemp(Column));
	};

	if (Normalized.Equals(TEXT("assetRequirements"), ESearchCase::IgnoreCase))
	{
		OutTable.KeyColumn = TEXT("requirementId");
		AddColumn(TEXT("owner"), TEXT("name"), true);
		AddColumn(TEXT("requirementId"), TEXT("name"), true);
		AddColumn(TEXT("assetClass"), TEXT("string"), true);
		AddColumn(TEXT("assetPath"), TEXT("soft_path"), true);
		AddColumn(TEXT("templatePath"), TEXT("soft_path"), false);
		AddColumn(TEXT("tags"), TEXT("string[]"), false);
		AddColumn(TEXT("requiredProperties"), TEXT("string_map"), false);

		for (const FFlightAssetRequirementRow& Row : Manifest.AssetRequirements)
		{
			FVexUiTableRow UiRow;
			UiRow.Fields.Add(TEXT("owner"), Row.Owner.ToString());
			UiRow.Fields.Add(TEXT("requirementId"), Row.RequirementId.ToString());
			UiRow.Fields.Add(TEXT("assetClass"), Row.AssetClass);
			UiRow.Fields.Add(TEXT("assetPath"), Row.AssetPath.ToString());
			UiRow.Fields.Add(TEXT("templatePath"), Row.TemplatePath.ToString());
			UiRow.Fields.Add(TEXT("tags"), FString::Join(Row.Tags, TEXT(",")));

			TArray<FString> PropertyParts;
			for (const TPair<FString, FString>& Pair : Row.RequiredProperties)
			{
				PropertyParts.Add(FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value));
			}
			UiRow.Fields.Add(TEXT("requiredProperties"), FString::Join(PropertyParts, TEXT("; ")));

			AddIssueIf(Row.RequirementId.IsNone(), TEXT("requirementId is required"), UiRow.ValidationIssues);
			AddIssueIf(Row.AssetPath.IsNull(), TEXT("assetPath is required"), UiRow.ValidationIssues);
			OutTable.Rows.Add(MoveTemp(UiRow));
		}
		return true;
	}

	if (Normalized.Equals(TEXT("niagaraRequirements"), ESearchCase::IgnoreCase))
	{
		OutTable.KeyColumn = TEXT("requirementId");
		AddColumn(TEXT("owner"), TEXT("name"), true);
		AddColumn(TEXT("requirementId"), TEXT("name"), true);
		AddColumn(TEXT("systemPath"), TEXT("soft_path"), true);
		AddColumn(TEXT("emitterTemplatePaths"), TEXT("soft_path[]"), false);
		AddColumn(TEXT("requiredUserParameters"), TEXT("string[]"), false);
		AddColumn(TEXT("requiredDataInterfaces"), TEXT("string[]"), false);

		for (const FFlightNiagaraRequirementRow& Row : Manifest.NiagaraRequirements)
		{
			FVexUiTableRow UiRow;
			UiRow.Fields.Add(TEXT("owner"), Row.Owner.ToString());
			UiRow.Fields.Add(TEXT("requirementId"), Row.RequirementId.ToString());
			UiRow.Fields.Add(TEXT("systemPath"), Row.SystemPath.ToString());

			TArray<FString> EmitterPaths;
			for (const FSoftObjectPath& Path : Row.EmitterTemplatePaths)
			{
				EmitterPaths.Add(Path.ToString());
			}
			UiRow.Fields.Add(TEXT("emitterTemplatePaths"), FString::Join(EmitterPaths, TEXT(",")));
			UiRow.Fields.Add(TEXT("requiredUserParameters"), FString::Join(Row.RequiredUserParameters, TEXT(",")));
			UiRow.Fields.Add(TEXT("requiredDataInterfaces"), FString::Join(Row.RequiredDataInterfaces, TEXT(",")));

			AddIssueIf(Row.RequirementId.IsNone(), TEXT("requirementId is required"), UiRow.ValidationIssues);
			AddIssueIf(Row.SystemPath.IsNull(), TEXT("systemPath is required"), UiRow.ValidationIssues);
			OutTable.Rows.Add(MoveTemp(UiRow));
		}
		return true;
	}

	if (Normalized.Equals(TEXT("cvarRequirements"), ESearchCase::IgnoreCase))
	{
		OutTable.KeyColumn = TEXT("requirementId");
		AddColumn(TEXT("owner"), TEXT("name"), true);
		AddColumn(TEXT("requirementId"), TEXT("name"), true);
		AddColumn(TEXT("profileName"), TEXT("name"), true);
		AddColumn(TEXT("cvarName"), TEXT("string"), true);
		AddColumn(TEXT("expectedValue"), TEXT("string"), true);
		AddColumn(TEXT("valueType"), TEXT("enum"), true);
		AddColumn(TEXT("floatTolerance"), TEXT("float"), false);

		for (const FFlightCVarRequirementRow& Row : Manifest.CVarRequirements)
		{
			FVexUiTableRow UiRow;
			UiRow.Fields.Add(TEXT("owner"), Row.Owner.ToString());
			UiRow.Fields.Add(TEXT("requirementId"), Row.RequirementId.ToString());
			UiRow.Fields.Add(TEXT("profileName"), Row.ProfileName.ToString());
			UiRow.Fields.Add(TEXT("cvarName"), Row.CVarName);
			UiRow.Fields.Add(TEXT("expectedValue"), Row.ExpectedValue);
			UiRow.Fields.Add(TEXT("valueType"), CVarValueTypeToString(Row.ValueType));
			UiRow.Fields.Add(TEXT("floatTolerance"), FString::Printf(TEXT("%.6f"), Row.FloatTolerance));

			AddIssueIf(Row.RequirementId.IsNone(), TEXT("requirementId is required"), UiRow.ValidationIssues);
			AddIssueIf(Row.CVarName.IsEmpty(), TEXT("cvarName is required"), UiRow.ValidationIssues);
			AddIssueIf(Row.ExpectedValue.IsEmpty(), TEXT("expectedValue is required"), UiRow.ValidationIssues);
			OutTable.Rows.Add(MoveTemp(UiRow));
		}
		return true;
	}

	if (Normalized.Equals(TEXT("pluginRequirements"), ESearchCase::IgnoreCase))
	{
		OutTable.KeyColumn = TEXT("requirementId");
		AddColumn(TEXT("owner"), TEXT("name"), true);
		AddColumn(TEXT("requirementId"), TEXT("name"), true);
		AddColumn(TEXT("profileName"), TEXT("name"), true);
		AddColumn(TEXT("pluginName"), TEXT("string"), true);
		AddColumn(TEXT("expectedEnabled"), TEXT("bool"), true);
		AddColumn(TEXT("expectedMounted"), TEXT("bool"), true);

		for (const FFlightPluginRequirementRow& Row : Manifest.PluginRequirements)
		{
			FVexUiTableRow UiRow;
			UiRow.Fields.Add(TEXT("owner"), Row.Owner.ToString());
			UiRow.Fields.Add(TEXT("requirementId"), Row.RequirementId.ToString());
			UiRow.Fields.Add(TEXT("profileName"), Row.ProfileName.ToString());
			UiRow.Fields.Add(TEXT("pluginName"), Row.PluginName);
			UiRow.Fields.Add(TEXT("expectedEnabled"), BoolToString(Row.bExpectedEnabled));
			UiRow.Fields.Add(TEXT("expectedMounted"), BoolToString(Row.bExpectedMounted));

			AddIssueIf(Row.RequirementId.IsNone(), TEXT("requirementId is required"), UiRow.ValidationIssues);
			AddIssueIf(Row.PluginName.IsEmpty(), TEXT("pluginName is required"), UiRow.ValidationIssues);
			OutTable.Rows.Add(MoveTemp(UiRow));
		}
		return true;
	}

	if (Normalized.Equals(TEXT("vexSymbolRequirements"), ESearchCase::IgnoreCase))
	{
		OutTable.KeyColumn = TEXT("symbolName");
		AddColumn(TEXT("owner"), TEXT("name"), true);
		AddColumn(TEXT("requirementId"), TEXT("name"), true);
		AddColumn(TEXT("symbolName"), TEXT("string"), true);
		AddColumn(TEXT("valueType"), TEXT("enum"), true);
		AddColumn(TEXT("residency"), TEXT("enum"), true);
		AddColumn(TEXT("affinity"), TEXT("enum"), true);
		AddColumn(TEXT("hlslIdentifier"), TEXT("string"), true);
		AddColumn(TEXT("verseIdentifier"), TEXT("string"), true);
		AddColumn(TEXT("writable"), TEXT("bool"), true);
		AddColumn(TEXT("required"), TEXT("bool"), true);

		for (const FFlightVexSymbolRow& Row : Manifest.VexSymbolRequirements)
		{
			FVexUiTableRow UiRow;
			UiRow.Fields.Add(TEXT("owner"), Row.Owner.ToString());
			UiRow.Fields.Add(TEXT("requirementId"), Row.RequirementId.ToString());
			UiRow.Fields.Add(TEXT("symbolName"), Row.SymbolName);
			UiRow.Fields.Add(TEXT("valueType"), VexValueTypeToString(Row.ValueType));
			UiRow.Fields.Add(TEXT("residency"), VexResidencyToString(Row.Residency));
			UiRow.Fields.Add(TEXT("affinity"), VexAffinityToString(Row.Affinity));
			UiRow.Fields.Add(TEXT("hlslIdentifier"), Row.HlslIdentifier);
			UiRow.Fields.Add(TEXT("verseIdentifier"), Row.VerseIdentifier);
			UiRow.Fields.Add(TEXT("writable"), BoolToString(Row.bWritable));
			UiRow.Fields.Add(TEXT("required"), BoolToString(Row.bRequired));

			AddIssueIf(Row.RequirementId.IsNone(), TEXT("requirementId is required"), UiRow.ValidationIssues);
			AddIssueIf(Row.SymbolName.IsEmpty(), TEXT("symbolName is required"), UiRow.ValidationIssues);
			AddIssueIf(!Row.SymbolName.StartsWith(TEXT("@")), TEXT("symbolName should start with '@'"), UiRow.ValidationIssues);
			AddIssueIf(Row.HlslIdentifier.IsEmpty(), TEXT("hlslIdentifier is required"), UiRow.ValidationIssues);
			AddIssueIf(Row.VerseIdentifier.IsEmpty(), TEXT("verseIdentifier is required"), UiRow.ValidationIssues);
			OutTable.Rows.Add(MoveTemp(UiRow));
		}
		return true;
	}

	if (Normalized.Equals(TEXT("renderProfiles"), ESearchCase::IgnoreCase))
	{
		OutTable.KeyColumn = TEXT("profileName");
		AddColumn(TEXT("profileName"), TEXT("name"), true);
		AddColumn(TEXT("enableLumen"), TEXT("bool"), true);
		AddColumn(TEXT("enableNanite"), TEXT("bool"), true);
		AddColumn(TEXT("requiredCVars"), TEXT("string_map"), false);

		for (const FFlightRenderProfileRow& Row : Manifest.RenderProfiles)
		{
			FVexUiTableRow UiRow;
			UiRow.Fields.Add(TEXT("profileName"), Row.ProfileName.ToString());
			UiRow.Fields.Add(TEXT("enableLumen"), BoolToString(Row.bEnableLumen));
			UiRow.Fields.Add(TEXT("enableNanite"), BoolToString(Row.bEnableNanite));

			TArray<FString> CVarParts;
			for (const TPair<FString, FString>& Pair : Row.RequiredCVars)
			{
				CVarParts.Add(FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value));
			}
			UiRow.Fields.Add(TEXT("requiredCVars"), FString::Join(CVarParts, TEXT("; ")));

			AddIssueIf(Row.ProfileName.IsNone(), TEXT("profileName is required"), UiRow.ValidationIssues);
			OutTable.Rows.Add(MoveTemp(UiRow));
		}
		return true;
	}

	OutError = FString::Printf(
		TEXT("unsupported schema table '%s' (expected one of: assetRequirements, niagaraRequirements, cvarRequirements, pluginRequirements, vexSymbolRequirements, renderProfiles)"),
		*Normalized);
	return false;
}

bool FVexUiSchemaViewModel::ResolveRowIndex(const FVexUiSchemaTableView& Table, const FString& RowKey, int32& OutRowIndex, FString& OutError)
{
	if (Table.Rows.Num() == 0)
	{
		OutError = TEXT("schema table has no rows");
		return false;
	}

	const FString NormalizedKey = RowKey.TrimStartAndEnd();
	if (NormalizedKey.IsEmpty())
	{
		OutRowIndex = 0;
		return true;
	}

	int32 ParsedIndex = INDEX_NONE;
	if (LexTryParseString<int32>(ParsedIndex, *NormalizedKey))
	{
		if (Table.Rows.IsValidIndex(ParsedIndex))
		{
			OutRowIndex = ParsedIndex;
			return true;
		}

		OutError = FString::Printf(TEXT("row index %d out of range [0,%d]"), ParsedIndex, Table.Rows.Num() - 1);
		return false;
	}

	if (!Table.KeyColumn.IsEmpty())
	{
		for (int32 RowIndex = 0; RowIndex < Table.Rows.Num(); ++RowIndex)
		{
			const FString* KeyValue = Table.Rows[RowIndex].Fields.Find(Table.KeyColumn);
			if (KeyValue && *KeyValue == NormalizedKey)
			{
				OutRowIndex = RowIndex;
				return true;
			}
		}
	}

	OutError = FString::Printf(TEXT("row key '%s' not found"), *NormalizedKey);
	return false;
}

bool FVexUiSchemaViewModel::ValidateEditIntent(const FVexUiSchemaTableView& Table, int32 RowIndex, const FString& ColumnName, const FString& NewValue, FString& OutError)
{
	if (!Table.Rows.IsValidIndex(RowIndex))
	{
		OutError = FString::Printf(TEXT("row index %d out of range [0,%d]"), RowIndex, Table.Rows.Num() - 1);
		return false;
	}

	const FVexUiSchemaColumn* Column = FindSchemaColumnByName(Table, ColumnName);
	if (!Column)
	{
		OutError = FString::Printf(TEXT("unknown column '%s'"), *ColumnName);
		return false;
	}

	const FString Value = NewValue.TrimStartAndEnd();
	if (Column->bRequired && Value.IsEmpty())
	{
		OutError = FString::Printf(TEXT("column '%s' is required"), *Column->Name);
		return false;
	}

	if (Value.IsEmpty())
	{
		return true;
	}

	if (IsBoolType(Column->Type))
	{
		bool bParsedBool = false;
		if (!ParseBoolValue(Value, bParsedBool))
		{
			OutError = FString::Printf(TEXT("column '%s' expects bool (true/false/1/0)"), *Column->Name);
			return false;
		}
		return true;
	}

	if (IsIntegerType(Column->Type))
	{
		int64 ParsedInt = 0;
		if (!LexTryParseString<int64>(ParsedInt, *Value))
		{
			OutError = FString::Printf(TEXT("column '%s' expects int"), *Column->Name);
			return false;
		}
		return true;
	}

	if (IsFloatType(Column->Type))
	{
		double ParsedFloat = 0.0;
		if (!LexTryParseString<double>(ParsedFloat, *Value))
		{
			OutError = FString::Printf(TEXT("column '%s' expects float"), *Column->Name);
			return false;
		}
	}

	return true;
}

bool FVexUiSchemaViewModel::TryApplyEdit(FVexUiSchemaTableView& InOutTable, int32 RowIndex, const FString& ColumnName, const FString& NewValue, FString& OutError)
{
	if (!ValidateEditIntent(InOutTable, RowIndex, ColumnName, NewValue, OutError))
	{
		return false;
	}

	FVexUiTableRow& Row = InOutTable.Rows[RowIndex];
	const FVexUiSchemaColumn* Column = FindSchemaColumnByName(InOutTable, ColumnName);
	if (!Column)
	{
		OutError = FString::Printf(TEXT("unknown column '%s'"), *ColumnName);
		return false;
	}

	const FString TrimmedNewValue = NewValue.TrimStartAndEnd();
	if (IsBoolType(Column->Type))
	{
		bool bParsedBool = false;
		ParseBoolValue(TrimmedNewValue, bParsedBool);
		Row.Fields.Add(ColumnName, BoolToString(bParsedBool));
	}
	else if (IsIntegerType(Column->Type))
	{
		int64 ParsedInt = 0;
		LexTryParseString<int64>(ParsedInt, *TrimmedNewValue);
		Row.Fields.Add(ColumnName, FString::Printf(TEXT("%lld"), ParsedInt));
	}
	else if (IsFloatType(Column->Type))
	{
		double ParsedFloat = 0.0;
		LexTryParseString<double>(ParsedFloat, *TrimmedNewValue);
		Row.Fields.Add(ColumnName, FString::Printf(TEXT("%.6f"), ParsedFloat));
	}
	else
	{
		Row.Fields.Add(ColumnName, TrimmedNewValue);
	}

	Row.ValidationIssues = ComputeRowValidationIssues(InOutTable, Row);
	return true;
}

FVexUiCompileResult FVexUiCompiler::Compile(const FString& Script)
{
	return CompileWithIncludes(Script, TMap<FString, FString>());
}

FVexUiCompileResult FVexUiCompiler::CompileWithIncludes(const FString& Script, const TMap<FString, FString>& IncludeLibrary)
{
	FVexUiCompileResult Result;
	FString CurrentTab;
	TArray<FString> SectionStack;
	TSet<FString> ActiveIncludes;

	TFunction<void(const FString&, const FString&)> ParseScript;
	ParseScript = [&](const FString& InScript, const FString& Origin)
	{
		TArray<FString> Lines;
		InScript.ParseIntoArrayLines(Lines, false);
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString Line = Trimmed(Lines[LineIndex]);
			if (Line.IsEmpty() || Line.StartsWith(TEXT("//")))
			{
				continue;
			}

			if (Line == TEXT("}"))
			{
				if (SectionStack.Num() == 0)
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: unexpected section close '}'"), *Origin, LineIndex + 1));
				}
				else
				{
					SectionStack.Pop(EAllowShrinking::No);
				}
				continue;
			}

			FString OpenSection;
			if (ParseSectionOpen(Line, OpenSection))
			{
				SectionStack.Add(OpenSection);
				continue;
			}

			FString Args;
			if (ParseInvocation(Line, TEXT("tab"), Args))
			{
				FString TabName;
				if (ParseQuoted(Args, TabName))
				{
					CurrentTab = TabName;
				}
				else
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: tab() requires a quoted name"), *Origin, LineIndex + 1));
				}
				continue;
			}

			if (ParseInvocation(Line, TEXT("include"), Args))
			{
				FString IncludeId;
				if (!ParseQuoted(Args, IncludeId))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: include() requires a quoted script id"), *Origin, LineIndex + 1));
					continue;
				}

				if (ActiveIncludes.Contains(IncludeId))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: cyclic include detected for '%s'"), *Origin, LineIndex + 1, *IncludeId));
					continue;
				}

				const FString* IncludedScript = IncludeLibrary.Find(IncludeId);
				if (!IncludedScript)
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: include target '%s' not found"), *Origin, LineIndex + 1, *IncludeId));
					continue;
				}

				ActiveIncludes.Add(IncludeId);
				ParseScript(*IncludedScript, IncludeId);
				ActiveIncludes.Remove(IncludeId);
				continue;
			}

			if (ParseInvocation(Line, TEXT("log"), Args))
			{
				FVexUiNode Node;
				Node.Type = EVexUiNodeType::Log;
				Node.Tab = CurrentTab;
				Node.SectionPath = ComposeSectionPath(SectionStack);

				if (Args.StartsWith(TEXT("@")))
				{
					Node.Symbol = Args;
					Result.Nodes.Add(MoveTemp(Node));
					continue;
				}

				FString Literal;
				if (ParseQuoted(Args, Literal))
				{
					Node.Text = Literal;
					Result.Nodes.Add(MoveTemp(Node));
					continue;
				}

				Result.Errors.Add(FString::Printf(TEXT("%s:%d: invalid log() argument '%s'"), *Origin, LineIndex + 1, *Args));
				continue;
			}

			if (ParseInvocation(Line, TEXT("text"), Args))
			{
				FVexUiNode Node;
				Node.Tab = CurrentTab;
				Node.SectionPath = ComposeSectionPath(SectionStack);

				if (Args.StartsWith(TEXT("@")))
				{
					Node.Type = EVexUiNodeType::TextSymbol;
					Node.Symbol = Args;
					Result.Nodes.Add(MoveTemp(Node));
					continue;
				}

				FString Literal;
				if (ParseQuoted(Args, Literal))
				{
					Node.Type = EVexUiNodeType::TextStatic;
					Node.Text = Literal;
					Result.Nodes.Add(MoveTemp(Node));
					continue;
				}

				Result.Errors.Add(FString::Printf(TEXT("%s:%d: invalid text() argument '%s'"), *Origin, LineIndex + 1, *Args));
				continue;
			}

			if (ParseInvocation(Line, TEXT("slider"), Args))
			{
				TArray<FString> Parts = ParseCallArgs(Args);
				if (Parts.Num() != 3)
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: slider() expects 3 arguments"), *Origin, LineIndex + 1));
					continue;
				}

				if (!Parts[0].StartsWith(TEXT("@")))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: slider() first argument must be a symbol"), *Origin, LineIndex + 1));
					continue;
				}

				float MinValue = 0.0f;
				float MaxValue = 0.0f;
				if (!LexTryParseString<float>(MinValue, *Parts[1]) || !LexTryParseString<float>(MaxValue, *Parts[2]) || MinValue >= MaxValue)
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: slider() range is invalid"), *Origin, LineIndex + 1));
					continue;
				}

				FVexUiNode Node;
				Node.Type = EVexUiNodeType::Slider;
				Node.Symbol = Parts[0];
				Node.MinValue = MinValue;
				Node.MaxValue = MaxValue;
				Node.Tab = CurrentTab;
				Node.SectionPath = ComposeSectionPath(SectionStack);
				Result.Nodes.Add(MoveTemp(Node));
				continue;
			}

			if (ParseInvocation(Line, TEXT("table"), Args))
			{
				TArray<FString> Parts = ParseCallArgs(Args);
				if (Parts.Num() != 2)
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: table() expects 2 arguments"), *Origin, LineIndex + 1));
					continue;
				}

				if (!Parts[0].StartsWith(TEXT("@")))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: table() first argument must be a symbol"), *Origin, LineIndex + 1));
					continue;
				}

				FString ColumnCsv;
				if (!ParseQuoted(Parts[1], ColumnCsv))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: table() second argument must be quoted column csv"), *Origin, LineIndex + 1));
					continue;
				}

				FVexUiNode Node;
				Node.Type = EVexUiNodeType::TableSymbol;
				Node.Symbol = Parts[0];
				Node.Columns = ParseSimpleCsv(ColumnCsv);
				Node.Tab = CurrentTab;
				Node.SectionPath = ComposeSectionPath(SectionStack);
				Result.Nodes.Add(MoveTemp(Node));
				continue;
			}

			if (ParseInvocation(Line, TEXT("schema_table"), Args))
			{
				FString TableName;
				if (!ParseQuoted(Args, TableName))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: schema_table() requires a quoted table name"), *Origin, LineIndex + 1));
					continue;
				}

				FVexUiSchemaTableView TableView;
				FString SchemaError;
				if (!FVexUiSchemaViewModel::TryBuildTable(TableName, TableView, SchemaError))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: %s"), *Origin, LineIndex + 1, *SchemaError));
					continue;
				}

				FVexUiNode Node;
				Node.Type = EVexUiNodeType::SchemaTable;
				Node.SchemaTableName = TableView.Name;
				Node.Columns.Reserve(TableView.Columns.Num());
				for (const FVexUiSchemaColumn& Column : TableView.Columns)
				{
					Node.Columns.Add(Column.Name);
				}
				Node.StaticRows = MoveTemp(TableView.Rows);
				Node.Tab = CurrentTab;
				Node.SectionPath = ComposeSectionPath(SectionStack);
				Result.Nodes.Add(MoveTemp(Node));
				continue;
			}

			if (ParseInvocation(Line, TEXT("schema_form"), Args))
			{
				TArray<FString> Parts = ParseCallArgs(Args);
				if (Parts.Num() != 2)
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: schema_form() expects 2 arguments"), *Origin, LineIndex + 1));
					continue;
				}

				FString TableName;
				FString RowKey;
				if (!ParseQuoted(Parts[0], TableName) || !ParseQuoted(Parts[1], RowKey))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: schema_form() requires quoted table and row key"), *Origin, LineIndex + 1));
					continue;
				}

				FVexUiSchemaTableView TableView;
				FString SchemaError;
				if (!FVexUiSchemaViewModel::TryBuildTable(TableName, TableView, SchemaError))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: %s"), *Origin, LineIndex + 1, *SchemaError));
					continue;
				}

				int32 RowIndex = INDEX_NONE;
				if (!FVexUiSchemaViewModel::ResolveRowIndex(TableView, RowKey, RowIndex, SchemaError))
				{
					Result.Errors.Add(FString::Printf(TEXT("%s:%d: %s"), *Origin, LineIndex + 1, *SchemaError));
					continue;
				}

				FVexUiNode Node;
				Node.Type = EVexUiNodeType::SchemaForm;
				Node.SchemaTableName = TableView.Name;
				Node.SchemaRowKey = RowKey;
				Node.Columns.Reserve(TableView.Columns.Num());
				for (const FVexUiSchemaColumn& Column : TableView.Columns)
				{
					Node.Columns.Add(Column.Name);
				}
				Node.StaticRows = TableView.Rows.IsValidIndex(RowIndex) ? TArray<FVexUiTableRow>{TableView.Rows[RowIndex]} : TArray<FVexUiTableRow>{};
				Node.Tab = CurrentTab;
				Node.SectionPath = ComposeSectionPath(SectionStack);
				Result.Nodes.Add(MoveTemp(Node));
				continue;
			}

			Result.Errors.Add(FString::Printf(TEXT("%s:%d: unknown VEX UI directive '%s'"), *Origin, LineIndex + 1, *Line));
		}
	};

	ParseScript(Script, TEXT("<root>"));
	if (SectionStack.Num() > 0)
	{
		Result.Errors.Add(TEXT("<root>: unterminated section block(s)"));
	}

	Result.bSuccess = Result.Errors.Num() == 0;
	return Result;
}

FVexUiStore::FNumberSignal& FVexUiStore::Number(const FString& Symbol, float DefaultValue)
{
	if (TSharedPtr<FNumberSignal>* Existing = NumberSignals.Find(Symbol))
	{
		return *Existing->Get();
	}

	TSharedPtr<FNumberSignal> Created = MakeShared<FNumberSignal>(DefaultValue);
	NumberSignals.Add(Symbol, Created);
	return *Created.Get();
}

FVexUiStore::FTextSignal& FVexUiStore::Text(const FString& Symbol, const FString& DefaultValue)
{
	if (TSharedPtr<FTextSignal>* Existing = TextSignals.Find(Symbol))
	{
		return *Existing->Get();
	}

	TSharedPtr<FTextSignal> Created = MakeShared<FTextSignal>(DefaultValue);
	TextSignals.Add(Symbol, Created);
	return *Created.Get();
}

FVexUiStore::FBoolSignal& FVexUiStore::Bool(const FString& Symbol, bool bDefaultValue)
{
	if (TSharedPtr<FBoolSignal>* Existing = BoolSignals.Find(Symbol))
	{
		return *Existing->Get();
	}

	TSharedPtr<FBoolSignal> Created = MakeShared<FBoolSignal>(bDefaultValue);
	BoolSignals.Add(Symbol, Created);
	return *Created.Get();
}

FVexUiStore::FTableSignal& FVexUiStore::Table(const FString& Symbol, const TArray<FVexUiTableRow>& DefaultRows)
{
	if (TSharedPtr<FTableSignal>* Existing = TableSignals.Find(Symbol))
	{
		return *Existing->Get();
	}

	TSharedPtr<FTableSignal> Created = MakeShared<FTableSignal>(DefaultRows);
	TableSignals.Add(Symbol, Created);
	return *Created.Get();
}

FVexUiStore::FNumberSignal* FVexUiStore::FindNumber(const FString& Symbol)
{
	TSharedPtr<FNumberSignal>* Found = NumberSignals.Find(Symbol);
	return Found ? Found->Get() : nullptr;
}

FVexUiStore::FTextSignal* FVexUiStore::FindText(const FString& Symbol)
{
	TSharedPtr<FTextSignal>* Found = TextSignals.Find(Symbol);
	return Found ? Found->Get() : nullptr;
}

FVexUiStore::FBoolSignal* FVexUiStore::FindBool(const FString& Symbol)
{
	TSharedPtr<FBoolSignal>* Found = BoolSignals.Find(Symbol);
	return Found ? Found->Get() : nullptr;
}

FVexUiStore::FTableSignal* FVexUiStore::FindTable(const FString& Symbol)
{
	TSharedPtr<FTableSignal>* Found = TableSignals.Find(Symbol);
	return Found ? Found->Get() : nullptr;
}

FVexUiStore::FSchemaTableSignal& FVexUiStore::SchemaTable(const FString& TableName)
{
	if (TSharedPtr<FSchemaTableSignal>* Existing = SchemaTableSignals.Find(TableName))
	{
		return *Existing->Get();
	}

	FVexUiSchemaTableView Table;
	FString Error;
	if (!FVexUiSchemaViewModel::TryBuildTable(TableName, Table, Error))
	{
		Table.Name = TableName;
		FVexUiTableRow ErrorRow;
		ErrorRow.Fields.Add(TEXT("error"), Error);
		ErrorRow.ValidationIssues.Add(TEXT("schema lookup failed"));
		Table.Rows.Add(MoveTemp(ErrorRow));
	}

	TSharedPtr<FSchemaTableSignal> Created = MakeShared<FSchemaTableSignal>(MoveTemp(Table));
	SchemaTableSignals.Add(TableName, Created);
	return *Created.Get();
}

FVexUiStore::FSchemaTableSignal* FVexUiStore::FindSchemaTable(const FString& TableName)
{
	TSharedPtr<FSchemaTableSignal>* Found = SchemaTableSignals.Find(TableName);
	return Found ? Found->Get() : nullptr;
}

bool FVexUiStore::TryApplySchemaEdit(const FVexUiEditableCell& Edit, FString& OutError)
{
	FSchemaTableSignal& TableSignal = SchemaTable(Edit.TableName);
	FVexUiSchemaTableView Table = TableSignal.Get();
	if (!FVexUiSchemaViewModel::TryApplyEdit(Table, Edit.RowIndex, Edit.ColumnName, Edit.NewValue, OutError))
	{
		return false;
	}

	if (SchemaCommitHook && !SchemaCommitHook(Edit, Table, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("schema commit hook rejected edit");
		}
		return false;
	}

	TableSignal.Set(MoveTemp(Table));
	return true;
}

void FVexUiStore::SetSchemaCommitHook(FSchemaCommitHook InHook)
{
	SchemaCommitHook = MoveTemp(InHook);
}

void SVexPanel::Construct(const FArguments& InArgs)
{
	Store = InArgs._Store.IsValid() ? InArgs._Store : MakeShared<FVexUiStore>();
	CompileResult = FVexUiCompiler::Compile(InArgs._Script);

	ChildSlot
	[
		SAssignNew(RootBox, SVerticalBox)
	];

	BuildFromNodes();
}

FText SVexPanel::ResolveSymbolText(const FString& Symbol) const
{
	if (!Store.IsValid())
	{
		return FText::FromString(TEXT("<no store>"));
	}

	if (const FVexUiStore::FTextSignal* TextSignal = Store->FindText(Symbol))
	{
		return FText::FromString(TextSignal->Get());
	}
	if (const FVexUiStore::FNumberSignal* NumberSignal = Store->FindNumber(Symbol))
	{
		return FText::AsNumber(NumberSignal->Get());
	}
	if (const FVexUiStore::FBoolSignal* BoolSignal = Store->FindBool(Symbol))
	{
		return BoolSignal->Get() ? FText::FromString(TEXT("true")) : FText::FromString(TEXT("false"));
	}
	return FText::FromString(FString::Printf(TEXT("<unbound %s>"), *Symbol));
}

void SVexPanel::BuildNodeInto(TSharedRef<SVerticalBox> TargetBox, const FVexUiNode& Node)
{
	switch (Node.Type)
	{
	case EVexUiNodeType::TextStatic:
	{
		TargetBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(STextBlock).Text(FText::FromString(Node.Text))
		];
		break;
	}
	case EVexUiNodeType::TextSymbol:
	{
		TSharedRef<STextBlock> TextWidget = SNew(STextBlock).Text(ResolveSymbolText(Node.Symbol));
		TargetBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			TextWidget
		];

		if (FVexUiStore::FTextSignal* TextSignal = Store->FindText(Node.Symbol))
		{
			Flight::ReactiveUI::Bindings::BindText<FString>(ReactiveContext, TextWidget, *TextSignal);
		}
		else if (FVexUiStore::FNumberSignal* NumberSignal = Store->FindNumber(Node.Symbol))
		{
			Flight::ReactiveUI::Bindings::BindNumericText<float>(ReactiveContext, TextWidget, *NumberSignal, TEXT("{0}"));
		}
		else if (FVexUiStore::FBoolSignal* BoolSignal = Store->FindBool(Node.Symbol))
		{
			Flight::ReactiveUI::Bindings::BindText<bool>(
				ReactiveContext,
				TextWidget,
				*BoolSignal,
				[](const bool bValue) { return bValue ? FText::FromString(TEXT("true")) : FText::FromString(TEXT("false")); });
		}
		break;
	}
	case EVexUiNodeType::Log:
	{
		const FString DisplayText = Node.Symbol.IsEmpty() ? Node.Text : Node.Symbol;
		TargetBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Log")))
				.OnClicked_Lambda([StoreWeak = TWeakPtr<FVexUiStore>(Store), Node]()
				{
					const TSharedPtr<FVexUiStore> PinnedStore = StoreWeak.Pin();
					if (!PinnedStore.IsValid()) return FReply::Handled();

					using namespace Flight::Logging;
					if (Node.Symbol.IsEmpty())
					{
						FLogger::Get().Log(ELogLevel::Log, "LogFlightVexUI", Node.Text);
					}
					else
					{
						FLogContext Context;
						FString Message = FString::Printf(TEXT("VEX Symbol Log: %s"), *Node.Symbol);

						if (const FVexUiStore::FTextSignal* TextSignal = PinnedStore->FindText(Node.Symbol))
						{
							Context.KeyValues.Add(TEXT("Value"), TextSignal->Get());
						}
						else if (const FVexUiStore::FNumberSignal* NumberSignal = PinnedStore->FindNumber(Node.Symbol))
						{
							Context.KeyValues.Add(TEXT("Value"), FString::Printf(TEXT("%g"), NumberSignal->Get()));
						}
						else if (const FVexUiStore::FBoolSignal* BoolSignal = PinnedStore->FindBool(Node.Symbol))
						{
							Context.KeyValues.Add(TEXT("Value"), BoolSignal->Get() ? TEXT("true") : TEXT("false"));
						}
						else if (const FVexUiStore::FTableSignal* TableSignal = PinnedStore->FindTable(Node.Symbol))
						{
							const TArray<FVexUiTableRow>& Rows = TableSignal->Get();
							Context.KeyValues.Add(TEXT("RowCount"), FString::FromInt(Rows.Num()));
							// We could log more table details here if needed
						}

						FLogger::Get().Log(ELogLevel::Log, "LogFlightVexUI", Message, Context);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(DisplayText))
			]
		];
		break;
	}
	case EVexUiNodeType::Slider:
	{
		const float MinValue = Node.MinValue;
		const float MaxValue = Node.MaxValue;
		FVexUiStore::FNumberSignal& NumberSignal = Store->Number(Node.Symbol, MinValue);

		const auto ToUnit = [MinValue, MaxValue](float Value)
		{
			return FMath::Clamp((Value - MinValue) / FMath::Max(KINDA_SMALL_NUMBER, MaxValue - MinValue), 0.0f, 1.0f);
		};
		const auto FromUnit = [MinValue, MaxValue](float Unit)
		{
			return FMath::Lerp(MinValue, MaxValue, FMath::Clamp(Unit, 0.0f, 1.0f));
		};

		TSharedPtr<SSlider> Slider;
		TargetBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(Node.Symbol))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(Slider, SSlider)
				.Value(ToUnit(NumberSignal.Get()))
				.OnValueChanged_Lambda([FromUnit, &NumberSignal](float UnitValue)
				{
					NumberSignal.Set(FromUnit(UnitValue));
				})
			]
		];

		if (Slider.IsValid())
		{
			const TWeakPtr<SSlider> WeakSlider = Slider;
			const Flight::Reactive::TReactiveValue<float>::SubscriptionId Id = NumberSignal.SubscribeLambda(
				[WeakSlider, ToUnit](const float&, const float NewValue)
				{
					if (const TSharedPtr<SSlider> Pinned = WeakSlider.Pin())
					{
						Pinned->SetValue(ToUnit(NewValue));
					}
				});
			ReactiveContext.AddCleanup([&NumberSignal, Id]() { NumberSignal.Unsubscribe(Id); });
		}
		break;
	}
	case EVexUiNodeType::SchemaForm:
	{
		FVexUiStore::FSchemaTableSignal& TableSignal = Store->SchemaTable(Node.SchemaTableName);
		const FVexUiSchemaTableView& Table = TableSignal.Get();

		int32 RowIndex = INDEX_NONE;
		FString ResolveError;
		if (!FVexUiSchemaViewModel::ResolveRowIndex(Table, Node.SchemaRowKey, RowIndex, ResolveError))
		{
			TargetBox->AddSlot().AutoHeight().Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("schema_form error: %s"), *ResolveError)))
				.ColorAndOpacity(FSlateColor(FLinearColor::Red))
			];
			break;
		}

		const FVexUiTableRow& Row = Table.Rows[RowIndex];
		TargetBox->AddSlot().AutoHeight().Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%s row %d"), *Table.Name, RowIndex)))
		];

		if (Row.ValidationIssues.Num() > 0)
		{
			TargetBox->AddSlot().AutoHeight().Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("row issues: %s"), *FString::Join(Row.ValidationIssues, TEXT("; ")))))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.55f, 0.15f)))
			];
		}

		for (const FVexUiSchemaColumn& Column : Table.Columns)
		{
			const FString ColumnName = Column.Name;
			const FString ColumnType = Column.Type;
			const FString InitialValue = Row.Fields.FindRef(ColumnName);

			TargetBox->AddSlot().AutoHeight().Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s (%s)%s"), *ColumnName, *ColumnType, Column.bRequired ? TEXT(" *") : TEXT(""))))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 1.0f)))
			];

			if (IsBoolType(ColumnType))
			{
				bool bCurrentValue = false;
				ParseBoolValue(InitialValue, bCurrentValue);

				TargetBox->AddSlot().AutoHeight().Padding(2.0f)
				[
					SNew(SCheckBox)
					.IsChecked(bCurrentValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([StoreWeak = TWeakPtr<FVexUiStore>(Store), TableName = Node.SchemaTableName, RowIndex, ColumnName](ECheckBoxState State)
					{
						const TSharedPtr<FVexUiStore> PinnedStore = StoreWeak.Pin();
						if (!PinnedStore.IsValid())
						{
							return;
						}

						FVexUiEditableCell Edit;
						Edit.TableName = TableName;
						Edit.RowIndex = RowIndex;
						Edit.ColumnName = ColumnName;
						Edit.NewValue = (State == ECheckBoxState::Checked) ? TEXT("true") : TEXT("false");

						FString Error;
						if (!PinnedStore->TryApplySchemaEdit(Edit, Error))
						{
							UE_LOG(LogTemp, Warning, TEXT("schema_form bool edit rejected: %s"), *Error);
						}
					})
				];
			}
			else if (IsNumericType(ColumnType))
			{
				double ParsedValue = 0.0;
				LexTryParseString<double>(ParsedValue, *InitialValue);

				TargetBox->AddSlot().AutoHeight().Padding(2.0f)
				[
					SNew(SSpinBox<double>)
					.Value(ParsedValue)
					.Delta(IsIntegerType(ColumnType) ? 1.0 : 0.1)
					.OnValueCommitted_Lambda([StoreWeak = TWeakPtr<FVexUiStore>(Store), TableName = Node.SchemaTableName, RowIndex, ColumnName, ColumnType](double NewValue, ETextCommit::Type)
					{
						const TSharedPtr<FVexUiStore> PinnedStore = StoreWeak.Pin();
						if (!PinnedStore.IsValid())
						{
							return;
						}

						FVexUiEditableCell Edit;
						Edit.TableName = TableName;
						Edit.RowIndex = RowIndex;
						Edit.ColumnName = ColumnName;
						Edit.NewValue = IsIntegerType(ColumnType)
							? FString::Printf(TEXT("%lld"), static_cast<int64>(FMath::RoundToInt64(NewValue)))
							: FString::Printf(TEXT("%.6f"), NewValue);

						FString Error;
						if (!PinnedStore->TryApplySchemaEdit(Edit, Error))
						{
							UE_LOG(LogTemp, Warning, TEXT("schema_form numeric edit rejected: %s"), *Error);
						}
					})
				];
			}
			else
			{
				TargetBox->AddSlot().AutoHeight().Padding(2.0f)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(InitialValue))
					.OnTextCommitted_Lambda([StoreWeak = TWeakPtr<FVexUiStore>(Store), TableName = Node.SchemaTableName, RowIndex, ColumnName](const FText& InText, ETextCommit::Type)
					{
						const TSharedPtr<FVexUiStore> PinnedStore = StoreWeak.Pin();
						if (!PinnedStore.IsValid())
						{
							return;
						}

						FVexUiEditableCell Edit;
						Edit.TableName = TableName;
						Edit.RowIndex = RowIndex;
						Edit.ColumnName = ColumnName;
						Edit.NewValue = InText.ToString();

						FString Error;
						if (!PinnedStore->TryApplySchemaEdit(Edit, Error))
						{
							UE_LOG(LogTemp, Warning, TEXT("schema_form text edit rejected: %s"), *Error);
						}
					})
				];
			}
		}
		break;
	}
	case EVexUiNodeType::TableSymbol:
	case EVexUiNodeType::SchemaTable:
	{
		const bool bSchemaTable = Node.Type == EVexUiNodeType::SchemaTable;
		const FString HeaderText = bSchemaTable
			? FString::Printf(TEXT("%s [%d rows]"), *Node.SchemaTableName, Node.StaticRows.Num())
			: (Node.Columns.Num() > 0 ? FString::Join(Node.Columns, TEXT(" | ")) : Node.Symbol);

		TSharedPtr<SVerticalBox> RowsBox;
		TargetBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(HeaderText))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(RowsBox, SVerticalBox)
			]
		];

		if (!RowsBox.IsValid())
		{
			break;
		}

		auto RebuildRowsFromTyped = [WeakRows = TWeakPtr<SVerticalBox>(RowsBox), Columns = Node.Columns](const TArray<FVexUiTableRow>& RowsData)
		{
			const TSharedPtr<SVerticalBox> Rows = WeakRows.Pin();
			if (!Rows.IsValid())
			{
				return;
			}

			Rows->ClearChildren();
			if (RowsData.Num() == 0)
			{
				Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("<empty>")))];
				return;
			}

			for (const FVexUiTableRow& Row : RowsData)
			{
				TArray<FString> Fragments;
				if (Columns.Num() > 0)
				{
					for (const FString& Column : Columns)
					{
						const FString* Value = Row.Fields.Find(Column);
						Fragments.Add(FString::Printf(TEXT("%s=%s"), *Column, Value ? **Value : TEXT("-")));
					}
				}
				else
				{
					for (const TPair<FString, FString>& Pair : Row.Fields)
					{
						Fragments.Add(FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value));
					}
				}

				const FString Badge = Row.ValidationIssues.Num() > 0 ? TEXT("[!] ") : TEXT("[OK] ");
				Rows->AddSlot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Badge + FString::Join(Fragments, TEXT(" | "))))
					.ColorAndOpacity(Row.ValidationIssues.Num() > 0
						? FSlateColor(FLinearColor(0.95f, 0.7f, 0.15f))
						: FSlateColor::UseForeground())
				];
				if (Row.ValidationIssues.Num() > 0)
				{
					Rows->AddSlot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("    issues: %s"), *FString::Join(Row.ValidationIssues, TEXT("; ")))))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.55f, 0.15f)))
					];
				}
			}
		};

		auto RebuildRowsFromCsv = [WeakRows = TWeakPtr<SVerticalBox>(RowsBox), Columns = Node.Columns](const FString& CsvRows)
		{
			const TSharedPtr<SVerticalBox> Rows = WeakRows.Pin();
			if (!Rows.IsValid())
			{
				return;
			}

			Rows->ClearChildren();
			TArray<FString> Lines;
			CsvRows.ParseIntoArrayLines(Lines, true);
			if (Lines.Num() == 0)
			{
				Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("<empty>")))];
				return;
			}

			for (const FString& Line : Lines)
			{
				const TArray<FString> Cells = ParseSimpleCsv(Line);
				FString RowText;
				if (Columns.Num() > 0)
				{
					TArray<FString> Fragments;
					for (int32 Index = 0; Index < Columns.Num(); ++Index)
					{
						const FString Value = Cells.IsValidIndex(Index) ? Cells[Index] : TEXT("-");
						Fragments.Add(FString::Printf(TEXT("%s=%s"), *Columns[Index], *Value));
					}
					RowText = FString::Join(Fragments, TEXT(" | "));
				}
				else
				{
					RowText = Line;
				}
				Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(RowText))];
			}
		};

		if (bSchemaTable)
		{
			RebuildRowsFromTyped(Node.StaticRows);
		}
		else if (FVexUiStore::FTableSignal* TableSignal = Store->FindTable(Node.Symbol))
		{
			RebuildRowsFromTyped(TableSignal->Get());
			const Flight::Reactive::TReactiveValue<TArray<FVexUiTableRow>>::SubscriptionId Id = TableSignal->SubscribeLambda(
				[RebuildRowsFromTyped](const TArray<FVexUiTableRow>&, const TArray<FVexUiTableRow>& NewRows)
				{
					RebuildRowsFromTyped(NewRows);
				});
			ReactiveContext.AddCleanup([TableSignal, Id]() { TableSignal->Unsubscribe(Id); });
		}
		else if (FVexUiStore::FTextSignal* TableRowsSignal = Store->FindText(Node.Symbol))
		{
			RebuildRowsFromCsv(TableRowsSignal->Get());
			const Flight::Reactive::TReactiveValue<FString>::SubscriptionId Id = TableRowsSignal->SubscribeLambda(
				[RebuildRowsFromCsv](const FString&, const FString& NewRows)
				{
					RebuildRowsFromCsv(NewRows);
				});
			ReactiveContext.AddCleanup([TableRowsSignal, Id]() { TableRowsSignal->Unsubscribe(Id); });
		}
		else
		{
			RowsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("<unbound %s>"), *Node.Symbol)))];
		}
		break;
	}
	default:
		break;
	}
}

void SVexPanel::BuildFromNodes()
{
	if (!RootBox.IsValid())
	{
		return;
	}

	if (!CompileResult.bSuccess)
	{
		for (const FString& Error : CompileResult.Errors)
		{
			RootBox->AddSlot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Error))
				.ColorAndOpacity(FSlateColor(FLinearColor::Red))
			];
		}
		return;
	}

	auto MaybeAddSectionHeader = [](TSharedRef<SVerticalBox> TargetBox, const FString& SectionPath, FString& InOutLastSection)
	{
		if (SectionPath == InOutLastSection)
		{
			return;
		}

		InOutLastSection = SectionPath;
		if (SectionPath.IsEmpty())
		{
			return;
		}

		TargetBox->AddSlot().AutoHeight().Padding(2.0f)
		[
			SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("[%s]"), *SectionPath)))
		];
	};

	bool bHasTabs = false;
	for (const FVexUiNode& Node : CompileResult.Nodes)
	{
		if (!Node.Tab.IsEmpty())
		{
			bHasTabs = true;
			break;
		}
	}

	if (!bHasTabs)
	{
		FString LastSection;
		for (const FVexUiNode& Node : CompileResult.Nodes)
		{
			MaybeAddSectionHeader(RootBox.ToSharedRef(), Node.SectionPath, LastSection);
			BuildNodeInto(RootBox.ToSharedRef(), Node);
		}
		return;
	}

	TSharedPtr<SHorizontalBox> TabBar;
	TSharedPtr<SWidgetSwitcher> Switcher;
	RootBox->AddSlot().AutoHeight().Padding(2.0f)[SAssignNew(TabBar, SHorizontalBox)];
	RootBox->AddSlot().FillHeight(1.0f).Padding(2.0f)[SAssignNew(Switcher, SWidgetSwitcher)];
	if (!TabBar.IsValid() || !Switcher.IsValid())
	{
		return;
	}

	TMap<FString, TSharedPtr<SVerticalBox>> TabContent;
	TMap<FString, int32> TabIndices;
	TArray<FString> OrderedTabs;
	TMap<FString, FString> LastSectionByTab;

	auto EnsureTab = [&](const FString& InTabName) -> TSharedRef<SVerticalBox>
	{
		const FString EffectiveTab = InTabName.IsEmpty() ? TEXT("Main") : InTabName;
		if (TSharedPtr<SVerticalBox>* Existing = TabContent.Find(EffectiveTab))
		{
			return Existing->ToSharedRef();
		}

		TSharedPtr<SVerticalBox> NewBox;
		Switcher->AddSlot()[SAssignNew(NewBox, SVerticalBox)];
		const int32 TabIndex = Switcher->GetNumWidgets() - 1;

		TabContent.Add(EffectiveTab, NewBox);
		TabIndices.Add(EffectiveTab, TabIndex);
		OrderedTabs.Add(EffectiveTab);

		TabBar->AddSlot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SButton)
			.Text(FText::FromString(EffectiveTab))
			.OnClicked_Lambda([WeakSwitcher = TWeakPtr<SWidgetSwitcher>(Switcher), TabIndex]()
			{
				if (const TSharedPtr<SWidgetSwitcher> Pinned = WeakSwitcher.Pin())
				{
					Pinned->SetActiveWidgetIndex(TabIndex);
				}
				return FReply::Handled();
			})
		];

		return NewBox.ToSharedRef();
	};

	for (const FVexUiNode& Node : CompileResult.Nodes)
	{
		const FString EffectiveTab = Node.Tab.IsEmpty() ? TEXT("Main") : Node.Tab;
		TSharedRef<SVerticalBox> Box = EnsureTab(EffectiveTab);
		FString& LastSection = LastSectionByTab.FindOrAdd(EffectiveTab);
		MaybeAddSectionHeader(Box, Node.SectionPath, LastSection);
		BuildNodeInto(Box, Node);
	}

	if (OrderedTabs.Num() > 0)
	{
		if (const int32* FirstIndex = TabIndices.Find(OrderedTabs[0]))
		{
			Switcher->SetActiveWidgetIndex(*FirstIndex);
		}
	}
}
} // namespace Flight::VexUI
