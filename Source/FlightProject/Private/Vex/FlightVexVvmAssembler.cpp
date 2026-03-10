// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Vex/FlightVexVvmAssembler.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMBytecodeEmitter.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMFalse.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexNativeRegistry.h"

namespace Flight::Vex
{

Verse::VProcedure* FVexVvmAssembler::Assemble(
	const FVexIrProgram& IrProgram,
	Verse::FAllocationContext& AllocContext,
	const FString& ProcedureNameStr)
{
	using namespace Verse;

	VUniqueString& FileName = VUniqueString::New(AllocContext, "VexGenerated.verse");
	VUniqueString& ProcedureName = VUniqueString::New(AllocContext, TCHAR_TO_UTF8(*ProcedureNameStr));

	// Behaviors typically have 0 parameters as they access droid state via native thunks.
	FOpEmitter Emitter(AllocContext, FileName, ProcedureName, 0, 0);

	// 1. Map IR registers to VVM registers
	TArray<FRegisterIndex> Registers;
	for (int32 i = 0; i < IrProgram.MaxRegisters; ++i)
	{
		Registers.Add(Emitter.AllocateRegister(FLocation()));
	}

	// 2. Map IR constants to VVM constants
	TArray<FConstantIndex> Constants;
	for (float ConstVal : IrProgram.Constants)
	{
		Constants.Add(Emitter.AllocateConstant(VValue::FromFloat(ConstVal)));
	}

	// 3. Pre-allocate symbol strings for Load/Store thunks
	TArray<FConstantIndex> SymbolConstants;
	for (const FString& Name : IrProgram.SymbolNames)
	{
		VUniqueString& SymbolName = VUniqueString::New(AllocContext, TCHAR_TO_UTF8(*Name));
		SymbolConstants.Add(Emitter.AllocateConstant(VValue(&SymbolName)));
	}

	// 4. Map Native Thunks from Registry
	TMap<FString, FConstantIndex> NativeFnConstants;
	FVexNativeRegistry& Registry = FVexNativeRegistry::Get();
	
	for (auto& It : Registry.GetFunctions())
	{
		const TSharedPtr<IVexNativeFunction>& NativeFn = It.Value;
		VUniqueString& VName = VUniqueString::New(AllocContext, TCHAR_TO_UTF8(*NativeFn->GetName()));
		
		// Map the static thunk address in UFlightVerseSubsystem to the native function
		// For the schema-driven ones, we use Get/SetStateValue thunks.
		Verse::TNativeFunctionPtr ThunkPtr = nullptr;
		if (NativeFn->GetName() == TEXT("GetStateValue")) ThunkPtr = &UFlightVerseSubsystem::GetStateValue;
		else if (NativeFn->GetName() == TEXT("SetStateValue")) ThunkPtr = &UFlightVerseSubsystem::SetStateValue;
		else ThunkPtr = &UFlightVerseSubsystem::RegistryThunk;

		VNativeFunction& VFn = VNativeFunction::New(AllocContext, NativeFn->GetNumArgs(), ThunkPtr, VName, GlobalFalse());
		NativeFnConstants.Add(NativeFn->GetName(), Emitter.AllocateConstant(VValue(&VFn)));
	}

	// Helpers for Load/Store Symbol (mandatory core thunks)
	FConstantIndex GetFnConst = NativeFnConstants.FindChecked(TEXT("GetStateValue"));
	FConstantIndex SetFnConst = NativeFnConstants.FindChecked(TEXT("SetStateValue"));

	// 5. Label Management for Control Flow
	TMap<int32, FOpEmitter::FLabel> Labels;
	auto GetLabel = [&](int32 InstIdx)
	{
		if (FOpEmitter::FLabel* Existing = Labels.Find(InstIdx)) return *Existing;
		FOpEmitter::FLabel NewLabel = Emitter.AllocateLabel();
		Labels.Add(InstIdx, NewLabel);
		return NewLabel;
	};

	auto GetOperand = [&](const FVexIrValue& Val) -> FValueOperand
	{
		if (Val.Kind == FVexIrValue::EKind::Constant) return FValueOperand(Constants[Val.Index]);
		if (Val.Kind == FVexIrValue::EKind::Register) return FValueOperand(Registers[Val.Index]);
		// Symbols are handled explicitly by LoadSymbol/StoreSymbol
		return FValueOperand(Emitter.AllocateConstant(GlobalFalse()));
	};

	// 6. Instruction Emission Loop
	for (int32 InstIdx = 0; InstIdx < IrProgram.Instructions.Num(); ++InstIdx)
	{
		// Bind label if this instruction is a jump target
		if (Labels.Contains(InstIdx))
		{
			Emitter.BindLabel(Labels[InstIdx]);
		}

		const auto& Inst = IrProgram.Instructions[InstIdx];
		FLocation Loc; // Diagnostic location mapping could be improved here

		switch (Inst.Op)
		{
		case EVexIrOp::LoadSymbol:
		{
			FValueOperand SymbolNameOp(SymbolConstants[Inst.Args[0].Index]);
			TArray<FValueOperand> CallArgs;
			CallArgs.Add(SymbolNameOp);
			
			// Procedure's 'Self' is expected to be the UFlightVerseSubsystem instance.
			Emitter.CallWithSelf(Loc, Registers[Inst.Dest.Index], FValueOperand(GetFnConst), Emitter.Self(), MoveTemp(CallArgs), {}, {}, false);
			break;
		}
		case EVexIrOp::StoreSymbol:
		{
			FValueOperand SymbolNameOp(SymbolConstants[Inst.Dest.Index]);
			FValueOperand ValueOp = GetOperand(Inst.Args[0]);
			TArray<FValueOperand> CallArgs;
			CallArgs.Add(SymbolNameOp);
			CallArgs.Add(ValueOp);

			FRegisterIndex DummyReg = Emitter.AllocateRegister(Loc);
			Emitter.CallWithSelf(Loc, DummyReg, FValueOperand(SetFnConst), Emitter.Self(), MoveTemp(CallArgs), {}, {}, false);
			break;
		}
		case EVexIrOp::Const:
			Emitter.Move(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]));
			break;
		case EVexIrOp::Add:
			Emitter.Add(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::Sub:
			Emitter.Sub(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::Mul:
			Emitter.Mul(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::Div:
			Emitter.Div(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::Less:
			Emitter.Lt(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::Greater:
			Emitter.Gt(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::LessEqual:
			Emitter.Lte(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::GreaterEqual:
			Emitter.Gte(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::Equal:
			// Using Neq and then inverting via a fallback logic if needed. 
			// For Tier 1 literal programs, strict equality is often mapped to unification.
			// Here we use Neq and assume a boolean result for now.
			Emitter.Neq(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			// TODO: Add logic inversion if VVM lacks a dedicated Eq op that returns logic.
			break;
		case EVexIrOp::NotEqual:
			Emitter.Neq(Loc, Registers[Inst.Dest.Index], GetOperand(Inst.Args[0]), GetOperand(Inst.Args[1]));
			break;
		case EVexIrOp::JumpIf:
		{
			// VEX IR JumpIf is inverted: it jumps to the target if the condition is FALSE.
			// This is used for skipping blocks.
			FOpEmitter::FLabel TargetLabel = GetLabel(Inst.Args[1].Index);
			
			// We use Switch for a boolean branch. 0 = False, 1 = True.
			// Since logic in VVM is 0/1 (VFalse/VTrue), we can use it as a switch index.
			TArray<FOpEmitter::FLabel> JumpOffsets;
			JumpOffsets.Add(TargetLabel); // Index 0 (False) jumps to TargetLabel
			JumpOffsets.Add(Emitter.AllocateLabel()); // Index 1 (True) continues to next instruction
			
			Emitter.Switch(Loc, GetOperand(Inst.Args[0]), MoveTemp(JumpOffsets));
			Emitter.BindLabel(JumpOffsets[1]);
			break;
		}
		case EVexIrOp::Jump:
		{
			FOpEmitter::FLabel TargetLabel = GetLabel(Inst.Args[0].Index);
			Emitter.Jump(Loc, TargetLabel);
			break;
		}
		case EVexIrOp::Return:
			Emitter.Return(Loc, GetOperand(Inst.Args[0]));
			break;
		default:
			// Unimplemented IR ops (Normalize, Sin, Cos, etc.) will eventually be native calls.
			break;
		}
	}

	// Final safeguard return if the IR program has no explicit return.
	if (IrProgram.Instructions.Num() == 0 || IrProgram.Instructions.Last().Op != EVexIrOp::Return)
	{
		Emitter.Return(FLocation(), FValueOperand(Emitter.AllocateConstant(GlobalFalse())));
	}

	// Bind any labels that might point to the "past-the-end" instruction.
	if (Labels.Contains(IrProgram.Instructions.Num()))
	{
		Emitter.BindLabel(Labels[IrProgram.Instructions.Num()]);
	}

	return &Emitter.MakeProcedure(AllocContext);
}

} // namespace Flight::Vex
#endif
