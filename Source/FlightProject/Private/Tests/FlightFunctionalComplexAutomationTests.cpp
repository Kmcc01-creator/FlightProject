// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/FlightFunctional.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Functional;

namespace Flight::Functional::Test
{

static TResult<int32, FString> MakeSeedValue()
{
    return Ok(21);
}

struct FValidationHarness
{
    TResult<float, FString> Scale(int32 Value) const
    {
        return Ok(Value * 1.5f);
    }

    TResult<FString, FString> Describe(int32 Value, float Scaled) const
    {
        return Ok(FString::Printf(TEXT("%d->%.1f"), Value, Scaled));
    }
};

} // namespace Flight::Functional::Test

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FFlightFunctionalComplexTest,
    "FlightProject.Functional.Complex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightFunctionalComplexTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("ValidateShortCircuit"));
    OutTestCommands.Add(TEXT("ValidateShortCircuit"));

    OutBeautifiedNames.Add(TEXT("BindingVariants"));
    OutTestCommands.Add(TEXT("BindingVariants"));

    OutBeautifiedNames.Add(TEXT("LazyMemoization"));
    OutTestCommands.Add(TEXT("LazyMemoization"));

    OutBeautifiedNames.Add(TEXT("LazyChainDeferredShortCircuit"));
    OutTestCommands.Add(TEXT("LazyChainDeferredShortCircuit"));

    OutBeautifiedNames.Add(TEXT("RetryBehavior"));
    OutTestCommands.Add(TEXT("RetryBehavior"));
}

bool FFlightFunctionalComplexTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Functional::Test;

    if (Parameters == TEXT("ValidateShortCircuit"))
    {
        int32 Calls = 0;

        auto Result = Validate<FString>()
            .Then([&]() -> TResult<int32, FString>
            {
                ++Calls;
                return Ok(5);
            })
            .Then([&](int32 Value) -> TResult<float, FString>
            {
                ++Calls;
                return Err<float>(FString::Printf(TEXT("stop-%d"), Value));
            })
            .Then([&](int32, float) -> TResult<FString, FString>
            {
                ++Calls;
                return Ok(FString(TEXT("never")));
            })
            .Finish();

        TestTrue("Validation chain should stop on the first error", Result.IsErr());
        TestEqual("Validation chain should not evaluate steps after an error", Calls, 2);
        TestEqual("Validation chain should preserve the first error", Result.GetError(), TEXT("stop-5"));
    }
    else if (Parameters == TEXT("BindingVariants"))
    {
        FValidationHarness Harness;

        auto Result = Validate<FString>()
            .Then_Static(&MakeSeedValue)
            .Then_Method(&Harness, &FValidationHarness::Scale)
            .Then_Method(&Harness, &FValidationHarness::Describe)
            .Finish();

        TestTrue("Binding variants should compose into a successful validation chain", Result.IsOk());
        if (Result.IsOk())
        {
            auto Values = Result.Unwrap();
            TestEqual("Static binding should provide the first accumulated value", std::get<0>(Values), 21);
            TestEqual("Method binding should receive the previous accumulated value", std::get<1>(Values), 31.5f);
            TestEqual("Later method binding should see the full accumulated tuple", std::get<2>(Values), TEXT("21->31.5"));
        }
    }
    else if (Parameters == TEXT("LazyMemoization"))
    {
        int32 EvaluationCount = 0;

        TLazy<int32, FString> LazyValue([&EvaluationCount]() -> TResult<int32, FString>
        {
            ++EvaluationCount;
            return Ok(7);
        });

        TestFalse("Lazy value should start unevaluated", LazyValue.IsEvaluated());

        const TResult<int32, FString>& First = LazyValue.Get();
        const TResult<int32, FString>& Second = LazyValue.Get();

        TestTrue("Lazy value should evaluate on first access", First.IsOk());
        TestTrue("Lazy value should stay evaluated after access", LazyValue.IsEvaluated());
        TestEqual("Lazy value should memoize its thunk", EvaluationCount, 1);
        TestEqual("Cached lazy result should remain stable across repeated reads", Second.Unwrap(), 7);

        LazyValue.Collapse();
        TestEqual("Collapsing the thunk should keep the cached value intact", LazyValue.Get().Unwrap(), 7);
        TestEqual("Collapsing the thunk should not force re-evaluation", EvaluationCount, 1);
    }
    else if (Parameters == TEXT("LazyChainDeferredShortCircuit"))
    {
        int32 Calls = 0;

        auto LazyValidation = LazyValidate<FString>()
            .Then([&]() -> TResult<int32, FString>
            {
                ++Calls;
                return Ok(4);
            })
            .Then([&](int32 Value) -> TResult<float, FString>
            {
                ++Calls;
                return Err<float>(FString::Printf(TEXT("lazy-stop-%d"), Value));
            })
            .Then([&](int32, float) -> TResult<FString, FString>
            {
                ++Calls;
                return Ok(FString(TEXT("never")));
            })
            .Build();

        TestEqual("Lazy chain should remain fully deferred before access", Calls, 0);

        const TResult<std::tuple<int32, float, FString>, FString>& Result = LazyValidation.Get();
        TestTrue("Lazy chain should surface the first error", Result.IsErr());
        TestEqual("Lazy chain should stop evaluating after the failing step", Calls, 2);
        TestEqual("Lazy chain should preserve the deferred error payload", Result.GetError(), TEXT("lazy-stop-4"));
    }
    else if (Parameters == TEXT("RetryBehavior"))
    {
        int32 Attempts = 0;

        auto Result = Retry(3, [&Attempts]() -> TResult<int32, FString>
        {
            ++Attempts;
            if (Attempts < 3)
            {
                return TResult<int32, FString>::Err(TEXT("retry"));
            }
            return Ok(9);
        });

        TestTrue("Retry should eventually succeed when a later attempt returns Ok", Result.IsOk());
        TestEqual("Retry should stop once it succeeds", Attempts, 3);
        TestEqual("Retry should return the successful value", Result.Unwrap(), 9);
    }

    return true;
}

#endif // WITH_AUTOMATION_TESTS
