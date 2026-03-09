// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/Frontend/VexToken.h"

namespace Flight::Vex
{

enum class EVexRequirement : uint32
{
	None            = 0,
	SwarmState      = 1 << 0, // Accesses @position, @velocity, etc.
	LightLattice    = 1 << 1, // Uses sample_lattice or inject_light
	VolumetricCloud = 1 << 2, // Uses sample_cloud or inject_cloud
	StructureSDF    = 1 << 3, // Uses sample_sdf
	WriteState      = 1 << 4, // Modifies a writable attribute
	Async           = 1 << 5  // Uses @async or suspendable calls
};
ENUM_CLASS_FLAGS(EVexRequirement);

enum class EVexStatementKind : uint8
{
	Expression,
	Assignment,
	IfHeader,
	BlockOpen,
	BlockClose,
	TargetDirective
};

enum class EVexExprKind : uint8
{
	Unknown,
	NumberLiteral,
	Identifier,
	SymbolRef,
	UnaryOp,
	BinaryOp,
	FunctionCall,
	Pipe,
	PipeIn,  // Host <| Device (Control/Inject)
	PipeOut  // Device |> Host (Perception/Extract)
};

/** Descriptor for a concurrent task block capture group. */
struct FVexTaskDescriptor
{
	/** Directive lexeme: @job, @thread, @async */
	FString Type;
	
	/** Token range for the task block */
	int32 StartTokenIndex = INDEX_NONE;
	int32 EndTokenIndex = INDEX_NONE;

	/** Symbols read within this task */
	TSet<FString> ReadSymbols;

	/** Symbols written within this task */
	TSet<FString> WriteSymbols;

	/** Scheduling priority if applicable */
	int32 Priority = 0;
};

struct FVexStatementAst
{
	EVexStatementKind Kind = EVexStatementKind::Expression;
	int32 StartTokenIndex = INDEX_NONE;
	int32 EndTokenIndex = INDEX_NONE;
	int32 ExpressionNodeIndex = INDEX_NONE; // root node for statement condition/expression/RHS
	FString SourceSpan;
};

struct FVexExpressionAst
{
	EVexExprKind Kind = EVexExprKind::Unknown;
	int32 TokenIndex = INDEX_NONE;
	FString Lexeme;
	int32 LeftNodeIndex = INDEX_NONE;
	int32 RightNodeIndex = INDEX_NONE;
	TArray<int32> ArgumentNodeIndices;
	FString InferredType = TEXT("unknown");
};

struct FVexProgramAst
{
	TArray<FVexToken> Tokens;
	TArray<FVexStatementAst> Statements;
	TArray<FVexExpressionAst> Expressions;
	TArray<FString> UsedSymbols;

	/** Concurrent task boundaries found in the program */
	TArray<FVexTaskDescriptor> Tasks;

	/** Execution frequency control from @rate directives */
	float ExecutionRateHz = 0.0f; // 0 = every frame
	uint32 FrameInterval = 1;     // e.g. 10 = every 10th frame

	/**
	 * Get indices of child expression nodes.
	 */
	TArray<int32> GetChildren(const int32 NodeIndex) const
	{
		TArray<int32> Children;
		if (Expressions.IsValidIndex(NodeIndex))
		{
			const FVexExpressionAst& Node = Expressions[NodeIndex];
			if (Node.LeftNodeIndex != INDEX_NONE) Children.Add(Node.LeftNodeIndex);
			if (Node.RightNodeIndex != INDEX_NONE) Children.Add(Node.RightNodeIndex);
			Children.Append(Node.ArgumentNodeIndices);
		}
		return Children;
	}

	/**
	 * Iterative Bottom-Up Transformation.
	 */
	template<typename F>
	void TransformIterative(F&& Func)
	{
		if (Expressions.Num() == 0) return;

		TArray<int32> Stack1;
		TArray<int32> Stack2;

		for (const FVexStatementAst& Stmt : Statements)
		{
			if (Stmt.ExpressionNodeIndex != INDEX_NONE)
			{
				Stack1.Push(Stmt.ExpressionNodeIndex);
			}
		}

		while (Stack1.Num() > 0)
		{
			int32 NodeIndex = Stack1.Pop();
			Stack2.Push(NodeIndex);

			for (int32 Child : GetChildren(NodeIndex))
			{
				Stack1.Push(Child);
			}
		}

		while (Stack2.Num() > 0)
		{
			int32 NodeIndex = Stack2.Pop();
			Func(Expressions[NodeIndex], NodeIndex);
		}
	}
};

} // namespace Flight::Vex
