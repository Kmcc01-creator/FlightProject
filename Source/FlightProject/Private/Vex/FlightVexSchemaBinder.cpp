// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexSchemaIr.h"

namespace Flight::Vex
{

namespace
{

FVexSymbolDefinition MakeDefinitionFromLogical(const FVexLogicalSymbolSchema& Logical)
{
	FVexSymbolDefinition Definition;
	Definition.SymbolName = Logical.SymbolName;
	Definition.ValueType = ToTypeString(Logical.ValueType);
	Definition.Residency = Logical.Residency;
	Definition.Affinity = Logical.Affinity;
	Definition.bWritable = Logical.bWritable;
	Definition.bRequired = Logical.bRequired;
	Definition.bSimdReadAllowed = Logical.bSimdReadAllowed;
	Definition.bSimdWriteAllowed = Logical.bSimdWriteAllowed;
	Definition.bGpuTier1Allowed = Logical.bGpuTier1Allowed;
	Definition.AlignmentRequirement = Logical.AlignmentRequirement;
	Definition.MathDeterminismProfile = Logical.MathDeterminismProfile;
	return Definition;
}

int32 FindAssignmentOperatorTokenIndex(const FVexProgramAst& Program, const FVexStatementAst& Statement)
{
	for (int32 TokenIndex = Statement.StartTokenIndex; TokenIndex <= Statement.EndTokenIndex; ++TokenIndex)
	{
		if (!Program.Tokens.IsValidIndex(TokenIndex))
		{
			continue;
		}

		const FVexToken& Token = Program.Tokens[TokenIndex];
		if (Token.Kind == EVexTokenKind::Operator
			&& (Token.Lexeme == TEXT("=")
				|| Token.Lexeme == TEXT("+=")
				|| Token.Lexeme == TEXT("-=")
				|| Token.Lexeme == TEXT("*=")
				|| Token.Lexeme == TEXT("/=")))
		{
			return TokenIndex;
		}
	}

	return INDEX_NONE;
}

int32 ResolveAssignmentTargetTokenIndex(const FVexProgramAst& Program, const FVexStatementAst& Statement)
{
	int32 TargetTokenIndex = Statement.StartTokenIndex;
	while (Program.Tokens.IsValidIndex(TargetTokenIndex)
		&& TargetTokenIndex <= Statement.EndTokenIndex
		&& Program.Tokens[TargetTokenIndex].Kind == EVexTokenKind::TargetDirective)
	{
		++TargetTokenIndex;
	}

	if (Program.Tokens.IsValidIndex(TargetTokenIndex + 1)
		&& TargetTokenIndex + 1 <= Statement.EndTokenIndex)
	{
		const FVexToken& MaybeTypeToken = Program.Tokens[TargetTokenIndex];
		const FVexToken& MaybeNameToken = Program.Tokens[TargetTokenIndex + 1];
		if (MaybeTypeToken.Kind == EVexTokenKind::Identifier
			&& MaybeNameToken.Kind == EVexTokenKind::Identifier
			&& IsDeclarationTypeToken(MaybeTypeToken.Lexeme))
		{
			++TargetTokenIndex;
		}
	}

	return (TargetTokenIndex <= Statement.EndTokenIndex) ? TargetTokenIndex : INDEX_NONE;
}

void AddSymbolUse(
	FVexSchemaBindingResult& Result,
	FVexSchemaStatementBinding& StatementBinding,
	const int32 StatementIndex,
	const int32 TokenIndex,
	const int32 BoundSymbolIndex,
	const EVexSchemaAccessKind AccessKind)
{
	FVexSchemaSymbolUse Use;
	Use.StatementIndex = StatementIndex;
	Use.TokenIndex = TokenIndex;
	Use.BoundSymbolIndex = BoundSymbolIndex;
	Use.AccessKind = AccessKind;
	Result.SymbolUses.Add(Use);

	const FString& SymbolName = Result.BoundSymbols[BoundSymbolIndex].SymbolName;
	if (AccessKind == EVexSchemaAccessKind::Write)
	{
		StatementBinding.WrittenSymbols.AddUnique(BoundSymbolIndex);
		Result.WrittenSymbols.Add(SymbolName);
	}
	else
	{
		StatementBinding.ReadSymbols.AddUnique(BoundSymbolIndex);
		Result.ReadSymbols.Add(SymbolName);
	}

	const int32 FragmentBindingIndex = Result.BoundSymbols[BoundSymbolIndex].FragmentBindingIndex;
	if (Result.FragmentBindings.IsValidIndex(FragmentBindingIndex))
	{
		FVexSchemaFragmentBinding& FragmentBinding = Result.FragmentBindings[FragmentBindingIndex];
		if (AccessKind == EVexSchemaAccessKind::Write)
		{
			FragmentBinding.WrittenSymbols.Add(SymbolName);
		}
		else
		{
			FragmentBinding.ReadSymbols.Add(SymbolName);
		}
	}
}

} // namespace

const FVexSchemaBoundSymbol* FVexSchemaBindingResult::FindBoundSymbolByName(const FString& Name) const
{
	return BoundSymbols.FindByPredicate([&Name](const FVexSchemaBoundSymbol& BoundSymbol)
	{
		return BoundSymbol.SymbolName == Name;
	});
}

const FVexSchemaFragmentBinding* FVexSchemaBindingResult::FindFragmentBindingByType(const FName FragmentType) const
{
	return FragmentBindings.FindByPredicate([FragmentType](const FVexSchemaFragmentBinding& FragmentBinding)
	{
		return FragmentBinding.FragmentType == FragmentType;
	});
}

const FVexSchemaFragmentBinding* FVexSchemaBindingResult::FindFragmentBindingForBoundSymbol(const int32 BoundSymbolIndex) const
{
	if (!BoundSymbols.IsValidIndex(BoundSymbolIndex))
	{
		return nullptr;
	}

	const int32 FragmentBindingIndex = BoundSymbols[BoundSymbolIndex].FragmentBindingIndex;
	return FragmentBindings.IsValidIndex(FragmentBindingIndex)
		? &FragmentBindings[FragmentBindingIndex]
		: nullptr;
}

TMap<FString, FString> FVexSchemaBindingResult::BuildVerseSymbolMap() const
{
	TMap<FString, FString> SymbolMap;
	for (const FVexSchemaBoundSymbol& BoundSymbol : BoundSymbols)
	{
		const FString FallbackName = BoundSymbol.SymbolName.StartsWith(TEXT("@"))
			? BoundSymbol.SymbolName.RightChop(1)
			: BoundSymbol.SymbolName;
		SymbolMap.Add(
			BoundSymbol.SymbolName,
			BoundSymbol.LogicalSymbol.BackendBinding.VerseIdentifier.IsEmpty()
				? FallbackName
				: BoundSymbol.LogicalSymbol.BackendBinding.VerseIdentifier);
	}
	return SymbolMap;
}

TMap<FString, FString> FVexSchemaBindingResult::BuildHlslSymbolMap() const
{
	TMap<FString, FString> SymbolMap;
	for (const FVexSchemaBoundSymbol& BoundSymbol : BoundSymbols)
	{
		const FString FallbackName = BoundSymbol.SymbolName.StartsWith(TEXT("@"))
			? BoundSymbol.SymbolName.RightChop(1)
			: BoundSymbol.SymbolName;
		SymbolMap.Add(
			BoundSymbol.SymbolName,
			BoundSymbol.LogicalSymbol.BackendBinding.HlslIdentifier.IsEmpty()
				? FallbackName
				: BoundSymbol.LogicalSymbol.BackendBinding.HlslIdentifier);
	}
	return SymbolMap;
}

FVexLogicalSymbolSchema FVexSchemaBinder::MakeSyntheticLogicalSymbol(const FVexSymbolDefinition& Definition)
{
	FVexLogicalSymbolSchema Logical;
	Logical.SymbolName = Definition.SymbolName;
	Logical.ValueType = ParseValueType(Definition.ValueType);
	Logical.bReadable = true;
	Logical.bWritable = Definition.bWritable;
	Logical.bRequired = Definition.bRequired;
	Logical.bSimdReadAllowed = Definition.bSimdReadAllowed;
	Logical.bSimdWriteAllowed = Definition.bSimdWriteAllowed;
	Logical.bGpuTier1Allowed = Definition.bGpuTier1Allowed;
	Logical.Residency = Definition.Residency;
	Logical.Affinity = Definition.Affinity;
	Logical.AlignmentRequirement = Definition.AlignmentRequirement;
	Logical.MathDeterminismProfile = Definition.MathDeterminismProfile;
	Logical.Storage.Kind = EVexStorageKind::ExternalProvider;

	if (Definition.SymbolName.StartsWith(TEXT("@")))
	{
		const FString FallbackName = Definition.SymbolName.RightChop(1);
		Logical.BackendBinding.HlslIdentifier = FallbackName;
		Logical.BackendBinding.VerseIdentifier = FallbackName;
	}

	return Logical;
}

TArray<FVexSymbolDefinition> FVexSchemaBinder::BuildSymbolDefinitions(
	const FVexTypeSchema& Schema,
	const TArray<FVexSymbolDefinition>& AmbientDefinitions)
{
	TArray<FVexSymbolDefinition> Definitions = AmbientDefinitions;
	TMap<FString, int32> DefinitionIndexByName;

	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		DefinitionIndexByName.Add(Definitions[Index].SymbolName, Index);
	}

