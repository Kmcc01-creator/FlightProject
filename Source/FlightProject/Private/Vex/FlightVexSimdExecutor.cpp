// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexSimdExecutor.h"
#include "Mass/FlightMassFragments.h"
#include "Vex/FlightVexSchemaIr.h"

#if (defined(__clang__) || defined(__GNUC__)) && (defined(__x86_64__) || defined(_M_X64))
#include <immintrin.h>
#define FLIGHT_VEX_HAS_EXPLICIT_AVX256X8 1
#define FLIGHT_VEX_TARGET_AVX2 __attribute__((target("avx2,fma")))
#else
#define FLIGHT_VEX_HAS_EXPLICIT_AVX256X8 0
#define FLIGHT_VEX_TARGET_AVX2
#endif

namespace Flight::Vex
{

namespace
{

struct FAvxRegister
{
	float Values[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

bool IsSupportedReadSymbol(const FString& SymbolName)
{
	return SymbolName == TEXT("@position") || SymbolName == TEXT("@shield");
}

bool IsSupportedWriteSymbol(const FString& SymbolName)
{
	// Current SIMD backend only supports scalar shield writes.
	return SymbolName == TEXT("@shield");
}

bool IsSupportedAvxReadSymbol(const FString& SymbolName)
{
	return SymbolName == TEXT("@position") || SymbolName == TEXT("@shield");
}

bool IsSupportedAvxWriteSymbol(const FString& SymbolName)
{
	return SymbolName == TEXT("@shield");
}

bool IsSupportedAvxDirectReadSymbol(const FString& SymbolName)
{
	return SymbolName == TEXT("@position") || SymbolName == TEXT("@shield") || SymbolName == TEXT("@energy");
}

bool IsSupportedAvxDirectWriteSymbol(const FString& SymbolName)
{
	return SymbolName == TEXT("@shield") || SymbolName == TEXT("@energy");
}

bool IsDeclarationTypeToken(const FString& Lexeme)
{
	return Lexeme == TEXT("float")
		|| Lexeme == TEXT("float2")
		|| Lexeme == TEXT("float3")
		|| Lexeme == TEXT("float4")
		|| Lexeme == TEXT("int")
		|| Lexeme == TEXT("bool");
}

bool IsSupportedSimdFunction(const FVexExpressionAst& Node)
{
	return Node.Lexeme == TEXT("sin") && Node.ArgumentNodeIndices.Num() == 1;
}

bool IsSupportedExpressionNode(const FVexExpressionAst& Node)
{
	switch (Node.Kind)
	{
	case EVexExprKind::NumberLiteral:
	case EVexExprKind::Identifier:
	case EVexExprKind::SymbolRef:
		return true;
	case EVexExprKind::BinaryOp:
		return Node.Lexeme == TEXT("+")
			|| Node.Lexeme == TEXT("-")
			|| Node.Lexeme == TEXT("*")
			|| Node.Lexeme == TEXT("/");
	case EVexExprKind::FunctionCall:
		return IsSupportedSimdFunction(Node);
	default:
		return false;
	}
}

bool IsSupportedFieldAdapterKey(const FVexSimdFieldAdapterKey& Key)
{
	return Key.ValueType == EVexValueType::Float
		&& (Key.StorageClass == EVexVectorStorageClass::AosPacked
			|| Key.StorageClass == EVexVectorStorageClass::MassFragmentColumn);
}

bool IsBulkFieldBindingSupported(const FVexCompiledSimdFieldBinding& Binding)
{
	return IsSupportedFieldAdapterKey(Binding.AdapterKey)
		&& Binding.AdapterKey.StorageClass == EVexVectorStorageClass::AosPacked
		&& Binding.MemberOffset != INDEX_NONE;
}

bool IsDirectFieldBindingSupported(const FVexCompiledSimdFieldBinding& Binding)
{
	return IsSupportedFieldAdapterKey(Binding.AdapterKey)
		&& Binding.AdapterKey.StorageClass == EVexVectorStorageClass::MassFragmentColumn
		&& Binding.MemberOffset != INDEX_NONE
		&& Binding.FragmentType == FName(TEXT("FFlightDroidStateFragment"));
}

void BuildCompiledFieldBindings(
	const FVexSchemaBindingResult* SchemaBinding,
	TMap<FString, FVexCompiledSimdFieldBinding>& OutBindings)
{
	OutBindings.Reset();
	if (!SchemaBinding)
	{
		return;
	}

	for (const FVexSchemaBoundSymbol& BoundSymbol : SchemaBinding->BoundSymbols)
	{
		FVexCompiledSimdFieldBinding Binding;
		Binding.SymbolName = BoundSymbol.SymbolName;
		Binding.AdapterKey.ValueType = BoundSymbol.LogicalSymbol.ValueType;
		Binding.AdapterKey.bWritable = BoundSymbol.LogicalSymbol.bWritable;
		Binding.AdapterKey.StorageClass = BoundSymbol.LogicalSymbol.VectorPack.StorageClass == EVexVectorStorageClass::None
			? BuildDefaultVectorPackContract(
				BoundSymbol.LogicalSymbol.Storage,
				BoundSymbol.LogicalSymbol.ValueType,
				BoundSymbol.LogicalSymbol.AlignmentRequirement,
				BoundSymbol.LogicalSymbol.bSimdReadAllowed,
				BoundSymbol.LogicalSymbol.bSimdWriteAllowed,
				BoundSymbol.LogicalSymbol.bGpuTier1Allowed).StorageClass
			: BoundSymbol.LogicalSymbol.VectorPack.StorageClass;
		Binding.StorageKind = BoundSymbol.LogicalSymbol.Storage.Kind;
		Binding.MemberOffset = BoundSymbol.LogicalSymbol.Storage.MemberOffset;
		Binding.FragmentType = BoundSymbol.LogicalSymbol.Storage.FragmentType;
		OutBindings.Add(Binding.SymbolName, MoveTemp(Binding));
	}
}

const float* ResolveBulkFloatFieldPointer(
	const Flight::Swarm::FDroidState& Droid,
	const FVexCompiledSimdFieldBinding& Binding)
{
	if (!IsBulkFieldBindingSupported(Binding))
	{
		return nullptr;
	}

	const uint8* Bytes = reinterpret_cast<const uint8*>(&Droid);
	return reinterpret_cast<const float*>(Bytes + Binding.MemberOffset);
}

float* ResolveBulkFloatFieldPointer(
	Flight::Swarm::FDroidState& Droid,
	const FVexCompiledSimdFieldBinding& Binding)
{
	if (!IsBulkFieldBindingSupported(Binding))
	{
		return nullptr;
	}

	uint8* Bytes = reinterpret_cast<uint8*>(&Droid);
	return reinterpret_cast<float*>(Bytes + Binding.MemberOffset);
}

const float* ResolveDirectFloatFieldPointer(
	const FFlightDroidStateFragment& Droid,
	const FVexCompiledSimdFieldBinding& Binding)
{
	if (!IsDirectFieldBindingSupported(Binding))
	{
		return nullptr;
	}

	const uint8* Bytes = reinterpret_cast<const uint8*>(&Droid);
	return reinterpret_cast<const float*>(Bytes + Binding.MemberOffset);
}

float* ResolveDirectFloatFieldPointer(
	FFlightDroidStateFragment& Droid,
	const FVexCompiledSimdFieldBinding& Binding)
{
	if (!IsDirectFieldBindingSupported(Binding))
	{
		return nullptr;
	}

	uint8* Bytes = reinterpret_cast<uint8*>(&Droid);
	return reinterpret_cast<float*>(Bytes + Binding.MemberOffset);
}

bool TryLoadPortableBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<Flight::Swarm::FDroidState> DroidStates,
	const int32 BaseIndex,
	const int32 BatchSize,
	FVector4f& OutRegister)
{
	if (!Binding || !IsBulkFieldBindingSupported(*Binding))
	{
		return false;
	}

	OutRegister = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	for (int32 LaneIndex = 0; LaneIndex < BatchSize; ++LaneIndex)
	{
		const float* Field = ResolveBulkFloatFieldPointer(DroidStates[BaseIndex + LaneIndex], *Binding);
		if (!Field)
		{
			return false;
		}
		OutRegister[LaneIndex] = *Field;
	}

	return true;
}

bool TryStorePortableBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<Flight::Swarm::FDroidState> DroidStates,
	const int32 BaseIndex,
	const int32 BatchSize,
	const FVector4f& RegisterValue)
{
	if (!Binding || !IsBulkFieldBindingSupported(*Binding))
	{
		return false;
	}

	for (int32 LaneIndex = 0; LaneIndex < BatchSize; ++LaneIndex)
	{
		float* Field = ResolveBulkFloatFieldPointer(DroidStates[BaseIndex + LaneIndex], *Binding);
		if (!Field)
		{
			return false;
		}
		*Field = RegisterValue[LaneIndex];
	}

	return true;
}

bool TryLoadPortableDirectBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<FFlightDroidStateFragment> DroidStates,
	const int32 BaseIndex,
	const int32 BatchSize,
	FVector4f& OutRegister)
{
	if (!Binding || !IsDirectFieldBindingSupported(*Binding))
	{
		return false;
	}

	OutRegister = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	for (int32 LaneIndex = 0; LaneIndex < BatchSize; ++LaneIndex)
	{
		const float* Field = ResolveDirectFloatFieldPointer(DroidStates[BaseIndex + LaneIndex], *Binding);
		if (!Field)
		{
			return false;
		}
		OutRegister[LaneIndex] = *Field;
	}

	return true;
}

bool TryStorePortableDirectBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<FFlightDroidStateFragment> DroidStates,
	const int32 BaseIndex,
	const int32 BatchSize,
	const FVector4f& RegisterValue)
{
	if (!Binding || !IsDirectFieldBindingSupported(*Binding))
	{
		return false;
	}

	for (int32 LaneIndex = 0; LaneIndex < BatchSize; ++LaneIndex)
	{
		FFlightDroidStateFragment& Droid = DroidStates[BaseIndex + LaneIndex];
		float* Field = ResolveDirectFloatFieldPointer(Droid, *Binding);
		if (!Field)
		{
			return false;
		}
		*Field = RegisterValue[LaneIndex];
		Droid.bIsDirty = true;
	}

	return true;
}

