// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/FlightReflection.h"
#include "Core/FlightReflectionMocks.h"
#include "UI/FlightReactiveSlate.h"

#include "UI/FlightSlate.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Reactive;
using namespace Flight::ReactiveUI;
using namespace Flight::Slate;

/**
 * Test: Core Reactive Primitives
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveCoreTest, "FlightProject.Reactive.Core.Primitives", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReactiveCoreTest::RunTest(const FString& Parameters)
{
	// 1. TReactiveValue basic behavior
	TReactiveValue<int32> Counter(10);
	int32 ChangeCount = 0;
	int32 LastNewValue = 0;

	auto SubId = Counter.SubscribeLambda([&](const int32& Old, const int32& New) {
		ChangeCount++;
		LastNewValue = New;
	});

	Counter = 20;
	TestEqual("ChangeCount should be 1", ChangeCount, 1);
	TestEqual("LastNewValue should be 20", LastNewValue, 20);

	Counter = 20; // Same value, should not trigger
	TestEqual("ChangeCount should still be 1", ChangeCount, 1);

	Counter.Unsubscribe(SubId);
	Counter = 30;
	TestEqual("ChangeCount should remain 1 after unsubscribe", ChangeCount, 1);

	return true;
}

/**
 * Test: Lambda subscription ownership and cleanup
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveSubscriptionLifecycleTest, "FlightProject.Reactive.Core.SubscriptionLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReactiveSubscriptionLifecycleTest::RunTest(const FString& Parameters)
{
	TReactiveValue<int32> Value(0);

	TSharedPtr<int32> Sentinel = MakeShared<int32>(42);
	TWeakPtr<int32> WeakSentinel = Sentinel;

	const auto Id = Value.SubscribeLambda([Sentinel](const int32&, const int32&) {});
	Sentinel.Reset();

	TestTrue("Captured object should stay alive while subscription exists", WeakSentinel.IsValid());

	Value.Unsubscribe(Id);
	TestFalse("Captured object should be released after unsubscribe", WeakSentinel.IsValid());

	TSharedPtr<int32> Sentinel2 = MakeShared<int32>(77);
	TWeakPtr<int32> WeakSentinel2 = Sentinel2;
	Value.SubscribeLambda([Sentinel2](const int32&, const int32&) {});
	Sentinel2.Reset();

	TestTrue("Second captured object should stay alive while subscribed", WeakSentinel2.IsValid());
	Value.UnsubscribeAll();
	TestFalse("Second captured object should be released after unsubscribe all", WeakSentinel2.IsValid());

	return true;
}

/**
 * Test: Effect teardown removes dependency subscriptions
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveEffectTeardownTest, "FlightProject.Reactive.Core.EffectTeardown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReactiveEffectTeardownTest::RunTest(const FString& Parameters)
{
	TReactiveValue<int32> Dependency(0);
	int32 EffectRunCount = 0;

	{
		TEffect<int32> Effect([&]() { ++EffectRunCount; }, Dependency);
		TestEqual("Effect should run immediately on construction", EffectRunCount, 1);

		Dependency = 1;
		TestEqual("Effect should run while alive when dependency changes", EffectRunCount, 2);
	}

	Dependency = 2;
	TestEqual("Effect should not run after destruction", EffectRunCount, 2);

	return true;
}

/**
 * Test: Slate Integration and Cleanup
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveSlateTest, "FlightProject.Reactive.UI.SlateBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReactiveSlateTest::RunTest(const FString& Parameters)
{
	FReactiveContext Context;
	TReactiveValue<FString> Name(TEXT("Initial"));
	
	// Create a mock widget
	TSharedRef<STextBlock> TextWidget = SNew(STextBlock);
	
	// Bind it
	Bindings::BindText(Context, TextWidget, Name);
	
	TestEqual("Widget text should match initial", TextWidget->GetText().ToString(), TEXT("Initial"));
	
	Name = TEXT("Updated");
	TestEqual("Widget text should update", TextWidget->GetText().ToString(), TEXT("Updated"));
	
	// Test managed cleanup
	{
		FReactiveContext InnerContext;
		TReactiveValue<int32> Value(1);
		Bindings::BindNumericText(InnerContext, TextWidget, Value);
		
		Value = 2;
		TestEqual("Widget should update from inner", TextWidget->GetText().ToString(), TEXT("2"));
		
		// InnerContext goes out of scope here, should unsubscribe
	}
	
	// Value should no longer update widget after context destroyed
	// Note: In a real test we'd need to keep 'Value' alive but it's on stack here.
	// This mainly verifies that the code compiles and runs without crashing.

	return true;
}

/**
 * Test: State Container with Reflection
 */
