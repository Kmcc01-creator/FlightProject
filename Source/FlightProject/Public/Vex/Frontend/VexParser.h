// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/Frontend/VexToken.h"
#include "Vex/Frontend/VexAst.h"
#include "Vex/FlightVexTypes.h"

namespace Flight::Schema
{
struct FManifestData;
}

namespace Flight::Vex
{

struct FVexSymbolDefinition
{
	FString SymbolName;
	FString ValueType;
	EFlightVexSymbolResidency Residency = EFlightVexSymbolResidency::Shared;
	EFlightVexSymbolAffinity Affinity = EFlightVexSymbolAffinity::Any;
	bool bWritable = true;
	bool bRequired = true;
	bool bSimdReadAllowed = true;
	bool bSimdWriteAllowed = true;
	bool bGpuTier1Allowed = true;
	EFlightVexAlignmentRequirement AlignmentRequirement = EFlightVexAlignmentRequirement::Any;
	EFlightVexMathDeterminismProfile MathDeterminismProfile = EFlightVexMathDeterminismProfile::Fast;
};

struct FVexParseResult
{
	bool bSuccess = false;
	FVexProgramAst Program;
	TArray<FVexIssue> Issues;
	TArray<FString> UnknownSymbols;
	TArray<FString> MissingRequiredSymbols;

	/** Bitmask of SCSL domains required by this script */
	EVexRequirement Requirements = EVexRequirement::None;
};

/** Build parser symbol definitions from schema manifest rows. */
TArray<FVexSymbolDefinition> BuildSymbolDefinitionsFromManifest(const Flight::Schema::FManifestData& ManifestData);

/** Tokenize source and build a minimal statement-oriented AST. */
FVexParseResult ParseAndValidate(
	const FString& Source,
	const TArray<FVexSymbolDefinition>& SymbolDefinitions,
	bool bRequireAllRequiredSymbols = false);

/** Human-readable multi-line formatter for parse/validation society. */
FString FormatIssues(const TArray<FVexIssue>& Issues);

/** Lower the validated AST to HLSL source code. */
FString LowerToHLSL(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, const TMap<FString, FString>& HlslBySymbol);

/** Lower the validated AST to Verse source code. */
FString LowerToVerse(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, const TMap<FString, FString>& VerseBySymbol);

/** Generate a unified HLSL Mega-Kernel from multiple behavior scripts with hoisting optimizations. */
FString LowerMegaKernel(const TMap<uint32, FVexProgramAst>& Scripts, const TArray<FVexSymbolDefinition>& SymbolDefinitions, const TMap<FString, FString>& SymbolMap);

} // namespace Flight::Vex