bool TryGetAssignmentInfo(
	const FVexStatementAst& Statement,
	const FVexProgramAst& Program,
	int32& OutOperatorIndex,
	FString& OutTargetName,
	bool& bOutIsSymbol)
{
	OutOperatorIndex = INDEX_NONE;
	OutTargetName.Reset();
	bOutIsSymbol = false;

	if (!Program.Tokens.IsValidIndex(Statement.StartTokenIndex) || !Program.Tokens.IsValidIndex(Statement.EndTokenIndex))
	{
		return false;
	}

	for (int32 TokenIndex = Statement.StartTokenIndex; TokenIndex <= Statement.EndTokenIndex; ++TokenIndex)
	{
		const FVexToken& Token = Program.Tokens[TokenIndex];
		if (Token.Kind == EVexTokenKind::Operator
			&& (Token.Lexeme == TEXT("=")
				|| Token.Lexeme == TEXT("+=")
				|| Token.Lexeme == TEXT("-=")
				|| Token.Lexeme == TEXT("*=")
				|| Token.Lexeme == TEXT("/=")))
		{
			OutOperatorIndex = TokenIndex;
			break;
		}
	}

	if (OutOperatorIndex == INDEX_NONE)
	{
		return false;
	}

	const FVexToken& FirstToken = Program.Tokens[Statement.StartTokenIndex];
	if (FirstToken.Kind == EVexTokenKind::Identifier
		&& IsDeclarationTypeToken(FirstToken.Lexeme))
	{
		const int32 NameTokenIndex = Statement.StartTokenIndex + 1;
		if (NameTokenIndex < OutOperatorIndex && Program.Tokens.IsValidIndex(NameTokenIndex))
		{
			const FVexToken& NameToken = Program.Tokens[NameTokenIndex];
			if (NameToken.Kind == EVexTokenKind::Identifier)
			{
				OutTargetName = NameToken.Lexeme;
				bOutIsSymbol = false;
				return true;
			}
		}
		return false;
	}

	if (FirstToken.Kind == EVexTokenKind::Symbol)
	{
		OutTargetName = FirstToken.Lexeme;
		bOutIsSymbol = true;
		return true;
	}

	if (FirstToken.Kind == EVexTokenKind::Identifier)
	{
		OutTargetName = FirstToken.Lexeme;
		bOutIsSymbol = false;
		return true;
	}

	return false;
}

