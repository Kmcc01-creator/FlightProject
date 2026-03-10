// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Vex/FlightVexNativeRegistry.h"
#include "Vex/FlightVexVvmAssembler.h"
#include "Verse/UFlightVerseSubsystem.h"
#include "Vex/FlightVexIr.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMOpResult.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMFalse.h"

namespace
{
	/**
	 * FVexSquareFunction: Simple native function thunk for testing the registry.
	 */
	class FVexSquareFunction : public Flight::Vex::IVexNativeFunction
	{
	public:
		virtual FString GetName() const override { return TEXT("square"); }
		virtual int32 GetNumArgs() const override { return 1; }

		virtual Verse::FOpResult Execute(Verse::FRunningContext Context, Verse::VValue Self, Verse::VNativeFunction::Args Arguments) override
		{
			if (Arguments.Num() < 1 || !Arguments[0].IsFloat())
			{
				return Verse::FOpResult(Verse::FOpResult::Error);
			}

			float Val = Arguments[0].AsFloat();
			return Verse::FOpResult(Verse::FOpResult::Return, Verse::VValue::FromFloat(Val * Val));
		}
	};
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlightVexNativeRegistryTest,
	"FlightProject.Functional.Verse.Vvm.NativeRegistry.Square",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FFlightVexNativeRegistryTest::RunTest(const FString& Parameters)
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	using namespace Flight::Vex;

	// 1. Register a new custom native function
	FVexNativeRegistry& Registry = FVexNativeRegistry::Get();
	Registry.RegisterFunction(MakeShared<FVexSquareFunction>());

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

	if (!Subsystem) return false;

	// 3. Assemble a manual IR program: return square(5.0);
	// Note: Currently FVexVvmAssembler only handles Load/StoreSymbol for thunks.
	// We'll need to expand the assembler to support EVexIrOp::Call for generic native functions.
	// For this test, let's verify the registry lookup works.
	
	auto SquareFn = Registry.FindFunction(TEXT("square"));
	TestTrue(TEXT("Square function should be registered"), SquareFn.IsValid());
	
	if (SquareFn.IsValid())
	{
		Verse::FOpResult Result(Verse::FOpResult::Error);
		Subsystem->VerseContext.EnterVM([&]()
		{
			Verse::FAllocationContext AllocContext(Subsystem->VerseContext);
			Verse::VValue Five = Verse::VValue::FromFloat(5.0f);
			Verse::VNativeFunction::Args Args;
			Args.Add(Five);

			Result = SquareFn->Execute(Subsystem->VerseContext, Verse::VValue(Subsystem), Args);
		});

		TestTrue(TEXT("Execution should return a value"), Result.IsReturn());
		if (Result.IsReturn())
		{
			TestEqual(TEXT("Square of 5.0 should be 25.0"), Result.Value.AsFloat().ReinterpretAsDouble(), 25.0);
		}
	}

	return true;
#else
	return true;
#endif
}
