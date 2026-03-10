// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Vex/FlightVexVvmAssembler.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexIr.h"
#include "Vex/FlightVexTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexIrComplexControlFlowTest,
	"FlightProject.Functional.Verse.Vvm.ComplexControlFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexIrComplexControlFlowTest::RunTest(const FString& Parameters)
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	using namespace Flight::Vex;

	/**
	 * Test Loop:
	 * var i:int = 0;
	 * while (i < 5):
	 *    @shield = @shield + 1.0;
	 *    i = i + 1;
	 * return @shield;
	 */
	FVexIrProgram Ir;
	Ir.SymbolNames.Add(TEXT("@shield")); // Sym 0
	Ir.Constants.Add(0.0f); // Const 0: init i
	Ir.Constants.Add(5.0f); // Const 1: loop limit
	Ir.Constants.Add(1.0f); // Const 2: increment
	Ir.MaxRegisters = 3;    // R0: i, R1: temp, R2: cond

	// 0: R0 = Const 0 (i = 0)
	Ir.Instructions.Add({EVexIrOp::Const, {FVexIrValue::EKind::Register, EVexValueType::Float, 0}, {{FVexIrValue::EKind::Constant, EVexValueType::Float, 0}}});
	
	// 1: Loop Header Target
	int32 LoopHeaderIdx = 1;
	// 1: R2 = R0 < Const 1 (i < 5)
	Ir.Instructions.Add({EVexIrOp::Less, {FVexIrValue::EKind::Register, EVexValueType::Bool, 2}, {{FVexIrValue::EKind::Register, EVexValueType::Float, 0}, {FVexIrValue::EKind::Constant, EVexValueType::Float, 1}}});
	
	// 2: JumpIf R2 to 8 (Exit)
	int32 JumpExitIdx = 2;
	Ir.Instructions.Add({EVexIrOp::JumpIf, {}, {{FVexIrValue::EKind::Register, EVexValueType::Bool, 2}, {FVexIrValue::EKind::Constant, EVexValueType::Int, 8}}});

	// 3: R1 = Load @shield
	Ir.Instructions.Add({EVexIrOp::LoadSymbol, {FVexIrValue::EKind::Register, EVexValueType::Float, 1}, {{FVexIrValue::EKind::Symbol, EVexValueType::Float, 0}}});
	
	// 4: R1 = R1 + Const 2 (@shield + 1.0)
	Ir.Instructions.Add({EVexIrOp::Add, {FVexIrValue::EKind::Register, EVexValueType::Float, 1}, {{FVexIrValue::EKind::Register, EVexValueType::Float, 1}, {FVexIrValue::EKind::Constant, EVexValueType::Float, 2}}});
	
	// 5: Store @shield = R1
	Ir.Instructions.Add({EVexIrOp::StoreSymbol, {FVexIrValue::EKind::Symbol, EVexValueType::Float, 0}, {{FVexIrValue::EKind::Register, EVexValueType::Float, 1}}});
	
	// 6: R0 = R0 + Const 2 (i = i + 1)
	Ir.Instructions.Add({EVexIrOp::Add, {FVexIrValue::EKind::Register, EVexValueType::Float, 0}, {{FVexIrValue::EKind::Register, EVexValueType::Float, 0}, {FVexIrValue::EKind::Constant, EVexValueType::Float, 2}}});
	
	// 7: Jump back to 1
	Ir.Instructions.Add({EVexIrOp::Jump, {}, {{FVexIrValue::EKind::Constant, EVexValueType::Int, LoopHeaderIdx}}});

	// 8: Exit Target
	// 8: R1 = Load @shield
	Ir.Instructions.Add({EVexIrOp::LoadSymbol, {FVexIrValue::EKind::Register, EVexValueType::Float, 1}, {{FVexIrValue::EKind::Symbol, EVexValueType::Float, 0}}});
	
	// 9: Return R1
	Ir.Instructions.Add({EVexIrOp::Return, {}, {{FVexIrValue::EKind::Register, EVexValueType::Float, 1}}});

	// Locate subsystem
	UFlightVerseSubsystem* Subsystem = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			Subsystem = Context.World()->GetSubsystem<UFlightVerseSubsystem>();
			if (Subsystem) break;
		}
	}

	if (!Subsystem) return false;

	Flight::Swarm::FDroidState DummyState;
	DummyState.Shield = 10.0f;

	// Assemble
	Verse::VProcedure* Procedure = nullptr;
	Subsystem->VerseContext.EnterVM([&]()
	{
		Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
		Procedure = FVexVvmAssembler::Assemble(Ir, AllocContext, TEXT("ComplexLoopTest"));
	});

	TestNotNull(TEXT("Assembler should produce a procedure"), Procedure);
	if (!Procedure) return false;

	// Execute
	Verse::FOpResult Result(Verse::FOpResult::Error);
	Subsystem->VerseContext.EnterVM([&]()
	{
		Subsystem->ActiveVmDroidState = &DummyState;
		Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
		Verse::VFunction& Function = Verse::VFunction::New(AllocContext, *Procedure, Verse::VValue(Subsystem));
		Result = Function.Invoke(Subsystem->VerseContext, Verse::VFunction::Args());
		Subsystem->ActiveVmDroidState = nullptr;
	});

	TestTrue(TEXT("Execution should return"), Result.IsReturn());
	if (Result.IsReturn())
	{
		// Expected: 10.0 + (1.0 * 5) = 15.0
		TestEqual(TEXT("Shield should be incremented 5 times to 15.0"), Result.Value.AsFloat().ReinterpretAsDouble(), 15.0);
		TestEqual(TEXT("Droid state shield should be updated to 15.0"), DummyState.Shield, 15.0f);
	}

	return true;
#else
	return true;
#endif
}