FVector4f ResolveOperandValue(const FVexIrValue& Operand, const FVexIrProgram& Program, const TArray<FVector4f>& Registers)
{
	switch (Operand.Kind)
	{
	case FVexIrValue::EKind::Register:
		return Registers.IsValidIndex(Operand.Index) ? Registers[Operand.Index] : FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	case FVexIrValue::EKind::Constant:
		if (Program.Constants.IsValidIndex(Operand.Index))
		{
			const float Value = Program.Constants[Operand.Index];
			return FVector4f(Value, Value, Value, Value);
		}
		return FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	default:
		return FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

bool CanExecuteExplicitAvx256x8Program(
	const FVexIrProgram& Program,
	const TMap<FString, FVexCompiledSimdFieldBinding>& CompiledFieldBindings)
{
#if FLIGHT_VEX_HAS_EXPLICIT_AVX256X8
	for (const FVexIrInstruction& Instruction : Program.Instructions)
	{
		switch (Instruction.Op)
		{
		case EVexIrOp::LoadSymbol:
		{
			if (Instruction.Args.Num() < 1 || !Program.SymbolNames.IsValidIndex(Instruction.Args[0].Index))
			{
				return false;
			}

			const FString& SymbolName = Program.SymbolNames[Instruction.Args[0].Index];
			const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
			if (!(Binding && IsBulkFieldBindingSupported(*Binding))
				&& !IsSupportedAvxReadSymbol(SymbolName))
			{
				return false;
			}
			break;
		}
		case EVexIrOp::StoreSymbol:
		{
			if (!Program.SymbolNames.IsValidIndex(Instruction.Dest.Index))
			{
				return false;
			}

			const FString& SymbolName = Program.SymbolNames[Instruction.Dest.Index];
			const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
			if (!(Binding && IsBulkFieldBindingSupported(*Binding))
				&& !IsSupportedAvxWriteSymbol(SymbolName))
			{
				return false;
			}
			break;
		}
		case EVexIrOp::Const:
		case EVexIrOp::Add:
		case EVexIrOp::Sub:
		case EVexIrOp::Mul:
		case EVexIrOp::Div:
		case EVexIrOp::Sin:
			break;
		default:
			return false;
		}
	}
	return true;
#else
	(void)Program;
	(void)CompiledFieldBindings;
	return false;
#endif
}

bool CanExecuteExplicitAvx256x8DirectProgram(
	const FVexIrProgram& Program,
	const TMap<FString, FVexCompiledSimdFieldBinding>& CompiledFieldBindings)
{
#if FLIGHT_VEX_HAS_EXPLICIT_AVX256X8
	for (const FVexIrInstruction& Instruction : Program.Instructions)
	{
		switch (Instruction.Op)
		{
		case EVexIrOp::LoadSymbol:
		{
			if (Instruction.Args.Num() < 1 || !Program.SymbolNames.IsValidIndex(Instruction.Args[0].Index))
			{
				return false;
			}

			const FString& SymbolName = Program.SymbolNames[Instruction.Args[0].Index];
			const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
			if (!(Binding && IsDirectFieldBindingSupported(*Binding))
				&& !IsSupportedAvxDirectReadSymbol(SymbolName))
			{
				return false;
			}
			break;
		}
		case EVexIrOp::StoreSymbol:
		{
			if (!Program.SymbolNames.IsValidIndex(Instruction.Dest.Index))
			{
				return false;
			}

			const FString& SymbolName = Program.SymbolNames[Instruction.Dest.Index];
			const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
			if (!(Binding && IsDirectFieldBindingSupported(*Binding))
				&& !IsSupportedAvxDirectWriteSymbol(SymbolName))
			{
				return false;
			}
			break;
		}
		case EVexIrOp::Const:
		case EVexIrOp::Add:
		case EVexIrOp::Sub:
		case EVexIrOp::Mul:
		case EVexIrOp::Div:
		case EVexIrOp::Sin:
			break;
		default:
			return false;
		}
	}
	return true;
#else
	(void)Program;
	(void)CompiledFieldBindings;
	return false;
#endif
}

#if FLIGHT_VEX_HAS_EXPLICIT_AVX256X8
FLIGHT_VEX_TARGET_AVX2 __m256 ResolveAvxOperandValue(
	const FVexIrValue& Operand,
	const FVexIrProgram& Program,
	const TArray<FAvxRegister>& Registers)
{
	if (Operand.Kind == FVexIrValue::EKind::Register && Registers.IsValidIndex(Operand.Index))
	{
		return _mm256_loadu_ps(Registers[Operand.Index].Values);
	}

	if (Operand.Kind == FVexIrValue::EKind::Constant && Program.Constants.IsValidIndex(Operand.Index))
	{
		return _mm256_set1_ps(Program.Constants[Operand.Index]);
	}

	return _mm256_setzero_ps();
}

FLIGHT_VEX_TARGET_AVX2 void StoreAvxRegister(TArray<FAvxRegister>& Registers, const int32 RegisterIndex, const __m256 Value)
{
	if (Registers.IsValidIndex(RegisterIndex))
	{
		_mm256_storeu_ps(Registers[RegisterIndex].Values, Value);
	}
}

FLIGHT_VEX_TARGET_AVX2 bool TryLoadAvxBulkBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<Flight::Swarm::FDroidState> DroidStates,
	const int32 BaseIndex,
	__m256& OutValue)
{
	if (!Binding || !IsBulkFieldBindingSupported(*Binding))
	{
		return false;
	}

	alignas(32) float Lanes[8];
	for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
	{
		const float* Field = ResolveBulkFloatFieldPointer(DroidStates[BaseIndex + LaneIndex], *Binding);
		if (!Field)
		{
			return false;
		}
		Lanes[LaneIndex] = *Field;
	}

	OutValue = _mm256_loadu_ps(Lanes);
	return true;
}

FLIGHT_VEX_TARGET_AVX2 bool TryStoreAvxBulkBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<Flight::Swarm::FDroidState> DroidStates,
	const int32 BaseIndex,
	const __m256 Value)
{
	if (!Binding || !IsBulkFieldBindingSupported(*Binding))
	{
		return false;
	}

	alignas(32) float Lanes[8];
	_mm256_storeu_ps(Lanes, Value);
	for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
	{
		float* Field = ResolveBulkFloatFieldPointer(DroidStates[BaseIndex + LaneIndex], *Binding);
		if (!Field)
		{
			return false;
		}
		*Field = Lanes[LaneIndex];
	}

	return true;
}

FLIGHT_VEX_TARGET_AVX2 bool TryLoadAvxDirectBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<FFlightDroidStateFragment> DroidStates,
	const int32 BaseIndex,
	__m256& OutValue)
{
	if (!Binding || !IsDirectFieldBindingSupported(*Binding))
	{
		return false;
	}

	alignas(32) float Lanes[8];
	for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
	{
		const float* Field = ResolveDirectFloatFieldPointer(DroidStates[BaseIndex + LaneIndex], *Binding);
		if (!Field)
		{
			return false;
		}
		Lanes[LaneIndex] = *Field;
	}

	OutValue = _mm256_loadu_ps(Lanes);
	return true;
}

