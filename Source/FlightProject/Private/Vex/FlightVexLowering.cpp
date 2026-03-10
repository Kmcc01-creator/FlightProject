// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/Frontend/VexParser.h"
#include "Vex/FlightVexIr.h"

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

FString LowerToHLSL(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, const TMap<FString, FString>& HlslBySymbol)
{
	FString Errors;
	TSharedPtr<FVexIrProgram> Ir = FVexIrCompiler::Compile(Program, SymbolDefinitions, Errors);
	if (Ir.IsValid())
	{
		TMap<FString, FString> Symbols = HlslBySymbol;
		Symbols.Add(TEXT("@dt"), TEXT("DeltaTime"));
		Symbols.Add(TEXT("@frame"), TEXT("FrameIndex"));
		Symbols.Add(TEXT("@time"), TEXT("(FrameIndex * DeltaTime)"));
		return FVexIrCompiler::LowerToHLSL(*Ir, Symbols);
	}
	return FString::Printf(TEXT("// IR Compile Failed: %s\n"), *Errors);
}

FString LowerToVerse(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, const TMap<FString, FString>& VerseBySymbol)
{
	FString Errors;
	TSharedPtr<FVexIrProgram> Ir = FVexIrCompiler::Compile(Program, SymbolDefinitions, Errors);
	
	FString Body;
	if (Ir.IsValid())
	{
		TMap<FString, FString> Symbols = VerseBySymbol;
		Symbols.Add(TEXT("@dt"), TEXT("Sim.DeltaTime"));
		Symbols.Add(TEXT("@frame"), TEXT("Sim.Frame"));
		Symbols.Add(TEXT("@time"), TEXT("Sim.TotalTime"));
		Body = FVexIrCompiler::LowerToVerse(*Ir, Symbols);
	}
	else
	{
		Body = FString::Printf(TEXT("    # IR Compile Failed: %s\n"), *Errors);
	}

	bool bIsAsync = false;
	for (const FVexStatementAst& Statement : Program.Statements)
	{
		if (Statement.Kind == EVexStatementKind::TargetDirective && Statement.SourceSpan == TEXT("@async"))
		{
			bIsAsync = true;
			break;
		}
	}

	const FString Effects = bIsAsync ? TEXT("<transacts><async>") : TEXT("<transacts>");
	return FString::Printf(TEXT("ProcessDroid(Droid:droid_state, Sim:sim_context)%s:void =\n%s"), *Effects, *Body);
}

FString LowerMegaKernel(const TMap<uint32, FVexProgramAst>& Scripts, const TArray<FVexSymbolDefinition>& SymbolDefinitions, const TMap<FString, FString>& SymbolMap, const FString& FunctionName)
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

	// Function Header with the stable Call Contract
	FString Source = TEXT("// Automatically generated by UFlightMegaKernelSubsystem\n");
	Source += TEXT("#define VEX_MEGA_KERNEL_DEFINED\n\n");
	Source += FString::Printf(TEXT("void %s(uint BehaviorID, inout float3 pos, inout float3 vel, inout float shield, inout uint status, float dt, uint frame)\n{\n"), *FunctionName);

	// LocalMap is the source of truth for the GPU Mega-Kernel (aliasing globals/params to locals)
	TMap<FString, FString> LocalMap = SymbolMap;
	
	Source += TEXT("    // --- Global Attribute Hoisting ---\n");
	for (const FString& Symbol : UsedOrdered)
	{
		const FString Alias = MakeLocalAlias(Symbol);
		Source += FString::Printf(TEXT("    auto %s = %s;\n"), *Alias, *SymbolMap.FindChecked(Symbol));
		LocalMap.Add(Symbol, Alias);
	}

	FString Dispatch = TEXT("\n    // --- Behavior Dispatch ---\n");
	Dispatch += TEXT("    switch (BehaviorID)\n    {\n");
	for (const TPair<uint32, FVexProgramAst>& Pair : Scripts)
	{
		Dispatch += FString::Printf(TEXT("        case %u:\n        {\n"), Pair.Key);
		
		FString Errors;
		TSharedPtr<FVexIrProgram> Ir = FVexIrCompiler::Compile(Pair.Value, SymbolDefinitions, Errors);
		if (Ir.IsValid())
		{
			const FString CaseBody = FVexIrCompiler::LowerToHLSL(*Ir, LocalMap);
			
			TArray<FString> BodyLines;
			CaseBody.ParseIntoArrayLines(BodyLines);
			for (const FString& Line : BodyLines)
			{
				if (!Line.IsEmpty())
				{
					Dispatch += TEXT("            ") + Line + TEXT("\n");
				}
			}
		}
		else
		{
			Dispatch += FString::Printf(TEXT("            // IR Compile Failed: %s\n"), *Errors);
		}
		
		Dispatch += TEXT("            break;\n");
		Dispatch += TEXT("        }\n");
	}
	Dispatch += TEXT("    }\n");

	Source += Dispatch;

	// Store-back section: Reconcile local aliases back to the inout parameters
	Source += TEXT("\n    // --- Attribute Store-back ---\n");
	if (UsedSymbols.Contains(TEXT("@position"))) Source += TEXT("    pos = position_local;\n");
	if (UsedSymbols.Contains(TEXT("@velocity"))) Source += TEXT("    vel = velocity_local;\n");
	if (UsedSymbols.Contains(TEXT("@shield")))   Source += TEXT("    shield = shield_local;\n");
	if (UsedSymbols.Contains(TEXT("@status")))   Source += TEXT("    status = (uint)status_local;\n");

	Source += TEXT("}\n");

	return Source;
}

} // namespace Flight::Vex
