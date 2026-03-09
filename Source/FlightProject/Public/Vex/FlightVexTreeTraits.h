// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexIr.h"

namespace Flight::Vex
{

/**
 * AST tree view for generic non-recursive traversals.
 * Node identity is expression index within FVexProgramAst::Expressions.
 */
struct FVexAstTreeView
{
	explicit FVexAstTreeView(const FVexProgramAst& InProgram)
		: Program(InProgram)
	{
	}

	void GetRootNodeIndices(TArray<int32>& OutRoots) const
	{
		OutRoots.Reset();
		for (const FVexStatementAst& Statement : Program.Statements)
		{
			if (Statement.ExpressionNodeIndex != INDEX_NONE)
			{
				OutRoots.Add(Statement.ExpressionNodeIndex);
			}
		}
	}

	void GetChildren(const int32 NodeIndex, TArray<int32>& OutChildren) const
	{
		OutChildren.Reset();
		if (!Program.Expressions.IsValidIndex(NodeIndex))
		{
			return;
		}

		const FVexExpressionAst& Node = Program.Expressions[NodeIndex];
		if (Node.LeftNodeIndex != INDEX_NONE)
		{
			OutChildren.Add(Node.LeftNodeIndex);
		}
		if (Node.RightNodeIndex != INDEX_NONE)
		{
			OutChildren.Add(Node.RightNodeIndex);
		}
		OutChildren.Append(Node.ArgumentNodeIndices);
	}

	const FVexProgramAst& Program;
};

/**
 * IR tree view over instruction dependencies.
 * Node identity is instruction index within FVexIrProgram::Instructions.
 */
struct FVexIrTreeView
{
	explicit FVexIrTreeView(const FVexIrProgram& InProgram)
		: Program(InProgram)
	{
		BuildRegisterDefs();
	}

	void GetRootNodeIndices(TArray<int32>& OutRoots) const
	{
		OutRoots.Reset();
		for (int32 InstructionIndex = 0; InstructionIndex < Program.Instructions.Num(); ++InstructionIndex)
		{
			const FVexIrInstruction& Instruction = Program.Instructions[InstructionIndex];
			if (Instruction.Op == EVexIrOp::StoreSymbol || Instruction.Op == EVexIrOp::Return)
			{
				OutRoots.Add(InstructionIndex);
			}
		}

		// Fallback for pure computational programs with no side-effect root.
		if (OutRoots.Num() == 0 && Program.Instructions.Num() > 0)
		{
			OutRoots.Add(Program.Instructions.Num() - 1);
		}
	}

	void GetChildren(const int32 NodeIndex, TArray<int32>& OutChildren) const
	{
		OutChildren.Reset();
		if (!Program.Instructions.IsValidIndex(NodeIndex))
		{
			return;
		}

		const FVexIrInstruction& Instruction = Program.Instructions[NodeIndex];
		for (const FVexIrValue& Arg : Instruction.Args)
		{
			if (Arg.Kind != FVexIrValue::EKind::Register)
			{
				continue;
			}

			if (!RegisterDefs.IsValidIndex(Arg.Index))
			{
				continue;
			}

			const int32 DefInstruction = RegisterDefs[Arg.Index];
			if (DefInstruction != INDEX_NONE)
			{
				OutChildren.Add(DefInstruction);
			}
		}
	}

	const FVexIrProgram& Program;
	TArray<int32> RegisterDefs;

private:
	void BuildRegisterDefs()
	{
		int32 MaxRegisterIndex = INDEX_NONE;
		for (const FVexIrInstruction& Instruction : Program.Instructions)
		{
			if (Instruction.Dest.Kind == FVexIrValue::EKind::Register)
			{
				MaxRegisterIndex = FMath::Max(MaxRegisterIndex, Instruction.Dest.Index);
			}
		}

		if (MaxRegisterIndex == INDEX_NONE)
		{
			return;
		}

		RegisterDefs.Init(INDEX_NONE, MaxRegisterIndex + 1);
		for (int32 InstructionIndex = 0; InstructionIndex < Program.Instructions.Num(); ++InstructionIndex)
		{
			const FVexIrInstruction& Instruction = Program.Instructions[InstructionIndex];
			if (Instruction.Dest.Kind == FVexIrValue::EKind::Register
				&& RegisterDefs.IsValidIndex(Instruction.Dest.Index))
			{
				RegisterDefs[Instruction.Dest.Index] = InstructionIndex;
			}
		}
	}
};

/**
 * Generic iterative post-order traversal over AST/IR tree views.
 * Visitor signature: void(int32 NodeIndex)
 */
template<typename TTreeView, typename FVisitor>
void TraversePostOrder(const TTreeView& TreeView, FVisitor&& Visitor)
{
	TArray<int32> Roots;
	TreeView.GetRootNodeIndices(Roots);
	if (Roots.Num() == 0)
	{
		return;
	}

	TArray<int32> Stack1;
	TArray<int32> Stack2;
	TSet<int32> Seen;

	Stack1.Reserve(Roots.Num());
	for (const int32 Root : Roots)
	{
		if (Root != INDEX_NONE && !Seen.Contains(Root))
		{
			Seen.Add(Root);
			Stack1.Push(Root);
		}
	}

	TArray<int32> ChildrenScratch;
	while (Stack1.Num() > 0)
	{
		const int32 NodeIndex = Stack1.Pop(EAllowShrinking::No);
		Stack2.Push(NodeIndex);

		TreeView.GetChildren(NodeIndex, ChildrenScratch);
		for (const int32 Child : ChildrenScratch)
		{
			if (Child != INDEX_NONE && !Seen.Contains(Child))
			{
				Seen.Add(Child);
				Stack1.Push(Child);
			}
		}
	}

	while (Stack2.Num() > 0)
	{
		const int32 NodeIndex = Stack2.Pop(EAllowShrinking::No);
		Visitor(NodeIndex);
	}
}

} // namespace Flight::Vex