FLIGHT_VEX_TARGET_AVX2 bool TryStoreAvxDirectBoundField(
	const FVexCompiledSimdFieldBinding* Binding,
	TArrayView<FFlightDroidStateFragment> DroidStates,
	const int32 BaseIndex,
	const __m256 Value)
{
	if (!Binding || !IsDirectFieldBindingSupported(*Binding))
	{
		return false;
	}

	alignas(32) float Lanes[8];
	_mm256_storeu_ps(Lanes, Value);
	for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
	{
		FFlightDroidStateFragment& Droid = DroidStates[BaseIndex + LaneIndex];
		float* Field = ResolveDirectFloatFieldPointer(Droid, *Binding);
		if (!Field)
		{
			return false;
		}
		*Field = Lanes[LaneIndex];
		Droid.bIsDirty = true;
	}

	return true;
}

FLIGHT_VEX_TARGET_AVX2 bool ExecuteExplicitAvx256x8Program(
	const FVexIrProgram& Program,
	const TMap<FString, FVexCompiledSimdFieldBinding>& CompiledFieldBindings,
	TArrayView<Flight::Swarm::FDroidState> DroidStates)
{
	if (DroidStates.Num() < 8)
	{
		return false;
	}

	TArray<FAvxRegister> Registers;
	Registers.SetNumZeroed(Program.MaxRegisters);

	alignas(32) float Lanes[8];
	const int32 SimdEnd = DroidStates.Num() - (DroidStates.Num() % 8);
	for (int32 BaseIndex = 0; BaseIndex < SimdEnd; BaseIndex += 8)
	{
		for (const FVexIrInstruction& Instruction : Program.Instructions)
		{
			switch (Instruction.Op)
			{
			case EVexIrOp::LoadSymbol:
			{
				if (!Registers.IsValidIndex(Instruction.Dest.Index)
					|| Instruction.Args.Num() < 1
					|| !Program.SymbolNames.IsValidIndex(Instruction.Args[0].Index))
				{
					return false;
				}

				const FString& SymbolName = Program.SymbolNames[Instruction.Args[0].Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				__m256 BoundValue;
				if (TryLoadAvxBulkBoundField(Binding, DroidStates, BaseIndex, BoundValue))
				{
					StoreAvxRegister(Registers, Instruction.Dest.Index, BoundValue);
					break;
				}

				for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
				{
					const Flight::Swarm::FDroidState& Droid = DroidStates[BaseIndex + LaneIndex];
					if (SymbolName == TEXT("@position"))
					{
						Lanes[LaneIndex] = Droid.Position.X;
					}
					else if (SymbolName == TEXT("@shield"))
					{
						Lanes[LaneIndex] = Droid.Shield;
					}
					else
					{
						return false;
					}
				}

				StoreAvxRegister(Registers, Instruction.Dest.Index, _mm256_loadu_ps(Lanes));
				break;
			}
			case EVexIrOp::StoreSymbol:
			{
				if (Instruction.Args.Num() < 1 || !Program.SymbolNames.IsValidIndex(Instruction.Dest.Index))
				{
					return false;
				}

				const FString& SymbolName = Program.SymbolNames[Instruction.Dest.Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				const __m256 Value = ResolveAvxOperandValue(Instruction.Args[0], Program, Registers);
				if (TryStoreAvxBulkBoundField(Binding, DroidStates, BaseIndex, Value))
				{
					break;
				}
				_mm256_storeu_ps(Lanes, Value);
				for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
				{
					Flight::Swarm::FDroidState& Droid = DroidStates[BaseIndex + LaneIndex];
					if (SymbolName == TEXT("@shield"))
					{
						Droid.Shield = Lanes[LaneIndex];
					}
					else
					{
						return false;
					}
				}
				break;
			}
			case EVexIrOp::Const:
				if (Instruction.Args.Num() < 1)
				{
					return false;
				}
				StoreAvxRegister(Registers, Instruction.Dest.Index, ResolveAvxOperandValue(Instruction.Args[0], Program, Registers));
				break;
			case EVexIrOp::Add:
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}
				StoreAvxRegister(
					Registers,
					Instruction.Dest.Index,
					_mm256_add_ps(
						ResolveAvxOperandValue(Instruction.Args[0], Program, Registers),
						ResolveAvxOperandValue(Instruction.Args[1], Program, Registers)));
				break;
			case EVexIrOp::Sub:
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}
				StoreAvxRegister(
					Registers,
					Instruction.Dest.Index,
					_mm256_sub_ps(
						ResolveAvxOperandValue(Instruction.Args[0], Program, Registers),
						ResolveAvxOperandValue(Instruction.Args[1], Program, Registers)));
				break;
			case EVexIrOp::Mul:
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}
				StoreAvxRegister(
					Registers,
					Instruction.Dest.Index,
					_mm256_mul_ps(
						ResolveAvxOperandValue(Instruction.Args[0], Program, Registers),
						ResolveAvxOperandValue(Instruction.Args[1], Program, Registers)));
				break;
			case EVexIrOp::Div:
			{
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}

				__m256 Numerator = ResolveAvxOperandValue(Instruction.Args[0], Program, Registers);
				__m256 Denominator = ResolveAvxOperandValue(Instruction.Args[1], Program, Registers);
				_mm256_storeu_ps(Lanes, Numerator);
				alignas(32) float DenominatorValues[8];
				_mm256_storeu_ps(DenominatorValues, Denominator);
				for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
				{
					Lanes[LaneIndex] = FMath::IsNearlyZero(DenominatorValues[LaneIndex])
						? 0.0f
						: (Lanes[LaneIndex] / DenominatorValues[LaneIndex]);
				}
				StoreAvxRegister(Registers, Instruction.Dest.Index, _mm256_loadu_ps(Lanes));
				break;
			}
			case EVexIrOp::Sin:
			{
				if (Instruction.Args.Num() < 1)
				{
					return false;
				}

				const __m256 Source = ResolveAvxOperandValue(Instruction.Args[0], Program, Registers);
				_mm256_storeu_ps(Lanes, Source);
				for (float& LaneValue : Lanes)
				{
					LaneValue = FMath::Sin(LaneValue);
				}
				StoreAvxRegister(Registers, Instruction.Dest.Index, _mm256_loadu_ps(Lanes));
				break;
			}
			default:
				return false;
			}
		}
	}

	return true;
}

