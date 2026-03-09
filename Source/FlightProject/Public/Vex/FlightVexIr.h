// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexParser.h"
#include "Vex/FlightVexTypes.h"

namespace Flight::Vex
{

/**
 * EVexIrOp: Low-level IR operations shared by SIMD and HLSL paths.
 */
enum class EVexIrOp : uint8
{
	LoadSymbol,
	StoreSymbol,
	Const,
	Add, Sub, Mul, Div,
	Normalize,
	Sin, Cos, Log, Exp, Pow,
	VectorCompose, // compose vec2/3/4
	JumpIf,        // For future Tier 2/3 IR support
	Return
};

/**
 * FVexIrValue: Typed value in the IR (Register or Constant)
 */
struct FVexIrValue
{
	enum class EKind : uint8 { Register, Constant, Symbol };
	EKind Kind = EKind::Register;
	EVexValueType Type = EVexValueType::Float;
	int32 Index = INDEX_NONE; // Register index, Symbol name index, or Constant index
};

/**
 * FVexIrInstruction: Single IR operation
 */
struct FVexIrInstruction
{
	EVexIrOp Op = EVexIrOp::Return;
	FVexIrValue Dest;
	TArray<FVexIrValue> Args;
	int32 SourceTokenIndex = INDEX_NONE; // For diagnostic mapping
};

/**
 * FVexIrProgram: Typed IR representation of a VEX program.
 */
struct FVexIrProgram
{
	TArray<FVexIrInstruction> Instructions;
	TArray<float> Constants;
	TArray<FString> SymbolNames;
	int32 MaxRegisters = 0;
};

/**
 * FVexIrCompiler: Builds IR from AST.
 */
class FLIGHTPROJECT_API FVexIrCompiler
{
public:
	static TSharedPtr<FVexIrProgram> Compile(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, FString& OutErrors);
};

} // namespace Flight::Vex
