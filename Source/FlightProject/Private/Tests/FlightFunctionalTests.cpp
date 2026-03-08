// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightReactive.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Functional;
using namespace Flight::Reactive;

/**
 * Test: Basic Pipeline Operators
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalPipeTest, "FlightProject.Functional.Pipes.Basic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalPipeTest::RunTest(const FString& Parameters)
{
	// 1. FunctionalTransform Pipe
	int32 Value = 10;
	int32 Result = Value | FunctionalTransform([](int32 x) { return x * 2; })
	                     | FunctionalTransform([](int32 x) { return x + 5; });
	
	TestEqual("Result should be (10 * 2) + 5 = 25", Result, 25);

	// 2. FunctionalFilter Pipe
	TArray<int32> Numbers = { 1, 2, 3, 4, 5, 6 };
	auto Evens = Numbers | FunctionalFilter([](int32 x) { return x % 2 == 0; });
	
	TestEqual("Filtered array should have 3 elements", Evens.Num(), 3);
	TestEqual("First even should be 2", Evens[0], 2);

	return true;
}

/**
 * Test: Temporal Projection (TProjectedState)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalProjectionTest, "FlightProject.Functional.Pipes.Projection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalProjectionTest::RunTest(const FString& Parameters)
{
	using namespace Order;
	
	// Create a projected state at Horizon 0
	TProjectedState<FVector, Position, 0> StartPos{ FVector(0, 0, 0) };
	
	// Simulate a projection 5 steps ahead with a constant velocity
	FVector Velocity(100, 0, 0);
	float dt = 0.1f;
	
	auto Future = StartPos.Project<5>(StartPos.Value + (Velocity * dt * 5));
	
	TestEqual("Future position should be 50 units ahead", Future.Value.X, 50.0);
	
	return true;
}

/**
 * Test: Control Fusion (Fuse Operator)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalFusionTest, "FlightProject.Functional.Pipes.ControlFusion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalFusionTest::RunTest(const FString& Parameters)
{
	// Layer 1: Reactive (Push away from 0)
	auto ReactiveLayer = [](float x) { return x > 0 ? 10.0f : -10.0f; };
	
	// Layer 2: Predictive (Stay at 0)
	auto PredictiveLayer = [](float x) { return -x; };
	
	// Weight: Panic near zero
	auto PanicFactor = [](float x) { return FMath::Abs(x) < 5.0f ? 1.0f : 0.0f; };

	auto FusedControl = Fuse(ReactiveLayer, PredictiveLayer, PanicFactor);

	// Test Case A: Far from zero (Should follow Predictive)
	float PosA = 20.0f;
	float ResultA = PosA | FusedControl;
	TestEqual("Should follow Predictive (-20)", ResultA, -20.0f);

	// Test Case B: Close to zero (Should follow Reactive)
	float PosB = 2.0f;
	float ResultB = PosB | FusedControl;
	TestEqual("Should follow Reactive (+10)", ResultB, 10.0f);

	return true;
}

/**
 * Integration: Lazy Chain with Heterogeneous Results
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalLazyChainIntegrationTest, "FlightProject.Functional.Integration.LazyChain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalLazyChainIntegrationTest::RunTest(const FString& Parameters)
{
	int32 CallCount = 0;

	// Use an initial value to avoid std::tuple<> ambiguity in Apply
	auto LazyValidation = LazyValidate<FString>()
		.Then([&](auto...) -> TResult<int32, FString> {
			CallCount++;
			return Ok(42);
		})
		.Then([&](int32 val) -> TResult<float, FString> {
			CallCount++;
			return Ok(val * 2.0f);
		})
		.Build();

	TestEqual("CallCount should be 0 before evaluation", CallCount, 0);

	// First evaluation
	auto Result = LazyValidation.Get();
	TestTrue("Result should be Ok", Result.IsOk());
	TestEqual("CallCount should be 2 after evaluation", CallCount, 2);
	
	float FloatVal = std::get<1>(Result.Unwrap());
	TestEqual("Final value should be 84.0", FloatVal, 84.0f);

	return true;
}

/**
 * Integration: Reactive State Flow with Monadic Chaining
 */
struct FSimState { 
	float Value = 0.0f; 
	bool bValid = true; 

	bool operator==(const FSimState& Other) const { return Value == Other.Value && bValid == Other.bValid; }
	FLIGHT_REFLECT_BODY(FSimState);
};

namespace Flight::Reflection {
	FLIGHT_REFLECT_FIELDS(FSimState, FLIGHT_FIELD(float, Value), FLIGHT_FIELD(bool, bValid))
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalReactiveStateFlowTest, "FlightProject.Functional.Integration.StateFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalReactiveStateFlowTest::RunTest(const FString& Parameters)
{
	TStateContainer<FSimState> State;
	float LastProcessedValue = 0.0f;

	// A pipe that validates and transforms
	auto ProcessPipe = FunctionalTransform([](const FSimState& S) -> TResult<float, FString> {
		if (!S.bValid) return TResult<float, FString>::Err(TEXT("Invalid State"));
		return TResult<float, FString>::Ok(S.Value * 10.0f);
	});

	// Subscribe and use monadic operations on the state change
	State.Subscribe([&](const FSimState&, const FSimState& New) {
		auto Result = New | ProcessPipe;
		
		// Use MoveTemp to satisfy Map() && requirement
		MoveTemp(Result).Map([&](float v) {
			LastProcessedValue = v;
			return v;
		});
	});

	// Test flow 1: Valid update
	State.Update([](FSimState& S) { S.Value = 5.0f; });
	TestEqual("Processed value should be 50", LastProcessedValue, 50.0f);

	// Test flow 2: Invalid update (should not update LastProcessedValue)
	State.Update([](FSimState& S) { S.bValid = false; S.Value = 10.0f; });
	TestEqual("Processed value should still be 50 (ignored invalid)", LastProcessedValue, 50.0f);

	return true;
}

/**
 * Test: Async continuation chain propagation
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFunctionalAsyncChainTest, "FlightProject.Functional.Async.ChainPropagation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FFunctionalAsyncChainTest::RunTest(const FString& Parameters)
{
	TAsyncOp<int32> Root;
	int32 FinalValue = 0;
	int32 LateSubscriberValue = 0;

	auto Chain = Root
		.Then([](int32 Value) { return Value + 1; })
		.Then([](int32 Value) { return Value * 2; });

	Chain.OnComplete([&](int32 Value) {
		FinalValue = Value;
	});

	Root.Complete(10);
	TestEqual("Async chain should propagate transformed value", FinalValue, 22);

	Chain.OnComplete([&](int32 Value) {
		LateSubscriberValue = Value;
	});
	TestEqual("Late subscriber should receive completed value", LateSubscriberValue, 22);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