FLIGHT_VEX_TARGET_AVX2 bool ExecuteExplicitAvx256x8DirectProgram(
	const FVexIrProgram& Program,
	const TMap<FString, FVexCompiledSimdFieldBinding>& CompiledFieldBindings,
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates)
{
	const int32 NumEntities = FMath::Min(Transforms.Num(), DroidStates.Num());
	if (NumEntities < 8)
	{
		return false;
	}

	TArray<FAvxRegister> Registers;
	Registers.SetNumZeroed(Program.MaxRegisters);

	alignas(32) float Lanes[8];
	const int32 SimdEnd = NumEntities - (NumEntities % 8);
	for (int32 BaseIndex = 0; BaseIndex < SimdEnd; BaseIndex += 8)
	{
		for (const FVexIrInstruction& Instruction : Program.Instructions)
		{
			switch (Instruction.Op)
			{
			case EVexIrOp::LoadSymbol:
			{
				if (!Registers.IsValidIndex(Instruction.Dest.Index)
					|| Instruction.Args.Num() < 1
					|| !Program.SymbolNames.IsValidIndex(Instruction.Args[0].Index))
				{
					return false;
				}

				const FString& SymbolName = Program.SymbolNames[Instruction.Args[0].Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				__m256 BoundValue;
				if (TryLoadAvxDirectBoundField(Binding, DroidStates, BaseIndex, BoundValue))
				{
					StoreAvxRegister(Registers, Instruction.Dest.Index, BoundValue);
					break;
				}

				for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
				{
					if (SymbolName == TEXT("@position"))
					{
						Lanes[LaneIndex] = Transforms[BaseIndex + LaneIndex].Location.X;
					}
					else if (SymbolName == TEXT("@shield"))
					{
						Lanes[LaneIndex] = DroidStates[BaseIndex + LaneIndex].Shield;
					}
					else if (SymbolName == TEXT("@energy"))
					{
						Lanes[LaneIndex] = DroidStates[BaseIndex + LaneIndex].Energy;
					}
					else
					{
						return false;
					}
				}

				StoreAvxRegister(Registers, Instruction.Dest.Index, _mm256_loadu_ps(Lanes));
				break;
			}
			case EVexIrOp::StoreSymbol:
			{
				if (Instruction.Args.Num() < 1 || !Program.SymbolNames.IsValidIndex(Instruction.Dest.Index))
				{
					return false;
				}

				const FString& SymbolName = Program.SymbolNames[Instruction.Dest.Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				const __m256 Value = ResolveAvxOperandValue(Instruction.Args[0], Program, Registers);
				if (TryStoreAvxDirectBoundField(Binding, DroidStates, BaseIndex, Value))
				{
					break;
				}
				_mm256_storeu_ps(Lanes, Value);
				for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
				{
					FFlightDroidStateFragment& Droid = DroidStates[BaseIndex + LaneIndex];
					if (SymbolName == TEXT("@shield"))
					{
						Droid.Shield = Lanes[LaneIndex];
						Droid.bIsDirty = true;
					}
					else if (SymbolName == TEXT("@energy"))
					{
						Droid.Energy = Lanes[LaneIndex];
						Droid.bIsDirty = true;
					}
					else
					{
						return false;
					}
				}
				break;
			}
			case EVexIrOp::Const:
				if (Instruction.Args.Num() < 1)
				{
					return false;
				}
				StoreAvxRegister(Registers, Instruction.Dest.Index, ResolveAvxOperandValue(Instruction.Args[0], Program, Registers));
				break;
			case EVexIrOp::Add:
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}
				StoreAvxRegister(
					Registers,
					Instruction.Dest.Index,
					_mm256_add_ps(
						ResolveAvxOperandValue(Instruction.Args[0], Program, Registers),
						ResolveAvxOperandValue(Instruction.Args[1], Program, Registers)));
				break;
			case EVexIrOp::Sub:
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}
				StoreAvxRegister(
					Registers,
					Instruction.Dest.Index,
					_mm256_sub_ps(
						ResolveAvxOperandValue(Instruction.Args[0], Program, Registers),
						ResolveAvxOperandValue(Instruction.Args[1], Program, Registers)));
				break;
			case EVexIrOp::Mul:
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}
				StoreAvxRegister(
					Registers,
					Instruction.Dest.Index,
					_mm256_mul_ps(
						ResolveAvxOperandValue(Instruction.Args[0], Program, Registers),
						ResolveAvxOperandValue(Instruction.Args[1], Program, Registers)));
				break;
			case EVexIrOp::Div:
			{
				if (Instruction.Args.Num() < 2)
				{
					return false;
				}

				__m256 Numerator = ResolveAvxOperandValue(Instruction.Args[0], Program, Registers);
				__m256 Denominator = ResolveAvxOperandValue(Instruction.Args[1], Program, Registers);
				_mm256_storeu_ps(Lanes, Numerator);
				alignas(32) float DenominatorValues[8];
				_mm256_storeu_ps(DenominatorValues, Denominator);
				for (int32 LaneIndex = 0; LaneIndex < 8; ++LaneIndex)
				{
					Lanes[LaneIndex] = FMath::IsNearlyZero(DenominatorValues[LaneIndex])
						? 0.0f
						: (Lanes[LaneIndex] / DenominatorValues[LaneIndex]);
				}
				StoreAvxRegister(Registers, Instruction.Dest.Index, _mm256_loadu_ps(Lanes));
				break;
			}
			case EVexIrOp::Sin:
			{
				if (Instruction.Args.Num() < 1)
				{
					return false;
				}

				const __m256 Source = ResolveAvxOperandValue(Instruction.Args[0], Program, Registers);
				_mm256_storeu_ps(Lanes, Source);
				for (float& LaneValue : Lanes)
				{
					LaneValue = FMath::Sin(LaneValue);
				}
				StoreAvxRegister(Registers, Instruction.Dest.Index, _mm256_loadu_ps(Lanes));
				break;
			}
			default:
				return false;
			}
		}
	}

	return true;
}
#endif

