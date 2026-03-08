// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightVexOptics - Declarative AST transformation and tiered execution
//
// Influenced by m2/{optics/core} and regex tiered architecture.
// Provides tools for AST rewriting, pattern matching, and performance tiering.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexParser.h"

namespace Flight::Vex
{

/**
 * VEX Execution Tiers
 * Borrowed from m2/regex tiered matching strategy.
 */
enum class EVexTier : uint8
{
	/** Tier 1: Literal (Pure Math/SIMD). O(1) per agent. Zero branching. */
	Literal,

	/** Tier 2: DFA (Predicated/Deterministic). O(n) statements. No async/jobs. */
	DFA,

	/** Tier 3: Full (Backtracking/Async/Verse). Managed budget and depth. */
	Full
};

/**
 * VEX Pattern - Declarative matching on AST nodes.
 * Replaces complex manual switch/if checks with composable rules.
 */
template<typename TOutput>
class TVexPattern
{
public:
	using Handler = TFunction<TOptional<TOutput>(const FVexExpressionAst&, const FVexProgramAst&)>;

	explicit TVexPattern(Handler InHandler)
		: HandlerFn(MoveTemp(InHandler))
	{
	}

	TOptional<TOutput> Apply(const FVexExpressionAst& Node, const FVexProgramAst& Program) const
	{
		return HandlerFn(Node, Program);
	}

	// Combinator: Or
	TVexPattern Or(const TVexPattern& Other) const
	{
		return TVexPattern([*this, Other](const FVexExpressionAst& N, const FVexProgramAst& P)
		{
			if (auto Result = Apply(N, P)) return Result;
			return Other.Apply(N, P);
		});
	}

private:
	Handler HandlerFn;
};

/**
 * Vex Rewrite Rule - Match a pattern and return a replacement node.
 */
using FVexRewriteRule = TVexPattern<FVexExpressionAst>;

/**
 * Vex Optimizer - High-level pipeline for AST simplification.
 */
class FLIGHTPROJECT_API FVexOptimizer
{
public:
	/** Apply algebraic rewrites until fixpoint (no more changes). */
	static void Optimize(FVexProgramAst& Program);

	/** Formally classify the execution tier of the program. */
	static EVexTier ClassifyTier(const FVexProgramAst& Program);

private:
	/** Single pass of algebraic rewrites. Returns true if anything changed. */
	static bool RewriteStep(FVexProgramAst& Program);

	/** Identify and fold constant expressions (1.0 + 2.0 -> 3.0). */
	static TOptional<FVexExpressionAst> FoldConstants(const FVexExpressionAst& Node, const FVexProgramAst& Program);

	/** Identify and simplify identity operations (x * 1.0 -> x). */
	static TOptional<FVexExpressionAst> SimplifyIdentities(const FVexExpressionAst& Node, const FVexProgramAst& Program);
};

// ============================================================================
// Pattern Constructors
// ============================================================================

namespace Patterns
{
	/** Match binary operation with specific operator lexeme. */
	inline TVexPattern<bool> BinaryOp(const FString& Op)
	{
		return TVexPattern<bool>([Op](const FVexExpressionAst& N, const FVexProgramAst&)
		{
			return (N.Kind == EVexExprKind::BinaryOp && N.Lexeme == Op) ? TOptional<bool>(true) : TOptional<bool>();
		});
	}

	/** Match number literal. */
	inline TVexPattern<float> Number()
	{
		return TVexPattern<float>([](const FVexExpressionAst& N, const FVexProgramAst&)
		{
			return (N.Kind == EVexExprKind::NumberLiteral) ? TOptional<float>(FCString::Atof(*N.Lexeme)) : TOptional<float>();
		});
	}
}

} // namespace Flight::Vex
