// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexBoundaryTypes.h"
#include "Vex/FlightVexBackendCapabilities.h"
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
	int32 FragmentBindingIndex = INDEX_NONE;
};

struct FVexSchemaFragmentBinding
{
	FName FragmentType = NAME_None;
	EVexStorageKind StorageKind = EVexStorageKind::None;
	TArray<int32> BoundSymbolIndices;
	TSet<FString> ReadSymbols;
	TSet<FString> WrittenSymbols;
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

struct FVexSchemaBoundaryUse
{
	int32 StatementIndex = INDEX_NONE;
	int32 ExpressionNodeIndex = INDEX_NONE;
	EVexExprKind OperatorKind = EVexExprKind::Unknown;
	int32 SourceBoundSymbolIndex = INDEX_NONE;
	int32 DestinationBoundSymbolIndex = INDEX_NONE;
	FString SourceSymbolName;
	FString DestinationSymbolName;
	FVexBoundaryMetadata Metadata;
};

struct FVexBackendBindingDiagnostic
{
	EVexBackendKind Backend = EVexBackendKind::NativeScalar;
	FString SymbolName;
	EVexSchemaAccessKind AccessKind = EVexSchemaAccessKind::Read;
	EVexBackendDecision Decision = EVexBackendDecision::Rejected;
	bool bPreferredFastLane = false;
	FString Reason;
};

struct FLIGHTPROJECT_API FVexSchemaBindingResult
{
	bool bSuccess = false;
	TArray<FString> Errors;
	TArray<FVexSymbolDefinition> SymbolDefinitions;
	TArray<FVexSchemaBoundSymbol> BoundSymbols;
	TArray<FVexSchemaFragmentBinding> FragmentBindings;
	TArray<FVexSchemaSymbolUse> SymbolUses;
	TArray<FVexSchemaStatementBinding> StatementBindings;
	TArray<FVexSchemaBoundaryUse> BoundaryUses;
	TSet<FString> ReadSymbols;
	TSet<FString> WrittenSymbols;
	TSet<FString> ImportedSymbols;
	TSet<FString> ExportedSymbols;
	bool bHasBoundaryOperators = false;
	bool bHasAwaitableBoundary = false;
	bool bHasMirrorRequest = false;
	TArray<FVexBackendBindingDiagnostic> BackendDiagnostics;
	TMap<EVexBackendKind, bool> BackendLegality;

	const FVexSchemaBoundSymbol* FindBoundSymbolByName(const FString& Name) const;
	const FVexSchemaFragmentBinding* FindFragmentBindingByType(FName FragmentType) const;
	const FVexSchemaFragmentBinding* FindFragmentBindingForBoundSymbol(int32 BoundSymbolIndex) const;
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