void ExecutePortableProgram(
	const FVexIrProgram& Program,
	const TMap<FString, FVexCompiledSimdFieldBinding>& CompiledFieldBindings,
	TArrayView<Flight::Swarm::FDroidState> DroidStates)
{
	const int32 NumEntities = DroidStates.Num();
	if (NumEntities == 0)
	{
		return;
	}

	TArray<FVector4f> Registers;
	Registers.SetNumZeroed(Program.MaxRegisters);

	for (int32 i = 0; i < NumEntities; i += 4)
	{
		const int32 BatchSize = FMath::Min(4, NumEntities - i);
		
		for (const auto& Inst : Program.Instructions)
		{
			switch (Inst.Op)
			{
			case EVexIrOp::LoadSymbol:
			{
				if (!Registers.IsValidIndex(Inst.Dest.Index)
					|| Inst.Args.Num() < 1
					|| !Program.SymbolNames.IsValidIndex(Inst.Args[0].Index))
				{
					break;
				}

				const FString& SymbolName = Program.SymbolNames[Inst.Args[0].Index];
				FVector4f& Reg = Registers[Inst.Dest.Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				if (TryLoadPortableBoundField(Binding, DroidStates, i, BatchSize, Reg))
				{
					break;
				}
				for (int32 b = 0; b < BatchSize; ++b)
				{
					const auto& Droid = DroidStates[i + b];
					if (SymbolName == TEXT("@position")) Reg[b] = Droid.Position.X;
					else if (SymbolName == TEXT("@shield")) Reg[b] = Droid.Shield;
					else Reg[b] = 0.0f;
				}
				break;
			}
			case EVexIrOp::StoreSymbol:
			{
				if (Inst.Args.Num() < 1 || !Program.SymbolNames.IsValidIndex(Inst.Dest.Index))
				{
					break;
				}

				const FString& SymbolName = Program.SymbolNames[Inst.Dest.Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				const FVector4f Reg = ResolveOperandValue(Inst.Args[0], Program, Registers);
				if (TryStorePortableBoundField(Binding, DroidStates, i, BatchSize, Reg))
				{
					break;
				}
				for (int32 b = 0; b < BatchSize; ++b)
				{
					auto& Droid = DroidStates[i + b];
					if (SymbolName == TEXT("@shield")) Droid.Shield = Reg[b];
				}
				break;
			}
			case EVexIrOp::Const:
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 1)
				{
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], Program, Registers);
				}
				break;
			case EVexIrOp::Add:
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], Program, Registers) + ResolveOperandValue(Inst.Args[1], Program, Registers);
				}
				break;
			case EVexIrOp::Sub:
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], Program, Registers) - ResolveOperandValue(Inst.Args[1], Program, Registers);
				}
				break;
			case EVexIrOp::Mul:
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], Program, Registers) * ResolveOperandValue(Inst.Args[1], Program, Registers);
				}
				break;
			case EVexIrOp::Div:
			{
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					const FVector4f Den = ResolveOperandValue(Inst.Args[1], Program, Registers);
					const FVector4f Num = ResolveOperandValue(Inst.Args[0], Program, Registers);
					Registers[Inst.Dest.Index] = FVector4f(
						FMath::IsNearlyZero(Den.X) ? 0.0f : (Num.X / Den.X),
						FMath::IsNearlyZero(Den.Y) ? 0.0f : (Num.Y / Den.Y),
						FMath::IsNearlyZero(Den.Z) ? 0.0f : (Num.Z / Den.Z),
						FMath::IsNearlyZero(Den.W) ? 0.0f : (Num.W / Den.W));
				}
				break;
			}
			case EVexIrOp::Sin:
			{
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 1)
				{
					const FVector4f Src = ResolveOperandValue(Inst.Args[0], Program, Registers);
					Registers[Inst.Dest.Index] = FVector4f(FMath::Sin(Src.X), FMath::Sin(Src.Y), FMath::Sin(Src.Z), FMath::Sin(Src.W));
				}
				break;
			}
			default: break;
			}
		}
	}
}

} // namespace

bool FVexSimdExecutor::HasExplicitAvx256x8KernelSupport()
{
#if FLIGHT_VEX_HAS_EXPLICIT_AVX256X8
	return true;
#else
	return false;
#endif
}

bool FVexSimdExecutor::IsExplicitAvx256x8RuntimeSupported()
{
#if FLIGHT_VEX_HAS_EXPLICIT_AVX256X8
	__builtin_cpu_init();
	return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
	return false;
#endif
}

bool FVexSimdExecutor::CanCompileProgram(const FVexProgramAst& Program, const TArray<FVexSymbolDefinition>& SymbolDefinitions, FString* OutReason)
{
	TMap<FString, const FVexSymbolDefinition*> SymbolMap;
	for (const auto& D : SymbolDefinitions) SymbolMap.Add(D.SymbolName, &D);

	for (const FVexStatementAst& Statement : Program.Statements)
	{
		if (Statement.Kind == EVexStatementKind::TargetDirective)
		{
			continue;
		}

		if (Statement.Kind != EVexStatementKind::Assignment)
		{
			if (OutReason) *OutReason = TEXT("SIMD backend currently supports assignment-only statements.");
			return false;
		}

		int32 OperatorIndex = INDEX_NONE;
		FString TargetName;
		bool bIsSymbol = false;
		if (!TryGetAssignmentInfo(Statement, Program, OperatorIndex, TargetName, bIsSymbol))
		{
			if (OutReason) *OutReason = TEXT("Malformed assignment statement for SIMD compilation.");
			return false;
		}

		if (!Program.Tokens.IsValidIndex(OperatorIndex) || Program.Tokens[OperatorIndex].Lexeme != TEXT("="))
		{
			if (OutReason) *OutReason = TEXT("SIMD backend currently supports only '=' assignments (no compound assignment).");
			return false;
		}

		if (bIsSymbol)
		{
			const auto* Def = SymbolMap.FindRef(TargetName);
			if (!Def)
			{
				if (OutReason) *OutReason = FString::Printf(TEXT("Symbol '%s' not found in manifest."), *TargetName);
				return false;
			}
			if (!Def->bSimdWriteAllowed)
			{
				if (OutReason) *OutReason = FString::Printf(TEXT("Symbol '%s' does not allow SIMD-accelerated writes."), *TargetName);
				return false;
			}
		}
	}

	for (const FVexExpressionAst& Expression : Program.Expressions)
	{
		if (Expression.Kind == EVexExprKind::SymbolRef)
		{
			const auto* Def = SymbolMap.FindRef(Expression.Lexeme);
			if (!Def)
			{
				if (OutReason) *OutReason = FString::Printf(TEXT("Symbol '%s' not found in manifest."), *Expression.Lexeme);
				return false;
			}
			if (!Def->bSimdReadAllowed)
			{
				if (OutReason) *OutReason = FString::Printf(TEXT("Symbol '%s' does not allow SIMD-accelerated reads."), *Expression.Lexeme);
				return false;
			}
		}
		else if (Expression.Kind == EVexExprKind::FunctionCall)
		{
			if (!IsSupportedSimdFunction(Expression))
			{
				if (OutReason) *OutReason = FString::Printf(TEXT("Function '%s' is not supported in SIMD path."), *Expression.Lexeme);
				return false;
			}
		}
		else if (!IsSupportedExpressionNode(Expression))
		{
			if (OutReason) *OutReason = FString::Printf(TEXT("SIMD expression node unsupported: kind=%d lexeme='%s'."), static_cast<int32>(Expression.Kind), *Expression.Lexeme);
			return false;
		}
	}

	return true;
}

