// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexOptics.h"
#include "Vex/FlightVexTypes.h"
#include "Math/UnrealMathUtility.h"

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

EVexTier FVexOptimizer::ClassifyTier(const FVexProgramAst& Program)
{
	bool bHasAsync = false;
	bool bHasJobs = false;
	bool bHasBranches = false;

	for (const FVexStatementAst& S : Program.Statements)
	{
		if (S.Kind == EVexStatementKind::TargetDirective)
		{
			if (S.SourceSpan == TEXT("@async")) bHasAsync = true;
			if (S.SourceSpan == TEXT("@job") || S.SourceSpan == TEXT("@thread")) bHasJobs = true;
		}
		if (S.Kind == EVexStatementKind::IfHeader) bHasBranches = true;
	}

	if (bHasAsync || bHasJobs) return EVexTier::Full;
	if (bHasBranches) return EVexTier::DFA;
	
	// Default to Literal if it's pure math
	return EVexTier::Literal;
}

bool FVexOptimizer::RewriteStep(FVexProgramAst& Program)
{
	bool bChanged = false;

	Program.TransformIterative([&](FVexExpressionAst& Node, int32 NodeIdx)
	{
		// 1. Constant Folding
		if (auto Folded = FoldConstants(Node, Program))
		{
			Node = MoveTemp(*Folded);
			bChanged = true;
		}

		// 2. Identity Simplification
		if (auto Simplified = SimplifyIdentities(Node, Program))
		{
			Node = MoveTemp(*Simplified);
			bChanged = true;
		}
	});

	return bChanged;
}

TOptional<FVexExpressionAst> FVexOptimizer::FoldConstants(const FVexExpressionAst& Node, const FVexProgramAst& Program)
{
	if (Node.Kind != EVexExprKind::BinaryOp) return TOptional<FVexExpressionAst>();

	const FVexExpressionAst* Left = Program.Expressions.IsValidIndex(Node.LeftNodeIndex) ? &Program.Expressions[Node.LeftNodeIndex] : nullptr;
	const FVexExpressionAst* Right = Program.Expressions.IsValidIndex(Node.RightNodeIndex) ? &Program.Expressions[Node.RightNodeIndex] : nullptr;

	if (Left && Right && Left->Kind == EVexExprKind::NumberLiteral && Right->Kind == EVexExprKind::NumberLiteral)
	{
		float LVal = FCString::Atof(*Left->Lexeme);
		float RVal = FCString::Atof(*Right->Lexeme);
		float Result = 0.0f;
		bool bValid = true;

		if (Node.Lexeme == TEXT("+")) Result = LVal + RVal;
		else if (Node.Lexeme == TEXT("-")) Result = LVal - RVal;
		else if (Node.Lexeme == TEXT("*")) Result = LVal * RVal;
		else if (Node.Lexeme == TEXT("/")) 
		{
			if (!FMath::IsNearlyZero(RVal)) Result = LVal / RVal;
			else bValid = false;
		}
		else bValid = false;

		if (bValid)
		{
			FVexExpressionAst NewNode;
			NewNode.Kind = EVexExprKind::NumberLiteral;
			NewNode.Lexeme = FString::SanitizeFloat(Result);
			NewNode.InferredType = TEXT("float");
			return NewNode;
		}
	}

	return TOptional<FVexExpressionAst>();
}

TOptional<FVexExpressionAst> FVexOptimizer::SimplifyIdentities(const FVexExpressionAst& Node, const FVexProgramAst& Program)
{
	if (Node.Kind != EVexExprKind::BinaryOp) return TOptional<FVexExpressionAst>();

	const FVexExpressionAst* Left = Program.Expressions.IsValidIndex(Node.LeftNodeIndex) ? &Program.Expressions[Node.LeftNodeIndex] : nullptr;
	const FVexExpressionAst* Right = Program.Expressions.IsValidIndex(Node.RightNodeIndex) ? &Program.Expressions[Node.RightNodeIndex] : nullptr;

	if (!Left || !Right) return TOptional<FVexExpressionAst>();

	// x * 1.0 -> x
	if (Node.Lexeme == TEXT("*"))
	{
		if (Right->Kind == EVexExprKind::NumberLiteral && FMath::IsNearlyEqual(FCString::Atof(*Right->Lexeme), 1.0f))
		{
			return *Left;
		}
		if (Left->Kind == EVexExprKind::NumberLiteral && FMath::IsNearlyEqual(FCString::Atof(*Left->Lexeme), 1.0f))
		{
			return *Right;
		}
	}

	// x + 0.0 -> x
	if (Node.Lexeme == TEXT("+"))
	{
		if (Right->Kind == EVexExprKind::NumberLiteral && FMath::IsNearlyZero(FCString::Atof(*Right->Lexeme)))
		{
			return *Left;
		}
		if (Left->Kind == EVexExprKind::NumberLiteral && FMath::IsNearlyZero(FCString::Atof(*Left->Lexeme)))
		{
			return *Right;
		}
	}

	return TOptional<FVexExpressionAst>();
}

} // namespace Flight::Vex
