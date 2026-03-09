// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexRewriteRegistry.h"

#include "Math/UnrealMathUtility.h"

namespace Flight::Vex
{

namespace
{

bool IsDomainEnabled(const EVexRewriteDomain RuleDomain, const EVexRewriteDomain EnabledDomains)
{
	const uint8 RuleBits = static_cast<uint8>(RuleDomain);
	const uint8 EnabledBits = static_cast<uint8>(EnabledDomains);
	return (RuleBits & EnabledBits) != 0;
}

TOptional<FVexExpressionAst> FoldConstantsRule(const FVexExpressionAst& Node, const FVexProgramAst& Program)
{
	if (Node.Kind != EVexExprKind::BinaryOp)
	{
		return TOptional<FVexExpressionAst>();
	}

	const FVexExpressionAst* Left = Program.Expressions.IsValidIndex(Node.LeftNodeIndex) ? &Program.Expressions[Node.LeftNodeIndex] : nullptr;
	const FVexExpressionAst* Right = Program.Expressions.IsValidIndex(Node.RightNodeIndex) ? &Program.Expressions[Node.RightNodeIndex] : nullptr;

	if (!Left || !Right
		|| Left->Kind != EVexExprKind::NumberLiteral
		|| Right->Kind != EVexExprKind::NumberLiteral)
	{
		return TOptional<FVexExpressionAst>();
	}

	const float LeftValue = FCString::Atof(*Left->Lexeme);
	const float RightValue = FCString::Atof(*Right->Lexeme);
	float Result = 0.0f;
	bool bValid = true;

	if (Node.Lexeme == TEXT("+"))
	{
		Result = LeftValue + RightValue;
	}
	else if (Node.Lexeme == TEXT("-"))
	{
		Result = LeftValue - RightValue;
	}
	else if (Node.Lexeme == TEXT("*"))
	{
		Result = LeftValue * RightValue;
	}
	else if (Node.Lexeme == TEXT("/"))
	{
		if (FMath::IsNearlyZero(RightValue))
		{
			bValid = false;
		}
		else
		{
			Result = LeftValue / RightValue;
		}
	}
	else
	{
		bValid = false;
	}

	if (!bValid)
	{
		return TOptional<FVexExpressionAst>();
	}

	FVexExpressionAst NewNode;
	NewNode.Kind = EVexExprKind::NumberLiteral;
	NewNode.Lexeme = FString::SanitizeFloat(Result);
	NewNode.InferredType = TEXT("float");
	return NewNode;
}

TOptional<FVexExpressionAst> SimplifyIdentitiesRule(const FVexExpressionAst& Node, const FVexProgramAst& Program)
{
	if (Node.Kind != EVexExprKind::BinaryOp)
	{
		return TOptional<FVexExpressionAst>();
	}

	const FVexExpressionAst* Left = Program.Expressions.IsValidIndex(Node.LeftNodeIndex) ? &Program.Expressions[Node.LeftNodeIndex] : nullptr;
	const FVexExpressionAst* Right = Program.Expressions.IsValidIndex(Node.RightNodeIndex) ? &Program.Expressions[Node.RightNodeIndex] : nullptr;

	if (!Left || !Right)
	{
		return TOptional<FVexExpressionAst>();
	}

	if (Node.Lexeme == TEXT("*"))
	{
		if (Right->Kind == EVexExprKind::NumberLiteral
			&& FMath::IsNearlyEqual(FCString::Atof(*Right->Lexeme), 1.0f))
		{
			return *Left;
		}
		if (Left->Kind == EVexExprKind::NumberLiteral
			&& FMath::IsNearlyEqual(FCString::Atof(*Left->Lexeme), 1.0f))
		{
			return *Right;
		}
	}

	if (Node.Lexeme == TEXT("+"))
	{
		if (Right->Kind == EVexExprKind::NumberLiteral
			&& FMath::IsNearlyZero(FCString::Atof(*Right->Lexeme)))
		{
			return *Left;
		}
		if (Left->Kind == EVexExprKind::NumberLiteral
			&& FMath::IsNearlyZero(FCString::Atof(*Left->Lexeme)))
		{
			return *Right;
		}
	}

	return TOptional<FVexExpressionAst>();
}

} // namespace

FVexAstRewriteRegistry FVexAstRewriteRegistry::MakeDefault()
{
	FVexAstRewriteRegistry Registry;

	FVexAstRewriteRule FoldConstants;
	FoldConstants.Name = TEXT("Core.FoldConstants");
	FoldConstants.Domain = EVexRewriteDomain::Core;
	FoldConstants.Rule = [](const FVexExpressionAst& Node, const FVexProgramAst& Program)
	{
		return FoldConstantsRule(Node, Program);
	};
	Registry.AddRule(MoveTemp(FoldConstants));

	FVexAstRewriteRule SimplifyIdentities;
	SimplifyIdentities.Name = TEXT("Core.SimplifyIdentities");
	SimplifyIdentities.Domain = EVexRewriteDomain::Core;
	SimplifyIdentities.Rule = [](const FVexExpressionAst& Node, const FVexProgramAst& Program)
	{
		return SimplifyIdentitiesRule(Node, Program);
	};
	Registry.AddRule(MoveTemp(SimplifyIdentities));

	return Registry;
}

void FVexAstRewriteRegistry::AddRule(FVexAstRewriteRule Rule)
{
	Rules.Add(MoveTemp(Rule));
}

bool FVexAstRewriteRegistry::ApplyFirstMatch(
	const FVexExpressionAst& Node,
	const FVexProgramAst& Program,
	const EVexRewriteDomain EnabledDomains,
	FVexExpressionAst& OutReplacement,
	FString* OutRuleName) const
{
	for (const FVexAstRewriteRule& Rule : Rules)
	{
		if (!IsDomainEnabled(Rule.Domain, EnabledDomains))
		{
			continue;
		}
		if (!Rule.Rule)
		{
			continue;
		}

		if (TOptional<FVexExpressionAst> Replacement = Rule.Rule(Node, Program))
		{
			OutReplacement = MoveTemp(*Replacement);
			if (OutRuleName)
			{
				*OutRuleName = Rule.Name;
			}
			return true;
		}
	}

	return false;
}

void FVexIrRewriteRegistry::AddRule(FVexIrRewriteRule Rule)
{
	Rules.Add(MoveTemp(Rule));
}

bool FVexIrRewriteRegistry::ApplyFirstMatch(
	const FVexIrInstruction& Instruction,
	const FVexIrProgram& Program,
	const int32 InstructionIndex,
	const EVexRewriteDomain EnabledDomains,
	FVexIrInstruction& OutReplacement,
	FString* OutRuleName) const
{
	for (const FVexIrRewriteRule& Rule : Rules)
	{
		if (!IsDomainEnabled(Rule.Domain, EnabledDomains))
		{
			continue;
		}
		if (!Rule.Rule)
		{
			continue;
		}

		if (TOptional<FVexIrInstruction> Replacement = Rule.Rule(Instruction, Program, InstructionIndex))
		{
			OutReplacement = MoveTemp(*Replacement);
			if (OutRuleName)
			{
				*OutRuleName = Rule.Name;
			}
			return true;
		}
	}

	return false;
}

} // namespace Flight::Vex
