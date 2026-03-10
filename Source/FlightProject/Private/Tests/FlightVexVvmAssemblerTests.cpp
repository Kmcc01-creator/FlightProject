// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Vex/FlightVexVvmAssembler.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexIr.h"
#include "Vex/FlightVexTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexIrVvmAssemblerTest,
	"FlightProject.Functional.Verse.Vvm.Assembler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexIrVvmAssemblerTest::RunTest(const FString& Parameters)
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	using namespace Flight::Vex;

	// 1. Manually construct a simple IR program: return 42.0
	FVexIrProgram Ir;
	Ir.MaxRegisters = 1;
	Ir.Constants.Add(42.0f);

	FVexIrInstruction Ret;
	Ret.Op = EVexIrOp::Return;
	Ret.Args.Add(FVexIrValue::MakeConstant(0, EVexValueType::Float));
	Ir.Instructions.Add(Ret);

	// 2. Locate subsystem for VM context
	UFlightVerseSubsystem* Subsystem = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::PIE)
		{
			Subsystem = Context.World()->GetSubsystem<UFlightVerseSubsystem>();
			if (Subsystem) break;
		}
	}

	if (!Subsystem)
	{
		AddError(TEXT("UFlightVerseSubsystem not found in current world."));
		return false;
	}

	// 3. Assemble IR to VVM Procedure
	Verse::VProcedure* Procedure = nullptr;
	Subsystem->VerseContext.EnterVM([&]()
	{
		Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
		Procedure = FVexVvmAssembler::Assemble(Ir, AllocContext, TEXT("TestProcedure"));
	});

	TestNotNull(TEXT("Assembler should have produced a VProcedure"), Procedure);
	if (!Procedure) return false;

	// 4. Execute and verify result
	Verse::FOpResult Result(Verse::FOpResult::Error);
	Subsystem->VerseContext.EnterVM([&]()
	{
		Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
		Verse::VFunction& Function = Verse::VFunction::New(AllocContext, *Procedure, Verse::VValue(Subsystem));
		Result = Function.Invoke(Subsystem->VerseContext, Verse::VFunction::Args());
	});

	TestTrue(TEXT("Execution should return a value"), Result.IsReturn());
	if (Result.IsReturn())
	{
		TestTrue(TEXT("Return value should be a float"), Result.Value.IsFloat());
		TestEqual(TEXT("Return value should be 42.0"), Result.Value.AsFloat().ReinterpretAsDouble(), 42.0);
	}

	return true;
#else
	return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexIrVvmThunkTest,
	"FlightProject.Functional.Verse.Vvm.Thunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexIrVvmThunkTest::RunTest(const FString& Parameters)
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	using namespace Flight::Vex;

	// 1. IR Program: @shield = @shield + 10.0; return @shield;
	FVexIrProgram Ir;
	Ir.SymbolNames.Add(TEXT("@shield"));
	Ir.Constants.Add(10.0f);
	Ir.MaxRegisters = 2;

	// R0 = Load @shield
	FVexIrInstruction Load;
	Load.Op = EVexIrOp::LoadSymbol;
	Load.Dest.Kind = FVexIrValue::EKind::Register;
	Load.Dest.Index = 0;
	Load.Dest.Type = EVexValueType::Float;
	FVexIrValue SymbolVal;
	SymbolVal.Kind = FVexIrValue::EKind::Symbol;
	SymbolVal.Index = 0;
	SymbolVal.Type = EVexValueType::Float;
	Load.Args.Add(SymbolVal);
	Ir.Instructions.Add(Load);

	// R1 = R0 + 10.0
	FVexIrInstruction Add;
	Add.Op = EVexIrOp::Add;
	Add.Dest.Kind = FVexIrValue::EKind::Register;
	Add.Dest.Index = 1;
	Add.Dest.Type = EVexValueType::Float;
	Add.Args.Add(Load.Dest);
	FVexIrValue ConstVal;
	ConstVal.Kind = FVexIrValue::EKind::Constant;
	ConstVal.Index = 0;
	ConstVal.Type = EVexValueType::Float;
	Add.Args.Add(ConstVal);
	Ir.Instructions.Add(Add);

	// Store @shield = R1
	FVexIrInstruction Store;
	Store.Op = EVexIrOp::StoreSymbol;
	Store.Dest = SymbolVal;
	Store.Args.Add(Add.Dest);
	Ir.Instructions.Add(Store);

	// Return R1
	FVexIrInstruction Ret;
	Ret.Op = EVexIrOp::Return;
	Ret.Args.Add(Add.Dest);
	Ir.Instructions.Add(Ret);

	// 2. Locate subsystem and setup dummy droid state
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
	DummyState.Shield = 50.0f;

	// 3. Assemble
	Verse::VProcedure* Procedure = nullptr;
	Subsystem->VerseContext.EnterVM([&]()
	{
		Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
		Procedure = FVexVvmAssembler::Assemble(Ir, AllocContext, TEXT("ThunkTest"));
	});

	TestNotNull(TEXT("Assembler should produce a procedure"), Procedure);
	if (!Procedure) return false;

	// 4. Execute with active droid state
	Verse::FOpResult Result(Verse::FOpResult::Error);
	Subsystem->VerseContext.EnterVM([&]()
	{
		// Set active state for thunks
		// We use a private-access hack or just a public setter if available.
		// Since we're in a test in the same module, we can access it if it's protected/public.
		// In UFlightVerseSubsystem.h, ActiveVmDroidState is public (with comment).
		Subsystem->ActiveVmDroidState = &DummyState;

		Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
		Verse::VFunction& Function = Verse::VFunction::New(AllocContext, *Procedure, Verse::VValue(Subsystem));
		Result = Function.Invoke(Subsystem->VerseContext, Verse::VFunction::Args());

		Subsystem->ActiveVmDroidState = nullptr;
	});

	TestTrue(TEXT("Execution should return"), Result.IsReturn());
	if (Result.IsReturn())
	{
		TestEqual(TEXT("Return value should be 60.0 (50+10)"), Result.Value.AsFloat().ReinterpretAsDouble(), 60.0);
		TestEqual(TEXT("Droid state shield should be updated to 60.0"), DummyState.Shield, 60.0f);
	}

	return true;
#else
	return true;
#endif
}
