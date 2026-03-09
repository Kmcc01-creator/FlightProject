// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexOptics.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexRewriteRegistry.h"
#include "Vex/FlightVexTreeTraits.h"

namespace Flight::Vex
{

void FVexOptimizer::Optimize(FVexProgramAst& Program)
{
	// Apply rewrites until fixpoint (limit to 10 iterations to prevent infinite loops)
	for (int32 i = 0; i < 10; ++i)
	{
		if (!RewriteStep(Program))
		{
			break;
		}
	}
}

EVexTier FVexOptimizer::ClassifyTier(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions)
{
	TMap<FString, const FVexSymbolDefinition*> SymbolMap;
	for (const auto& D : SymbolDefinitions) SymbolMap.Add(D.SymbolName, &D);

	bool bHasAsync = false;
	bool bHasJobs = false;
	bool bHasBranches = false;
	bool bHasUnsupportedSimdSymbols = false;
	bool bHasUnsupportedGpuSymbols = false;

	for (const FVexStatementAst& S : Program.Statements)
	{
		if (S.Kind == EVexStatementKind::TargetDirective)
		{
			if (S.SourceSpan == TEXT("@async")) bHasAsync = true;
			if (S.SourceSpan == TEXT("@job") || S.SourceSpan == TEXT("@thread")) bHasJobs = true;
		}
		if (S.Kind == EVexStatementKind::IfHeader) bHasBranches = true;

		// Check symbol capabilities for Tier 1 eligibility
		if (S.Kind == EVexStatementKind::Assignment || S.Kind == EVexStatementKind::Expression || S.Kind == EVexStatementKind::IfHeader)
		{
			for (int32 i = S.StartTokenIndex; i <= S.EndTokenIndex; ++i)
			{
				if (Program.Tokens[i].Kind == EVexTokenKind::Symbol)
				{
					if (const auto* Def = SymbolMap.FindRef(Program.Tokens[i].Lexeme))
					{
						bool bIsBeingWritten = (S.Kind == EVexStatementKind::Assignment && i == S.StartTokenIndex);
						if (bIsBeingWritten)
						{
							if (!Def->bSimdWriteAllowed) bHasUnsupportedSimdSymbols = true;
						}
						else
						{
							if (!Def->bSimdReadAllowed) bHasUnsupportedSimdSymbols = true;
						}
						
						if (!Def->bGpuTier1Allowed) bHasUnsupportedGpuSymbols = true;
					}
				}
			}
		}
	}

	if (bHasAsync || bHasJobs) return EVexTier::Full;
	if (bHasBranches || bHasUnsupportedSimdSymbols || bHasUnsupportedGpuSymbols) return EVexTier::DFA;
	
	// Default to Literal if it's pure math and all symbols support it
	return EVexTier::Literal;
}

bool FVexOptimizer::RewriteStep(FVexProgramAst& Program)
{
	static const FVexAstRewriteRegistry RewriteRegistry = FVexAstRewriteRegistry::MakeDefault();
	bool bChanged = false;

	const FVexAstTreeView AstTree(Program);
	TraversePostOrder(AstTree, [&](const int32 NodeIndex)
	{
		if (!Program.Expressions.IsValidIndex(NodeIndex))
		{
			return;
		}

		FVexExpressionAst Replacement;
		if (RewriteRegistry.ApplyFirstMatch(
			Program.Expressions[NodeIndex],
			Program,
			EVexRewriteDomain::Core,
			Replacement,
			nullptr))
		{
			Program.Expressions[NodeIndex] = MoveTemp(Replacement);
			bChanged = true;
		}
	});

	return bChanged;
}

} // namespace Flight::Vex
