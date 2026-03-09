// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexSimdExecutor.h"
#include "Mass/FlightMassFragments.h"

namespace Flight::Vex
{

namespace
{

bool IsSupportedReadSymbol(const FString& SymbolName)
{
	return SymbolName == TEXT("@position") || SymbolName == TEXT("@shield");
}

bool IsSupportedWriteSymbol(const FString& SymbolName)
{
	// Current SIMD backend only supports scalar shield writes.
	return SymbolName == TEXT("@shield");
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
		return true;
	case EVexExprKind::SymbolRef:
		return IsSupportedReadSymbol(Node.Lexeme);
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

} // namespace

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

TSharedPtr<FVexSimdExecutor> FVexSimdExecutor::Compile(TSharedPtr<FVexIrProgram> IrProgram)
{
	if (!IrProgram.IsValid()) return nullptr;
	TSharedPtr<FVexSimdExecutor> Plan = MakeShared<FVexSimdExecutor>();
	Plan->Ir = IrProgram;
	return Plan;
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
				const FVector4f Reg = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
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
				if (SymbolName == TEXT("@position")) ScalarRegs[Inst.Dest.Index] = Transforms[i].Location.X;
				else if (SymbolName == TEXT("@shield")) ScalarRegs[Inst.Dest.Index] = DroidStates[i].Shield;
				else if (SymbolName == TEXT("@energy")) ScalarRegs[Inst.Dest.Index] = DroidStates[i].Energy;
				break;
			}
			case EVexIrOp::StoreSymbol:
			{
				const FString& SymbolName = Ir->SymbolNames[Inst.Dest.Index];
				float Val = GetScalar(Inst.Args[0]);
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
	const int32 NumEntities = DroidStates.Num();
	if (NumEntities == 0 || !Ir.IsValid()) return;

	TArray<FVector4f> Registers;
	Registers.SetNumZeroed(Ir->MaxRegisters);

	for (int32 i = 0; i < NumEntities; i += 4)
	{
		const int32 BatchSize = FMath::Min(4, NumEntities - i);
		
		for (const auto& Inst : Ir->Instructions)
		{
			switch (Inst.Op)
			{
			case EVexIrOp::LoadSymbol:
			{
				if (!Registers.IsValidIndex(Inst.Dest.Index)
					|| Inst.Args.Num() < 1
					|| !Ir->SymbolNames.IsValidIndex(Inst.Args[0].Index))
				{
					break;
				}

				const FString& SymbolName = Ir->SymbolNames[Inst.Args[0].Index];
				FVector4f& Reg = Registers[Inst.Dest.Index];
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
				if (Inst.Args.Num() < 1 || !Ir->SymbolNames.IsValidIndex(Inst.Dest.Index))
				{
					break;
				}

				const FString& SymbolName = Ir->SymbolNames[Inst.Dest.Index];
				const FVector4f Reg = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
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
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
				}
				break;
			case EVexIrOp::Add:
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers) + ResolveOperandValue(Inst.Args[1], *Ir, Registers);
				}
				break;
			case EVexIrOp::Sub:
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers) - ResolveOperandValue(Inst.Args[1], *Ir, Registers);
				}
				break;
			case EVexIrOp::Mul:
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					Registers[Inst.Dest.Index] = ResolveOperandValue(Inst.Args[0], *Ir, Registers) * ResolveOperandValue(Inst.Args[1], *Ir, Registers);
				}
				break;
			case EVexIrOp::Div:
			{
				if (Registers.IsValidIndex(Inst.Dest.Index) && Inst.Args.Num() >= 2)
				{
					const FVector4f Den = ResolveOperandValue(Inst.Args[1], *Ir, Registers);
					const FVector4f Num = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
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
					const FVector4f Src = ResolveOperandValue(Inst.Args[0], *Ir, Registers);
					Registers[Inst.Dest.Index] = FVector4f(FMath::Sin(Src.X), FMath::Sin(Src.Y), FMath::Sin(Src.Z), FMath::Sin(Src.W));
				}
				break;
			}
			default: break;
			}
		}
	}
}

} // namespace Flight::Vex