TSharedPtr<FVexSimdExecutor> FVexSimdExecutor::Compile(
	TSharedPtr<FVexIrProgram> IrProgram,
	const FVexSchemaBindingResult* SchemaBinding)
{
	if (!IrProgram.IsValid()) return nullptr;
	TSharedPtr<FVexSimdExecutor> Plan = MakeShared<FVexSimdExecutor>();
	Plan->Ir = IrProgram;
	BuildCompiledFieldBindings(SchemaBinding, Plan->CompiledFieldBindings);
	return Plan;
}

bool FVexSimdExecutor::SupportsExplicitAvx256x8() const
{
	return Ir.IsValid() && CanExecuteExplicitAvx256x8Program(*Ir, CompiledFieldBindings);
}

bool FVexSimdExecutor::SupportsExplicitAvx256x8Direct() const
{
	return Ir.IsValid() && CanExecuteExplicitAvx256x8DirectProgram(*Ir, CompiledFieldBindings);
}

void FVexSimdExecutor::ExecuteDirect(
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates) const
{
	const int32 NumEntities = DroidStates.Num();
	if (NumEntities == 0 || !Ir.IsValid()) return;

	// 1. Alignment Validation (Mass fragments should be 16-byte aligned for optimal SIMD)
	checkf(((PTRINT)Transforms.GetData() % 16) == 0, TEXT("Transforms fragment data must be 16-byte aligned for VEX SIMD."));
	checkf(((PTRINT)DroidStates.GetData() % 16) == 0, TEXT("DroidStates fragment data must be 16-byte aligned for VEX SIMD."));

	TArray<FVector4f> Registers;
	Registers.SetNumZeroed(Ir->MaxRegisters);

	// 2. SIMD 4-wide Main Loop
	const int32 SimdEnd = NumEntities - (NumEntities % 4);
	for (int32 i = 0; i < SimdEnd; i += 4)
	{
		for (const auto& Inst : Ir->Instructions)
		{
			switch (Inst.Op)
			{
			case EVexIrOp::LoadSymbol:
			{
				const FString& SymbolName = Ir->SymbolNames[Inst.Args[0].Index];
				FVector4f& Reg = Registers[Inst.Dest.Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				if (TryLoadPortableDirectBoundField(Binding, DroidStates, i, 4, Reg))
				{
					break;
				}
				if (SymbolName == TEXT("@position"))
				{
					Reg = FVector4f(Transforms[i+0].Location.X, Transforms[i+1].Location.X, Transforms[i+2].Location.X, Transforms[i+3].Location.X);
				}
				else if (SymbolName == TEXT("@shield"))
				{
					Reg = FVector4f(DroidStates[i+0].Shield, DroidStates[i+1].Shield, DroidStates[i+2].Shield, DroidStates[i+3].Shield);
				}
				else if (SymbolName == TEXT("@energy"))
				{
					Reg = FVector4f(DroidStates[i+0].Energy, DroidStates[i+1].Energy, DroidStates[i+2].Energy, DroidStates[i+3].Energy);
				}
				break;
			}
			case EVexIrOp::StoreSymbol:
			{
				const FString& SymbolName = Ir->SymbolNames[Inst.Dest.Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				const FVector4f Reg = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
				if (TryStorePortableDirectBoundField(Binding, DroidStates, i, 4, Reg))
				{
					break;
				}
				if (SymbolName == TEXT("@shield"))
				{
					DroidStates[i+0].Shield = Reg.X; DroidStates[i+0].bIsDirty = true;
					DroidStates[i+1].Shield = Reg.Y; DroidStates[i+1].bIsDirty = true;
					DroidStates[i+2].Shield = Reg.Z; DroidStates[i+2].bIsDirty = true;
					DroidStates[i+3].Shield = Reg.W; DroidStates[i+3].bIsDirty = true;
				}
				else if (SymbolName == TEXT("@energy"))
				{
					DroidStates[i+0].Energy = Reg.X; DroidStates[i+0].bIsDirty = true;
					DroidStates[i+1].Energy = Reg.Y; DroidStates[i+1].bIsDirty = true;
					DroidStates[i+2].Energy = Reg.Z; DroidStates[i+2].bIsDirty = true;
					DroidStates[i+3].Energy = Reg.W; DroidStates[i+3].bIsDirty = true;
				}
				break;
			}
			case EVexIrOp::Const: Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers); break;
			case EVexIrOp::Add: Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers) + ResolveOperandValue(Inst.Args[1], *Ir, Registers); break;
			case EVexIrOp::Sub: Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers) - ResolveOperandValue(Inst.Args[1], *Ir, Registers); break;
			case EVexIrOp::Mul: Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers) * ResolveOperandValue(Inst.Args[1], *Ir, Registers); break;
			case EVexIrOp::Div:
			{
				const FVector4f Den = ResolveOperandValue(Inst.Args[1], *Ir, Registers);
				const FVector4f Num = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
				Registers[Inst.Dest.Index] = FVector4f(
					FMath::IsNearlyZero(Den.X) ? 0.0f : (Num.X / Den.X),
					FMath::IsNearlyZero(Den.Y) ? 0.0f : (Num.Y / Den.Y),
					FMath::IsNearlyZero(Den.Z) ? 0.0f : (Num.Z / Den.Z),
					FMath::IsNearlyZero(Den.W) ? 0.0f : (Num.W / Den.W));
				break;
			}
			case EVexIrOp::Sin:
			{
				const FVector4f Src = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
				Registers[Inst.Dest.Index] = FVector4f(FMath::Sin(Src.X), FMath::Sin(Src.Y), FMath::Sin(Src.Z), FMath::Sin(Src.W));
				break;
			}
			default: break;
			}
		}
	}

	// 3. Scalar Cleanup Loop (Tail)
	for (int32 i = SimdEnd; i < NumEntities; ++i)
	{
		TArray<float> ScalarRegs;
		ScalarRegs.SetNumZeroed(Ir->MaxRegisters);

		for (const auto& Inst : Ir->Instructions)
		{
			auto GetScalar = [&](const FVexIrValue& Val) -> float
			{
				if (Val.Kind == FVexIrValue::EKind::Constant) return Ir->Constants[Val.Index];
				return ScalarRegs[Val.Index];
			};

			switch (Inst.Op)
			{
			case EVexIrOp::LoadSymbol:
			{
				const FString& SymbolName = Ir->SymbolNames[Inst.Args[0].Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				if (Binding && IsDirectFieldBindingSupported(*Binding))
				{
					const float* Field = ResolveDirectFloatFieldPointer(DroidStates[i], *Binding);
					if (Field)
					{
						ScalarRegs[Inst.Dest.Index] = *Field;
						break;
					}
				}
				if (SymbolName == TEXT("@position")) ScalarRegs[Inst.Dest.Index] = Transforms[i].Location.X;
				else if (SymbolName == TEXT("@shield")) ScalarRegs[Inst.Dest.Index] = DroidStates[i].Shield;
				else if (SymbolName == TEXT("@energy")) ScalarRegs[Inst.Dest.Index] = DroidStates[i].Energy;
				break;
			}
			case EVexIrOp::StoreSymbol:
			{
				const FString& SymbolName = Ir->SymbolNames[Inst.Dest.Index];
				const FVexCompiledSimdFieldBinding* Binding = CompiledFieldBindings.Find(SymbolName);
				float Val = GetScalar(Inst.Args[0]);
				if (Binding && IsDirectFieldBindingSupported(*Binding))
				{
					FFlightDroidStateFragment& Droid = DroidStates[i];
					float* Field = ResolveDirectFloatFieldPointer(Droid, *Binding);
					if (Field)
					{
						*Field = Val;
						Droid.bIsDirty = true;
						break;
					}
				}
				if (SymbolName == TEXT("@shield")) { DroidStates[i].Shield = Val; DroidStates[i].bIsDirty = true; }
				else if (SymbolName == TEXT("@energy")) { DroidStates[i].Energy = Val; DroidStates[i].bIsDirty = true; }
				break;
			}
			case EVexIrOp::Const: ScalarRegs[Inst.Dest.Index] = Ir->Constants[Inst.Args[0].Index]; break;
			case EVexIrOp::Add: ScalarRegs[Inst.Dest.Index] = GetScalar(Inst.Args[0]) + GetScalar(Inst.Args[1]); break;
			case EVexIrOp::Sub: ScalarRegs[Inst.Dest.Index] = GetScalar(Inst.Args[0]) - GetScalar(Inst.Args[1]); break;
			case EVexIrOp::Mul: ScalarRegs[Inst.Dest.Index] = GetScalar(Inst.Args[0]) * GetScalar(Inst.Args[1]); break;
			case EVexIrOp::Div: { float D = GetScalar(Inst.Args[1]); ScalarRegs[Inst.Dest.Index] = FMath::IsNearlyZero(D) ? 0.0f : (GetScalar(Inst.Args[0]) / D); break; }
			case EVexIrOp::Sin: ScalarRegs[Inst.Dest.Index] = FMath::Sin(GetScalar(Inst.Args[0])); break;
			default: break;
			}
		}
	}
}

