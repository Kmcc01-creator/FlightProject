// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/Frontend/VexAst.h"
#include "Vex/Frontend/VexParser.h"
#include "Vex/FlightVexSchema.h"

namespace Flight::Vex
{

enum class EVexSchemaBindingSource : uint8
{
	Schema,
	AmbientDefinition
};

enum class EVexSchemaAccessKind : uint8
{
	Read,
	Write
};

struct FVexSchemaBoundSymbol
{
	FString SymbolName;
	FVexLogicalSymbolSchema LogicalSymbol;
	EVexSchemaBindingSource Source = EVexSchemaBindingSource::Schema;
};

struct FVexSchemaSymbolUse
{
	int32 StatementIndex = INDEX_NONE;
	int32 TokenIndex = INDEX_NONE;
	int32 BoundSymbolIndex = INDEX_NONE;
	EVexSchemaAccessKind AccessKind = EVexSchemaAccessKind::Read;
};

struct FVexSchemaStatementBinding
{
	int32 StatementIndex = INDEX_NONE;
	TArray<int32> ReadSymbols;
	TArray<int32> WrittenSymbols;
};

struct FLIGHTPROJECT_API FVexSchemaBindingResult
{
	bool bSuccess = false;
	TArray<FString> Errors;
	TArray<FVexSymbolDefinition> SymbolDefinitions;
	TArray<FVexSchemaBoundSymbol> BoundSymbols;
	TArray<FVexSchemaSymbolUse> SymbolUses;
	TArray<FVexSchemaStatementBinding> StatementBindings;
	TSet<FString> ReadSymbols;
	TSet<FString> WrittenSymbols;

	const FVexSchemaBoundSymbol* FindBoundSymbolByName(const FString& Name) const;
	TMap<FString, FString> BuildVerseSymbolMap() const;
	TMap<FString, FString> BuildHlslSymbolMap() const;
};

class FLIGHTPROJECT_API FVexSchemaBinder
{
public:
	static TArray<FVexSymbolDefinition> BuildSymbolDefinitions(
		const FVexTypeSchema& Schema,
		const TArray<FVexSymbolDefinition>& AmbientDefinitions = {});

	static FVexSchemaBindingResult BindProgram(
		const FVexProgramAst& Program,
		const FVexTypeSchema& Schema,
		const TArray<FVexSymbolDefinition>& AmbientDefinitions = {});

private:
	static FVexLogicalSymbolSchema MakeSyntheticLogicalSymbol(const FVexSymbolDefinition& Definition);
};

} // namespace Flight::Vex
