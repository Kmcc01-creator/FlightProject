// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexIr.h"
#include "Vex/FlightVexTypes.h"

namespace Flight::Vex
{

TSharedPtr<FVexIrProgram> FVexIrCompiler::Compile(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, FString& OutErrors)
{
	TSharedPtr<FVexIrProgram> Ir = MakeShared<FVexIrProgram>();
	
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
		if (NodeIdx == INDEX_NONE) return FVexIrValue();
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
			FVexIrValue L = CompileExpr(Node.LeftNodeIndex);
			FVexIrValue R = CompileExpr(Node.RightNodeIndex);
			
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
				Inst.Args.Add(CompileExpr(ArgIdx));
			}

			if (Node.Lexeme == TEXT("normalize")) Inst.Op = EVexIrOp::Normalize;
			else if (Node.Lexeme == TEXT("sin")) Inst.Op = EVexIrOp::Sin;
			else if (Node.Lexeme == TEXT("cos")) Inst.Op = EVexIrOp::Cos;
			// ... mapping more functions
			
			Ir->Instructions.Add(Inst);
			return Inst.Dest;
		}
		default: return FVexIrValue();
		}
	};

	for (const auto& Stmt : Program.Statements)
	{
		if (Stmt.Kind == EVexStatementKind::Assignment)
		{
			// Find target (Simplified for Phase 3 scaffold)
			const FVexToken& TargetToken = Program.Tokens[Stmt.StartTokenIndex];
			FVexIrValue Rhs = CompileExpr(Stmt.ExpressionNodeIndex);

			if (TargetToken.Kind == EVexTokenKind::Symbol)
			{
				FVexIrInstruction Store;
				Store.Op = EVexIrOp::StoreSymbol;
				Store.Args.Add(Rhs);
				Store.Dest.Kind = FVexIrValue::EKind::Symbol;
				Store.Dest.Index = GetSymbolIndex(TargetToken.Lexeme);
				Store.SourceTokenIndex = Stmt.StartTokenIndex;
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

	Ir->MaxRegisters = NextReg;
	return Ir;
}

} // namespace Flight::Vex