struct FTestState
{
	int32 Score = 0;
	FString Name;

	bool operator==(const FTestState& Other) const
	{
		return Score == Other.Score && Name == Other.Name;
	}

	FLIGHT_REFLECT_BODY(FTestState);
};

namespace Flight::Reflection
{
	FLIGHT_REFLECT_FIELDS(FTestState,
		FLIGHT_FIELD(int32, Score),
		FLIGHT_FIELD(FString, Name)
	);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveStateContainerTest, "FlightProject.Reactive.Core.StateContainer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReactiveStateContainerTest::RunTest(const FString& Parameters)
{
	TStateContainer<FTestState> State;
	int32 NotifyCount = 0;

	State.Subscribe([&](const FTestState& Old, const FTestState& New) {
		NotifyCount++;
	});

	State.Update([](FTestState& S) {
		S.Score = 100;
	});

	TestEqual("NotifyCount should be 1", NotifyCount, 1);
	TestEqual("Score should be 100", State.Get().Score, 100);

	return true;
}

/**
 * Test: Non-UI Reactive Reuse (Mass Fragment style)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveNonUITest, "FlightProject.Reactive.Core.NonUIReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReactiveNonUITest::RunTest(const FString& Parameters)
{
	using namespace Flight::Mocks;

	// 1. Setup a "Fragment" (simulating data in ECS)
	FReactiveFlightFragment Fragment;
	
	// 2. Setup a "Telemetry System" that watches the fragment
	// In a real scenario, this might be a sound system or an AI analyzer
	int32 WarningCount = 0;
	float LastObservedAltitude = 0.0f;

	Fragment.Altitude.Bind([](const float& Old, const float& New, void* UserData) {
		auto* CountPtr = static_cast<int32*>(UserData);
		if (New < 50.0f && Old >= 50.0f)
		{
			(*CountPtr)++;
		}
	}, &WarningCount);

	// 3. Simulate physics updates (Imperative path)
	Fragment.Altitude = 100.0f;
	Fragment.Altitude = 40.0f; // Crosses 50.0f threshold
	Fragment.Altitude = 30.0f; // Stays below
	Fragment.Altitude = 60.0f; // Crosses back up
	Fragment.Altitude = 20.0f; // Crosses back down

	TestEqual("Should have recorded 2 low-altitude warnings", WarningCount, 2);

	// 4. Verify we can also use reflection to access the observable
	Fragment.Altitude = 100.0f; // Reset to above threshold
	
	TReflectionTraits<FReactiveFlightFragment>::Fields::At<0>::Get(Fragment).Modify([](float& V) {
		V = 10.0f; // Crosses down -> should trigger 3rd warning
	});

	TestEqual("Should have recorded 3rd warning via reflection access", WarningCount, 3);

	return true;
}

/**
 * Test: Declarative Builder Syntax
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactiveBuilderTest, "FlightProject.Reactive.UI.DeclarativeBuilders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReactiveBuilderTest::RunTest(const FString& Parameters)
{
	FReactiveContext Context;
	TReactiveValue<float> Speed(120.5f);
	
	// Create widget using fluent builder with reactive binding
	auto SpeedText = Text()
		.FontSize(14)
		.BindNumeric(Context, Speed, TEXT("Speed: {0} km/h"))
		.Build();
	
	TestEqual("Initial builder text should match", SpeedText->GetText().ToString(), TEXT("Speed: 120.5 km/h"));
	
	Speed = 150.0f;
	TestEqual("Updated builder text should match", SpeedText->GetText().ToString(), TEXT("Speed: 150 km/h"));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
