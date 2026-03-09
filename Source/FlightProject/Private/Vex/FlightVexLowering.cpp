// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexParser.h"

namespace Flight::Vex
{

namespace
{

bool IsAssignmentOperator(const FString& Op)
{
	return Op == TEXT("=") || Op == TEXT("+=") || Op == TEXT("-=") || Op == TEXT("*=") || Op == TEXT("/=");
}

namespace Lower
{

FString Expression(const int32 NodeIdx, const FVexProgramAst& Program, const TMap<FString, FString>& SymbolMap)
{
	if (NodeIdx == INDEX_NONE)
	{
		return TEXT("");
	}

	const FVexExpressionAst& Node = Program.Expressions[NodeIdx];
	switch (Node.Kind)
	{
	case EVexExprKind::NumberLiteral:
		return Node.Lexeme;
	case EVexExprKind::Identifier:
		return Node.Lexeme;
	case EVexExprKind::SymbolRef:
		return SymbolMap.Contains(Node.Lexeme) ? SymbolMap[Node.Lexeme] : Node.Lexeme;
	case EVexExprKind::UnaryOp:
		return Node.Lexeme + Expression(Node.LeftNodeIndex, Program, SymbolMap);
	case EVexExprKind::BinaryOp:
		if (Node.Lexeme == TEXT("."))
		{
			return FString::Printf(
				TEXT("%s.%s"),
				*Expression(Node.LeftNodeIndex, Program, SymbolMap),
				*Expression(Node.RightNodeIndex, Program, SymbolMap));
		}
		return FString::Printf(
			TEXT("(%s %s %s)"),
			*Expression(Node.LeftNodeIndex, Program, SymbolMap),
			*Node.Lexeme,
			*Expression(Node.RightNodeIndex, Program, SymbolMap));
	case EVexExprKind::FunctionCall:
	{
		TArray<FString> Args;
		Args.Reserve(Node.ArgumentNodeIndices.Num());
		for (const int32 ArgIdx : Node.ArgumentNodeIndices)
		{
			Args.Add(Expression(ArgIdx, Program, SymbolMap));
		}

		if (Node.Lexeme.StartsWith(TEXT("vec")))
		{
			return FString::Printf(TEXT("{%s}"), *FString::Join(Args, TEXT(", ")));
		}

		return FString::Printf(TEXT("%s(%s)"), *Node.Lexeme, *FString::Join(Args, TEXT(", ")));
	}
	case EVexExprKind::Pipe:
	case EVexExprKind::PipeIn:
	case EVexExprKind::PipeOut:
	{
		const FString Piped = Expression(Node.LeftNodeIndex, Program, SymbolMap);
		if (Program.Expressions.IsValidIndex(Node.RightNodeIndex))
		{
			const FVexExpressionAst& RightNode = Program.Expressions[Node.RightNodeIndex];
			if (RightNode.Kind == EVexExprKind::FunctionCall)
			{
				TArray<FString> Args;
				Args.Reserve(RightNode.ArgumentNodeIndices.Num() + 1);
				Args.Add(Piped);
				for (const int32 ArgIdx : RightNode.ArgumentNodeIndices)
				{
					Args.Add(Expression(ArgIdx, Program, SymbolMap));
				}
				return FString::Printf(TEXT("%s(%s)"), *RightNode.Lexeme, *FString::Join(Args, TEXT(", ")));
			}
		}
		return FString::Printf(
			TEXT("%s(%s)"),
			*Expression(Node.RightNodeIndex, Program, SymbolMap),
			*Piped);
	}
	default:
		return TEXT("/* unknown */");
	}
}

FString Program(const FVexProgramAst& ProgramAst, const TMap<FString, FString>& SymbolMap, const bool bVerseStyle)
{
	FString Out;
	int32 IndentLevel = 0;
	auto AddIndent = [&](const int32 Level)
	{
		for (int32 Index = 0; Index < Level; ++Index)
		{
			Out += TEXT("    ");
		}
	};

	for (const FVexStatementAst& Statement : ProgramAst.Statements)
	{
		switch (Statement.Kind)
		{
		case EVexStatementKind::TargetDirective:
			break;
		case EVexStatementKind::BlockOpen:
			if (bVerseStyle)
			{
				Out += TEXT(":\n");
			}
			else
			{
				Out += TEXT("{\n");
			}
			++IndentLevel;
			break;
		case EVexStatementKind::BlockClose:
			--IndentLevel;
			if (!bVerseStyle)
			{
				AddIndent(IndentLevel);
				Out += TEXT("}\n");
			}
			break;
		case EVexStatementKind::IfHeader:
			AddIndent(IndentLevel);
			Out += FString::Printf(
				TEXT("if (%s)%s"),
				*Expression(Statement.ExpressionNodeIndex, ProgramAst, SymbolMap),
				bVerseStyle ? TEXT(":") : TEXT(" "));
			if (!bVerseStyle)
			{
				Out += TEXT("\n");
			}
			break;
		case EVexStatementKind::Assignment:
		{
			AddIndent(IndentLevel);
			FString LeftSide;
			int32 OperatorTokenIndex = INDEX_NONE;
			for (int32 TokenIndex = Statement.StartTokenIndex; TokenIndex <= Statement.EndTokenIndex; ++TokenIndex)
			{
				const FVexToken& Token = ProgramAst.Tokens[TokenIndex];
				if (Token.Kind == EVexTokenKind::Operator && IsAssignmentOperator(Token.Lexeme))
				{
					OperatorTokenIndex = TokenIndex;
					break;
				}
				if (!LeftSide.IsEmpty())
				{
					LeftSide += TEXT(" ");
				}
				LeftSide += Token.Lexeme;
			}

			for (const TPair<FString, FString>& SymbolPair : SymbolMap)
			{
				LeftSide.ReplaceInline(*SymbolPair.Key, *SymbolPair.Value);
			}

			if (bVerseStyle)
			{
				Out += FString::Printf(
					TEXT("set %s %s %s\n"),
					*LeftSide,
					OperatorTokenIndex != INDEX_NONE ? *ProgramAst.Tokens[OperatorTokenIndex].Lexeme : TEXT("="),
					*Expression(Statement.ExpressionNodeIndex, ProgramAst, SymbolMap));
			}
			else
			{
				Out += FString::Printf(
					TEXT("%s %s %s;\n"),
					*LeftSide,
					OperatorTokenIndex != INDEX_NONE ? *ProgramAst.Tokens[OperatorTokenIndex].Lexeme : TEXT("="),
					*Expression(Statement.ExpressionNodeIndex, ProgramAst, SymbolMap));
			}
			break;
		}
		case EVexStatementKind::Expression:
			AddIndent(IndentLevel);
			Out += Expression(Statement.ExpressionNodeIndex, ProgramAst, SymbolMap) + (bVerseStyle ? TEXT("\n") : TEXT(";\n"));
			break;
		}
	}

	return Out;
}

} // namespace Lower

} // namespace

FString LowerToHLSL(const FVexProgramAst& Program, const TMap<FString, FString>& HlslBySymbol)
{
	TMap<FString, FString> Symbols = HlslBySymbol;
	Symbols.Add(TEXT("@dt"), TEXT("DeltaTime"));
	Symbols.Add(TEXT("@frame"), TEXT("FrameIndex"));
	Symbols.Add(TEXT("@time"), TEXT("(FrameIndex * DeltaTime)"));
	return Lower::Program(Program, Symbols, false);
}

FString LowerToVerse(const FVexProgramAst& Program, const TMap<FString, FString>& VerseBySymbol)
{
	TMap<FString, FString> Symbols = VerseBySymbol;
	Symbols.Add(TEXT("@dt"), TEXT("Sim.DeltaTime"));
	Symbols.Add(TEXT("@frame"), TEXT("Sim.Frame"));
	Symbols.Add(TEXT("@time"), TEXT("Sim.TotalTime"));

	bool bIsAsync = false;
	for (const FVexStatementAst& Statement : Program.Statements)
	{
		if (Statement.Kind == EVexStatementKind::TargetDirective && Statement.SourceSpan == TEXT("@async"))
		{
			bIsAsync = true;
			break;
		}
	}

	const FString Body = Lower::Program(Program, Symbols, true);
	const FString Effects = bIsAsync ? TEXT("<transacts><async>") : TEXT("<transacts>");
	return FString::Printf(TEXT("ProcessDroid(Droid:droid_state, Sim:sim_context)%s:void =\n%s"), *Effects, *Body);
}

FString LowerMegaKernel(const TMap<uint32, FVexProgramAst>& Scripts, const TMap<FString, FString>& SymbolMap)
{
	auto MakeLocalAlias = [](const FString& SymbolName)
	{
		FString Alias = SymbolName;
		Alias.RemoveFromStart(TEXT("@"));
		return Alias + TEXT("_local");
	};

	TSet<FString> UsedSymbols;
	for (const TPair<uint32, FVexProgramAst>& Pair : Scripts)
	{
		for (const FString& UsedSymbol : Pair.Value.UsedSymbols)
		{
			if (SymbolMap.Contains(UsedSymbol))
			{
				UsedSymbols.Add(UsedSymbol);
			}
		}
	}

	TArray<FString> UsedOrdered = UsedSymbols.Array();
	UsedOrdered.Sort();

	TMap<FString, FString> LocalMap = SymbolMap;
	FString Hoist = TEXT("// --- Global Attribute Hoisting ---\n");
	for (const FString& Symbol : UsedOrdered)
	{
		const FString Alias = MakeLocalAlias(Symbol);
		Hoist += FString::Printf(TEXT("auto %s = %s;\n"), *Alias, *SymbolMap.FindChecked(Symbol));
		LocalMap.Add(Symbol, Alias);
	}

	FString Dispatch = TEXT("// --- Behavior Dispatch ---\n");
	Dispatch += TEXT("switch (BehaviorID)\n{\n");
	for (const TPair<uint32, FVexProgramAst>& Pair : Scripts)
	{
		Dispatch += FString::Printf(TEXT("    case %u:\n    {\n"), Pair.Key);
		const FString CaseBody = Lower::Program(Pair.Value, LocalMap, false);
		TArray<FString> BodyLines;
		CaseBody.ParseIntoArrayLines(BodyLines);
		for (const FString& Line : BodyLines)
		{
			if (!Line.IsEmpty())
			{
				Dispatch += TEXT("        ") + Line + TEXT("\n");
			}
		}
		Dispatch += TEXT("        break;\n");
		Dispatch += TEXT("    }\n");
	}
	Dispatch += TEXT("}\n");

	return TEXT("// SCSL Unified Mega-Kernel\n") + Hoist + Dispatch;
}

} // namespace Flight::Vex
