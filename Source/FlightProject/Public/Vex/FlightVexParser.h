// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexTypes.h"
#include "Schema/FlightRequirementSchema.h"

namespace Flight::Schema
{
struct FManifestData;
}

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

enum class EVexTokenKind : uint8
{
	Identifier,
	Symbol,
	TargetDirective, // @cpu, @gpu, @job, @thread, @async, @rate
	Number,
	KeywordIf,
	KeywordElse,
	Operator,
	LParen,
	RParen,
	LBrace,
	RBrace,
	Comma,
	Dot,
	Semicolon,
	EndOfFile
};

enum class EVexIssueSeverity : uint8
{
	Warning,
	Error
};

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
	Pipe
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

struct FVexToken
{
	EVexTokenKind Kind = EVexTokenKind::Identifier;
	FString Lexeme;
	int32 Line = 1;
	int32 Column = 1;
};

struct FVexIssue
{
	EVexIssueSeverity Severity = EVexIssueSeverity::Error;
	FString Message;
	int32 Line = 1;
	int32 Column = 1;
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

	// ========================================================================
	// m2-inspired Tree Trait Integration
	// ========================================================================
	
	/**
	 * Get indices of child expression nodes.
	 * This enables recursive/iterative traversal without deep C++ recursion.
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
	 * Replaces recursive 'transform' with a stack-based approach to avoid overflow.
	 */
	template<typename F>
	void TransformIterative(F&& Func)
	{
		if (Expressions.Num() == 0) return;

		// Post-order traversal via two stacks
		TArray<int32> Stack1;
		TArray<int32> Stack2;

		// We process each statement's expression root
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

		// Apply function to nodes in post-order (bottom-up)
		while (Stack2.Num() > 0)
		{
			int32 NodeIndex = Stack2.Pop();
			Func(Expressions[NodeIndex], NodeIndex);
		}
	}
};

struct FVexSymbolDefinition
{
	FString SymbolName;
	FString ValueType;
	EFlightVexSymbolResidency Residency = EFlightVexSymbolResidency::Shared;
	EFlightVexSymbolAffinity Affinity = EFlightVexSymbolAffinity::Any;
	bool bWritable = true;
	bool bRequired = true;
};

struct FVexParseResult
{
	bool bSuccess = false;
	FVexProgramAst Program;
	TArray<FVexIssue> Issues;
	TArray<FString> UnknownSymbols;
	TArray<FString> MissingRequiredSymbols;

	/** Bitmask of SCSL domains required by this script */
	EVexRequirement Requirements = EVexRequirement::None;
};

/** Build parser symbol definitions from schema manifest rows. */
TArray<FVexSymbolDefinition> BuildSymbolDefinitionsFromManifest(const Flight::Schema::FManifestData& ManifestData);

/** Tokenize source and build a minimal statement-oriented AST. */
FVexParseResult ParseAndValidate(
	const FString& Source,
	const TArray<FVexSymbolDefinition>& SymbolDefinitions,
	bool bRequireAllRequiredSymbols = false);

/** Human-readable multi-line formatter for parse/validation society. */
FString FormatIssues(const TArray<FVexIssue>& Issues);

/** Lower the validated AST to HLSL source code. */
FString LowerToHLSL(const FVexProgramAst& Program, const TMap<FString, FString>& HlslBySymbol);

/** Lower the validated AST to Verse source code. */
FString LowerToVerse(const FVexProgramAst& Program, const TMap<FString, FString>& VerseBySymbol);

/** Generate a unified HLSL Mega-Kernel from multiple behavior scripts with hoisting optimizations. */
FString LowerMegaKernel(const TMap<uint32, FVexProgramAst>& Scripts, const TMap<FString, FString>& SymbolMap);

} // namespace Flight::Vex