void FVexSimdExecutor::Execute(TArrayView<Flight::Swarm::FDroidState> DroidStates) const
{
	if (!Ir.IsValid())
	{
		return;
	}

	ExecutePortableProgram(*Ir, CompiledFieldBindings, DroidStates);
}

bool FVexSimdExecutor::ExecuteExplicitAvx256x8(TArrayView<Flight::Swarm::FDroidState> DroidStates) const
{
	if (!Ir.IsValid()
		|| !SupportsExplicitAvx256x8()
		|| !IsExplicitAvx256x8RuntimeSupported())
	{
		return false;
	}

#if FLIGHT_VEX_HAS_EXPLICIT_AVX256X8
	const int32 SimdEnd = DroidStates.Num() - (DroidStates.Num() % 8);
	if (SimdEnd >= 8)
	{
		if (!ExecuteExplicitAvx256x8Program(
			*Ir,
			CompiledFieldBindings,
			TArrayView<Flight::Swarm::FDroidState>(DroidStates.GetData(), SimdEnd)))
		{
			return false;
		}
	}

	if (SimdEnd < DroidStates.Num())
	{
		ExecutePortableProgram(
			*Ir,
			CompiledFieldBindings,
			TArrayView<Flight::Swarm::FDroidState>(DroidStates.GetData() + SimdEnd, DroidStates.Num() - SimdEnd));
	}

	return true;
#else
	(void)DroidStates;
	return false;
#endif
}

bool FVexSimdExecutor::ExecuteExplicitAvx256x8Direct(
	TArrayView<FFlightTransformFragment> Transforms,
	TArrayView<FFlightDroidStateFragment> DroidStates) const
{
	if (!Ir.IsValid()
		|| !SupportsExplicitAvx256x8Direct()
		|| !IsExplicitAvx256x8RuntimeSupported())
	{
		return false;
	}

#if FLIGHT_VEX_HAS_EXPLICIT_AVX256X8
	const int32 NumEntities = FMath::Min(Transforms.Num(), DroidStates.Num());
	const int32 SimdEnd = NumEntities - (NumEntities % 8);
	if (SimdEnd >= 8)
	{
		if (!ExecuteExplicitAvx256x8DirectProgram(
			*Ir,
			CompiledFieldBindings,
			TArrayView<FFlightTransformFragment>(Transforms.GetData(), SimdEnd),
			TArrayView<FFlightDroidStateFragment>(DroidStates.GetData(), SimdEnd)))
		{
			return false;
		}
	}

	if (SimdEnd < NumEntities)
	{
		ExecuteDirect(
			TArrayView<FFlightTransformFragment>(Transforms.GetData() + SimdEnd, NumEntities - SimdEnd),
			TArrayView<FFlightDroidStateFragment>(DroidStates.GetData() + SimdEnd, NumEntities - SimdEnd));
	}

	return true;
#else
	(void)Transforms;
	(void)DroidStates;
	return false;
#endif
}

const FVexCompiledSimdFieldBinding* FVexSimdExecutor::FindCompiledFieldBinding(const FString& SymbolName) const
{
	return CompiledFieldBindings.Find(SymbolName);
}

} // namespace Flight::Vex
