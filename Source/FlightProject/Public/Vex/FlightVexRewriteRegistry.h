// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexIr.h"

namespace Flight::Vex
{

enum class EVexRewriteDomain : uint8
{
	None = 0,
	Core = 1 << 0,
	Simd = 1 << 1,
	Gpu = 1 << 2,
	Verse = 1 << 3,
	All = 0x0F
};
ENUM_CLASS_FLAGS(EVexRewriteDomain);

using FVexAstRewriteFn = TFunction<TOptional<FVexExpressionAst>(const FVexExpressionAst&, const FVexProgramAst&)>;

struct FVexAstRewriteRule
{
	FString Name;
	EVexRewriteDomain Domain = EVexRewriteDomain::Core;
	FVexAstRewriteFn Rule;
};

class FLIGHTPROJECT_API FVexAstRewriteRegistry
{
public:
	static FVexAstRewriteRegistry MakeDefault();

	void AddRule(FVexAstRewriteRule Rule);

	bool ApplyFirstMatch(
		const FVexExpressionAst& Node,
		const FVexProgramAst& Program,
		EVexRewriteDomain EnabledDomains,
		FVexExpressionAst& OutReplacement,
		FString* OutRuleName = nullptr) const;

	int32 Num() const { return Rules.Num(); }

private:
	TArray<FVexAstRewriteRule> Rules;
};

using FVexIrRewriteFn = TFunction<TOptional<FVexIrInstruction>(const FVexIrInstruction&, const FVexIrProgram&, int32 /*InstructionIndex*/)>;

struct FVexIrRewriteRule
{
	FString Name;
	EVexRewriteDomain Domain = EVexRewriteDomain::Core;
	FVexIrRewriteFn Rule;
};

class FLIGHTPROJECT_API FVexIrRewriteRegistry
{
public:
	void AddRule(FVexIrRewriteRule Rule);

	bool ApplyFirstMatch(
		const FVexIrInstruction& Instruction,
		const FVexIrProgram& Program,
		int32 InstructionIndex,
		EVexRewriteDomain EnabledDomains,
		FVexIrInstruction& OutReplacement,
		FString* OutRuleName = nullptr) const;

	int32 Num() const { return Rules.Num(); }

private:
	TArray<FVexIrRewriteRule> Rules;
};

} // namespace Flight::Vex