	TArray<FString> SymbolNames;
	Schema.LogicalSymbols.GetKeys(SymbolNames);
	SymbolNames.Sort();

	for (const FString& SymbolName : SymbolNames)
	{
		const FVexLogicalSymbolSchema* Logical = Schema.LogicalSymbols.Find(SymbolName);
		if (!Logical)
		{
			continue;
		}

		const FVexSymbolDefinition Definition = MakeDefinitionFromLogical(*Logical);
		if (const int32* ExistingIndex = DefinitionIndexByName.Find(SymbolName))
		{
			Definitions[*ExistingIndex] = Definition;
		}
		else
		{
			DefinitionIndexByName.Add(SymbolName, Definitions.Add(Definition));
		}
	}

	return Definitions;
}

FVexSchemaBindingResult FVexSchemaBinder::BindProgram(
	const FVexProgramAst& Program,
	const FVexTypeSchema& Schema,
	const TArray<FVexSymbolDefinition>& AmbientDefinitions)
{
	FVexSchemaBindingResult Result;
	Result.SymbolDefinitions = BuildSymbolDefinitions(Schema, AmbientDefinitions);
	Result.StatementBindings.Reserve(Program.Statements.Num());

	TMap<FString, FVexLogicalSymbolSchema> LogicalSymbols = Schema.LogicalSymbols;
	for (const FVexSymbolDefinition& Definition : AmbientDefinitions)
	{
		if (!LogicalSymbols.Contains(Definition.SymbolName))
		{
			LogicalSymbols.Add(Definition.SymbolName, MakeSyntheticLogicalSymbol(Definition));
		}
	}

	TMap<FString, int32> BoundIndexByName;
	TMap<FName, int32> FragmentBindingIndexByType;
	TArray<FString> BoundNames;
	LogicalSymbols.GetKeys(BoundNames);
	BoundNames.Sort();

	for (const FString& SymbolName : BoundNames)
	{
		const FVexLogicalSymbolSchema* Logical = LogicalSymbols.Find(SymbolName);
		if (!Logical)
		{
			continue;
		}

		FVexSchemaBoundSymbol& BoundSymbol = Result.BoundSymbols.AddDefaulted_GetRef();
		BoundSymbol.SymbolName = SymbolName;
		BoundSymbol.LogicalSymbol = *Logical;
		BoundSymbol.Source = Schema.LogicalSymbols.Contains(SymbolName)
			? EVexSchemaBindingSource::Schema
			: EVexSchemaBindingSource::AmbientDefinition;

		if (Logical->Storage.Kind == EVexStorageKind::MassFragmentField && !Logical->Storage.FragmentType.IsNone())
		{
			int32* ExistingFragmentBindingIndex = FragmentBindingIndexByType.Find(Logical->Storage.FragmentType);
			if (!ExistingFragmentBindingIndex)
			{
				FVexSchemaFragmentBinding& FragmentBinding = Result.FragmentBindings.AddDefaulted_GetRef();
				FragmentBinding.FragmentType = Logical->Storage.FragmentType;
				FragmentBinding.StorageKind = Logical->Storage.Kind;
				FragmentBinding.BoundSymbolIndices.Add(Result.BoundSymbols.Num() - 1);
				FragmentBindingIndexByType.Add(Logical->Storage.FragmentType, Result.FragmentBindings.Num() - 1);
				BoundSymbol.FragmentBindingIndex = Result.FragmentBindings.Num() - 1;
			}
			else
			{
				Result.FragmentBindings[*ExistingFragmentBindingIndex].BoundSymbolIndices.Add(Result.BoundSymbols.Num() - 1);
				BoundSymbol.FragmentBindingIndex = *ExistingFragmentBindingIndex;
			}
		}

		BoundIndexByName.Add(SymbolName, Result.BoundSymbols.Num() - 1);
	}

	for (int32 StatementIndex = 0; StatementIndex < Program.Statements.Num(); ++StatementIndex)
	{
		const FVexStatementAst& Statement = Program.Statements[StatementIndex];
		FVexSchemaStatementBinding& StatementBinding = Result.StatementBindings.AddDefaulted_GetRef();
		StatementBinding.StatementIndex = StatementIndex;

		if (Statement.StartTokenIndex == INDEX_NONE || Statement.EndTokenIndex == INDEX_NONE)
		{
			continue;
		}

		const int32 AssignmentOperatorIndex = FindAssignmentOperatorTokenIndex(Program, Statement);
		const int32 AssignmentTargetIndex = Statement.Kind == EVexStatementKind::Assignment
			? ResolveAssignmentTargetTokenIndex(Program, Statement)
			: INDEX_NONE;
		const bool bCompoundAssignment = AssignmentOperatorIndex != INDEX_NONE
			&& Program.Tokens.IsValidIndex(AssignmentOperatorIndex)
			&& Program.Tokens[AssignmentOperatorIndex].Lexeme != TEXT("=");

		for (int32 TokenIndex = Statement.StartTokenIndex; TokenIndex <= Statement.EndTokenIndex; ++TokenIndex)
		{
			if (!Program.Tokens.IsValidIndex(TokenIndex))
			{
				continue;
			}

			const FVexToken& Token = Program.Tokens[TokenIndex];
			if (Token.Kind != EVexTokenKind::Symbol)
			{
				continue;
			}

			const int32* BoundSymbolIndex = BoundIndexByName.Find(Token.Lexeme);
			if (!BoundSymbolIndex)
			{
				Result.Errors.Add(FString::Printf(TEXT("Schema binder could not resolve symbol '%s'."), *Token.Lexeme));
				continue;
			}

			const FVexLogicalSymbolSchema& Logical = Result.BoundSymbols[*BoundSymbolIndex].LogicalSymbol;
			const bool bIsAssignmentTarget = Statement.Kind == EVexStatementKind::Assignment && TokenIndex == AssignmentTargetIndex;

			if (bIsAssignmentTarget)
			{
				if (!Logical.bWritable)
				{
					Result.Errors.Add(FString::Printf(TEXT("Schema binder resolved read-only write target '%s'."), *Token.Lexeme));
				}
				AddSymbolUse(Result, StatementBinding, StatementIndex, TokenIndex, *BoundSymbolIndex, EVexSchemaAccessKind::Write);

				if (bCompoundAssignment)
				{
					if (!Logical.bReadable)
					{
						Result.Errors.Add(FString::Printf(TEXT("Schema binder resolved unreadable compound assignment target '%s'."), *Token.Lexeme));
					}
					AddSymbolUse(Result, StatementBinding, StatementIndex, TokenIndex, *BoundSymbolIndex, EVexSchemaAccessKind::Read);
				}
			}
			else
			{
				if (!Logical.bReadable)
				{
					Result.Errors.Add(FString::Printf(TEXT("Schema binder resolved unreadable symbol '%s'."), *Token.Lexeme));
				}
				AddSymbolUse(Result, StatementBinding, StatementIndex, TokenIndex, *BoundSymbolIndex, EVexSchemaAccessKind::Read);
			}
		}
	}

	Result.bSuccess = Result.Errors.Num() == 0;
	return Result;
}

} // namespace Flight::Vex
