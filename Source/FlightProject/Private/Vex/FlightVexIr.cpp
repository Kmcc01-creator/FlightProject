// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexIr.h"
#include "Vex/FlightVexTypes.h"

namespace Flight::Vex
{

TSharedPtr<FVexIrProgram> FVexIrCompiler::Compile(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, FString& OutErrors)
{
	(void)SymbolDefinitions;
	TSharedPtr<FVexIrProgram> Ir = MakeShared<FVexIrProgram>();

	auto AppendError = [&](const FString& Message)
	{
		if (!OutErrors.IsEmpty())
		{
			OutErrors += TEXT("\n");
		}
		OutErrors += Message;
	};

	auto MakeInvalidValue = [](const EVexValueType Type = EVexValueType::Unknown)
	{
		FVexIrValue Invalid;
		Invalid.Kind = FVexIrValue::EKind::Register;
		Invalid.Type = Type;
		Invalid.Index = INDEX_NONE;
		return Invalid;
	};
	
	TMap<FString, int32> SymbolToIndex;
	auto GetSymbolIndex = [&](const FString& Name)
	{
		if (int32* Existing = SymbolToIndex.Find(Name)) return *Existing;
		int32 Idx = Ir->SymbolNames.Add(Name);
		SymbolToIndex.Add(Name, Idx);
		return Idx;
	};

	TMap<FString, int32> LocalToReg;
	int32 NextReg = 0;
	auto GetLocalReg = [&](const FString& Name)
	{
		if (int32* Existing = LocalToReg.Find(Name)) return *Existing;
		int32 Reg = NextReg++;
		LocalToReg.Add(Name, Reg);
		return Reg;
	};

	TFunction<FVexIrValue(int32)> CompileExpr = [&](int32 NodeIdx) -> FVexIrValue
	{
		if (NodeIdx == INDEX_NONE || !Program.Expressions.IsValidIndex(NodeIdx)) return MakeInvalidValue();
		const auto& Node = Program.Expressions[NodeIdx];

		FVexIrValue Val;
		Val.Type = ParseValueType(Node.InferredType);

		switch (Node.Kind)
		{
		case EVexExprKind::NumberLiteral:
		{
			Val.Kind = FVexIrValue::EKind::Constant;
			Val.Index = Ir->Constants.Add(FCString::Atof(*Node.Lexeme));
			return Val;
		}
		case EVexExprKind::SymbolRef:
		{
			Val.Kind = FVexIrValue::EKind::Symbol;
			Val.Index = GetSymbolIndex(Node.Lexeme);
			
			// Load to temp reg for processing
			FVexIrInstruction Load;
			Load.Op = EVexIrOp::LoadSymbol;
			Load.Dest.Kind = FVexIrValue::EKind::Register;
			Load.Dest.Type = Val.Type;
			Load.Dest.Index = NextReg++;
			Load.Args.Add(Val);
			Load.SourceTokenIndex = Node.TokenIndex;
			Ir->Instructions.Add(Load);
			return Load.Dest;
		}
		case EVexExprKind::Identifier:
		{
			Val.Kind = FVexIrValue::EKind::Register;
			Val.Index = GetLocalReg(Node.Lexeme);
			return Val;
		}
		case EVexExprKind::BinaryOp:
		{
			if (Node.Lexeme == TEXT("."))
			{
				const FVexIrValue Base = CompileExpr(Node.LeftNodeIndex);
				if (!Program.Expressions.IsValidIndex(Node.RightNodeIndex))
				{
					AppendError(TEXT("IR lowering failed: malformed member access."));
					return Base;
				}

				const FVexExpressionAst& SelectorNode = Program.Expressions[Node.RightNodeIndex];
				if (SelectorNode.Kind != EVexExprKind::Identifier)
				{
					AppendError(TEXT("IR lowering failed: member selector must be an identifier."));
					return Base;
				}

				// Current SIMD scaffold is scalarized to X; preserve `.x`/`.r` access.
				if (SelectorNode.Lexeme == TEXT("x") || SelectorNode.Lexeme == TEXT("r"))
				{
					return Base;
				}

				AppendError(FString::Printf(TEXT("IR lowering failed: unsupported component selector '.%s'."), *SelectorNode.Lexeme));
				return Base;
			}

			FVexIrValue L = CompileExpr(Node.LeftNodeIndex);
			FVexIrValue R = CompileExpr(Node.RightNodeIndex);
			if (L.Index == INDEX_NONE || R.Index == INDEX_NONE)
			{
				return MakeInvalidValue(Val.Type);
			}
			
			FVexIrInstruction Inst;
			Inst.Dest.Kind = FVexIrValue::EKind::Register;
			Inst.Dest.Type = Val.Type;
			Inst.Dest.Index = NextReg++;
			Inst.Args.Add(L);
			Inst.Args.Add(R);
			Inst.SourceTokenIndex = Node.TokenIndex;

			if (Node.Lexeme == TEXT("+")) Inst.Op = EVexIrOp::Add;
			else if (Node.Lexeme == TEXT("-")) Inst.Op = EVexIrOp::Sub;
			else if (Node.Lexeme == TEXT("*")) Inst.Op = EVexIrOp::Mul;
			else if (Node.Lexeme == TEXT("/")) Inst.Op = EVexIrOp::Div;
			else if (Node.Lexeme == TEXT("<")) Inst.Op = EVexIrOp::Less;
			else if (Node.Lexeme == TEXT(">")) Inst.Op = EVexIrOp::Greater;
			else if (Node.Lexeme == TEXT("<=")) Inst.Op = EVexIrOp::LessEqual;
			else if (Node.Lexeme == TEXT(">=")) Inst.Op = EVexIrOp::GreaterEqual;
			else if (Node.Lexeme == TEXT("==")) Inst.Op = EVexIrOp::Equal;
			else if (Node.Lexeme == TEXT("!=")) Inst.Op = EVexIrOp::NotEqual;
			else
			{
				AppendError(FString::Printf(TEXT("IR lowering failed: unsupported binary operator '%s'."), *Node.Lexeme));
				return MakeInvalidValue(Val.Type);
			}
			
			Ir->Instructions.Add(Inst);
			return Inst.Dest;
		}
		case EVexExprKind::FunctionCall:
		{
			FVexIrInstruction Inst;
			Inst.Dest.Kind = FVexIrValue::EKind::Register;
			Inst.Dest.Type = Val.Type;
			Inst.Dest.Index = NextReg++;
			Inst.SourceTokenIndex = Node.TokenIndex;

			for (int32 ArgIdx : Node.ArgumentNodeIndices)
			{
				const FVexIrValue Arg = CompileExpr(ArgIdx);
				if (Arg.Index == INDEX_NONE)
				{
					return MakeInvalidValue(Val.Type);
				}
				Inst.Args.Add(Arg);
			}

			if (Node.Lexeme == TEXT("normalize")) Inst.Op = EVexIrOp::Normalize;
			else if (Node.Lexeme == TEXT("sin")) Inst.Op = EVexIrOp::Sin;
			else if (Node.Lexeme == TEXT("cos")) Inst.Op = EVexIrOp::Cos;
			else if (Node.Lexeme == TEXT("exp")) Inst.Op = EVexIrOp::Exp;
			else if (Node.Lexeme == TEXT("log")) Inst.Op = EVexIrOp::Log;
			else if (Node.Lexeme == TEXT("pow")) Inst.Op = EVexIrOp::Pow;
			else
			{
				AppendError(FString::Printf(TEXT("IR lowering failed: unsupported function '%s'."), *Node.Lexeme));
				return MakeInvalidValue(Val.Type);
			}
			
			Ir->Instructions.Add(Inst);
			return Inst.Dest;
		}
		default:
			AppendError(FString::Printf(TEXT("IR lowering failed: unsupported expression kind %d."), static_cast<int32>(Node.Kind)));
			return MakeInvalidValue(Val.Type);
		}
	};

	TArray<int32> JumpPatchStack;

	for (int32 StmtIdx = 0; StmtIdx < Program.Statements.Num(); ++StmtIdx)
	{
		const auto& Stmt = Program.Statements[StmtIdx];
		
		if (Stmt.Kind == EVexStatementKind::IfHeader)
		{
			FVexIrValue Cond = CompileExpr(Stmt.ExpressionNodeIndex);
			if (Cond.Index == INDEX_NONE) continue;

			// Emit JumpIf (inverted) to skip the block if condition is false
			FVexIrInstruction JumpInst;
			JumpInst.Op = EVexIrOp::JumpIf;
			JumpInst.Args.Add(Cond);
			
			// We'll patch the target index (Args[1]) later
			FVexIrValue TargetPlaceholder;
			TargetPlaceholder.Kind = FVexIrValue::EKind::Constant;
			TargetPlaceholder.Index = INDEX_NONE; 
			JumpInst.Args.Add(TargetPlaceholder);
			
			int32 InstIdx = Ir->Instructions.Add(JumpInst);
			JumpPatchStack.Push(InstIdx);
		}
		else if (Stmt.Kind == EVexStatementKind::BlockClose)
		{
			if (JumpPatchStack.Num() > 0)
			{
				int32 JumpInstIdx = JumpPatchStack.Pop();
				// Patch the jump target to the instruction immediately following this block
				// In our IR, this means the current number of instructions
				Ir->Instructions[JumpInstIdx].Args[1].Index = Ir->Instructions.Num();
			}
		}
		else if (Stmt.Kind == EVexStatementKind::Assignment)
		{
			if (!Program.Tokens.IsValidIndex(Stmt.StartTokenIndex))
			{
				continue;
			}

			int32 TargetTokenIndex = Stmt.StartTokenIndex;
			if (Program.Tokens.IsValidIndex(Stmt.StartTokenIndex + 1))
			{
				const FVexToken& MaybeTypeToken = Program.Tokens[Stmt.StartTokenIndex];
				const FVexToken& MaybeNameToken = Program.Tokens[Stmt.StartTokenIndex + 1];
				if (MaybeTypeToken.Kind == EVexTokenKind::Identifier
					&& MaybeNameToken.Kind == EVexTokenKind::Identifier
					&& (MaybeTypeToken.Lexeme == TEXT("float")
						|| MaybeTypeToken.Lexeme == TEXT("float2")
						|| MaybeTypeToken.Lexeme == TEXT("float3")
						|| MaybeTypeToken.Lexeme == TEXT("float4")
						|| MaybeTypeToken.Lexeme == TEXT("int")
						|| MaybeTypeToken.Lexeme == TEXT("bool")))
				{
					TargetTokenIndex = Stmt.StartTokenIndex + 1;
				}
			}

			const FVexToken& TargetToken = Program.Tokens[TargetTokenIndex];
			FVexIrValue Rhs = CompileExpr(Stmt.ExpressionNodeIndex);
			if (Rhs.Index == INDEX_NONE)
			{
				continue;
			}

			if (TargetToken.Kind == EVexTokenKind::Symbol)
			{
				FVexIrInstruction Store;
				Store.Op = EVexIrOp::StoreSymbol;
				Store.Args.Add(Rhs);
				Store.Dest.Kind = FVexIrValue::EKind::Symbol;
				Store.Dest.Index = GetSymbolIndex(TargetToken.Lexeme);
				Store.SourceTokenIndex = TargetTokenIndex;
				Ir->Instructions.Add(Store);
			}
			else if (TargetToken.Kind == EVexTokenKind::Identifier)
			{
				// Copy to local reg
				FVexIrInstruction Copy;
				Copy.Op = EVexIrOp::Add; // Copy implemented as Add zero
				Copy.Dest.Kind = FVexIrValue::EKind::Register;
				Copy.Dest.Index = GetLocalReg(TargetToken.Lexeme);
				Copy.Args.Add(Rhs);
				
				FVexIrValue Zero;
				Zero.Kind = FVexIrValue::EKind::Constant;
				Zero.Index = Ir->Constants.Add(0.0f);
				Copy.Args.Add(Zero);
				
				Ir->Instructions.Add(Copy);
			}
		}
	}

	if (!OutErrors.IsEmpty())
	{
		return nullptr;
	}

	Ir->MaxRegisters = NextReg;
	return Ir;
}

FString FVexIrCompiler::LowerToHLSL(const FVexIrProgram& Program, const TMap<FString, FString>& HlslBySymbol)
{
	FString Source;
	auto GetVal = [&](const FVexIrValue& Val) -> FString
	{
		if (Val.Kind == FVexIrValue::EKind::Constant) return FString::Printf(TEXT("%g"), Program.Constants[Val.Index]);
		if (Val.Kind == FVexIrValue::EKind::Symbol) { const FString& Name = Program.SymbolNames[Val.Index]; return HlslBySymbol.Contains(Name) ? HlslBySymbol[Name] : Name; }
		return FString::Printf(TEXT("v%d"), Val.Index);
	};

	for (int32 i = 0; i < Program.Instructions.Num(); ++i)
	{
		const auto& Inst = Program.Instructions[i];
		const FString DestType = ToTypeString(Inst.Dest.Type);
		switch (Inst.Op)
		{
		case EVexIrOp::LoadSymbol:
			Source += FString::Printf(TEXT("%s v%d = %s;\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::StoreSymbol:
			Source += FString::Printf(TEXT("%s = %s;\n"), *GetVal(Inst.Dest), *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Const:
			Source += FString::Printf(TEXT("%s v%d = %s;\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Add:
			Source += FString::Printf(TEXT("%s v%d = %s + %s;\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Sub:
			Source += FString::Printf(TEXT("%s v%d = %s - %s;\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Mul:
			Source += FString::Printf(TEXT("%s v%d = %s * %s;\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Div:
			Source += FString::Printf(TEXT("%s v%d = %s / %s;\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Less:
			Source += FString::Printf(TEXT("bool v%d = %s < %s;\n"), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Greater:
			Source += FString::Printf(TEXT("bool v%d = %s > %s;\n"), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::LessEqual:
			Source += FString::Printf(TEXT("bool v%d = %s <= %s;\n"), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::GreaterEqual:
			Source += FString::Printf(TEXT("bool v%d = %s >= %s;\n"), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Equal:
			Source += FString::Printf(TEXT("bool v%d = %s == %s;\n"), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::NotEqual:
			Source += FString::Printf(TEXT("bool v%d = %s != %s;\n"), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Normalize:
			Source += FString::Printf(TEXT("%s v%d = normalize(%s);\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Sin:
			Source += FString::Printf(TEXT("%s v%d = sin(%s);\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Cos:
			Source += FString::Printf(TEXT("%s v%d = cos(%s);\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Exp:
			Source += FString::Printf(TEXT("%s v%d = exp(%s);\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Log:
			Source += FString::Printf(TEXT("%s v%d = log(%s);\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Pow:
			Source += FString::Printf(TEXT("%s v%d = pow(%s, %s);\n"), *DestType, Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::JumpIf:
			// inverted jump for block-skipping
			Source += FString::Printf(TEXT("if (!(%s)) goto Label%d;\n"), *GetVal(Inst.Args[0]), Inst.Args[1].Index);
			break;
		default: break;
		}

		// Check if we need a label for this instruction (if anyone jumps here)
		for (int32 j = 0; j < Program.Instructions.Num(); ++j)
		{
			const auto& TargetInst = Program.Instructions[j];
			if (TargetInst.Op == EVexIrOp::JumpIf && TargetInst.Args[1].Index == i + 1)
			{
				Source += FString::Printf(TEXT("Label%d: ;\n"), TargetInst.Args[1].Index);
			}
		}
	}
	return Source;
}

FString FVexIrCompiler::LowerToVerse(const FVexIrProgram& Program, const TMap<FString, FString>& VerseBySymbol)
{
	FString Source;
	int32 Indent = 1;
	auto GetIndent = [&]() { FString S; for (int32 i = 0; i < Indent; ++i) S += TEXT("    "); return S; };

	auto GetVal = [&](const FVexIrValue& Val) -> FString
	{
		if (Val.Kind == FVexIrValue::EKind::Constant) return FString::Printf(TEXT("%g"), Program.Constants[Val.Index]);
		if (Val.Kind == FVexIrValue::EKind::Symbol) { const FString& Name = Program.SymbolNames[Val.Index]; return VerseBySymbol.Contains(Name) ? VerseBySymbol[Name] : Name; }
		return FString::Printf(TEXT("Var%d"), Val.Index);
	};

	TArray<int32> PendingEnds;

	for (int32 InstIdx = 0; InstIdx < Program.Instructions.Num(); ++InstIdx)
	{
		const auto& Inst = Program.Instructions[InstIdx];

		// Close blocks that end here (in reverse order for correct nesting)
		for (int32 i = PendingEnds.Num() - 1; i >= 0; --i)
		{
			if (PendingEnds[i] == InstIdx)
			{
				PendingEnds.RemoveAt(i);
				--Indent;
			}
		}

		const FString DestType = ToTypeString(Inst.Dest.Type);
		switch (Inst.Op)
		{
		case EVexIrOp::LoadSymbol:
			Source += FString::Printf(TEXT("%svar Var%d:%s = %s\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::StoreSymbol:
			Source += FString::Printf(TEXT("%sset %s = %s\n"), *GetIndent(), *GetVal(Inst.Dest), *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::JumpIf:
			Source += FString::Printf(TEXT("%sif (%s = true):\n"), *GetIndent(), *GetVal(Inst.Args[0]));
			PendingEnds.Add(Inst.Args[1].Index);
			++Indent;
			break;
		case EVexIrOp::Add:
			Source += FString::Printf(TEXT("%svar Var%d:%s = %s + %s\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Sub:
			Source += FString::Printf(TEXT("%svar Var%d:%s = %s - %s\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Mul:
			Source += FString::Printf(TEXT("%svar Var%d:%s = %s * %s\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Div:
			Source += FString::Printf(TEXT("%svar Var%d:%s = %s / %s\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Less:
			Source += FString::Printf(TEXT("%svar Var%d:logic = if (%s < %s) then true else false\n"), *GetIndent(), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Greater:
			Source += FString::Printf(TEXT("%svar Var%d:logic = if (%s > %s) then true else false\n"), *GetIndent(), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::LessEqual:
			Source += FString::Printf(TEXT("%svar Var%d:logic = if (%s <= %s) then true else false\n"), *GetIndent(), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::GreaterEqual:
			Source += FString::Printf(TEXT("%svar Var%d:logic = if (%s >= %s) then true else false\n"), *GetIndent(), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Equal:
			Source += FString::Printf(TEXT("%svar Var%d:logic = if (%s = %s) then true else false\n"), *GetIndent(), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::NotEqual:
			Source += FString::Printf(TEXT("%svar Var%d:logic = if (not (%s = %s)) then true else false\n"), *GetIndent(), Inst.Dest.Index, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		case EVexIrOp::Normalize:
			Source += FString::Printf(TEXT("%svar Var%d:%s = normalize(%s)\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Sin:
			Source += FString::Printf(TEXT("%svar Var%d:%s = sin(%s)\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Cos:
			Source += FString::Printf(TEXT("%svar Var%d:%s = cos(%s)\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Exp:
			Source += FString::Printf(TEXT("%svar Var%d:%s = exp(%s)\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Log:
			Source += FString::Printf(TEXT("%svar Var%d:%s = log(%s)\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]));
			break;
		case EVexIrOp::Pow:
			Source += FString::Printf(TEXT("%svar Var%d:%s = pow(%s, %s)\n"), *GetIndent(), Inst.Dest.Index, *DestType, *GetVal(Inst.Args[0]), *GetVal(Inst.Args[1]));
			break;
		default: break;
		}
	}

	// Final block closing
	while (PendingEnds.Num() > 0)
	{
		PendingEnds.Pop();
		--Indent;
	}

	return Source;
}

} // namespace Flight::Vex
