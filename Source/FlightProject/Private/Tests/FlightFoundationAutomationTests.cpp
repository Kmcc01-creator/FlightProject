// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/FlightLogging.h"
#include "UI/FlightLogCapture.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Functional;
using namespace Flight::Logging;
using namespace Flight::Log;
using namespace Flight::Reflection;

namespace
{

class FCapturingSink : public ILogSink
{
public:
    virtual void Receive(const FLogEntry& Entry, const FLogContext& Context) override
    {
        ReceivedEntries.Add(Entry);
        ReceivedContexts.Add(Context);
    }

    void Reset()
    {
        ReceivedEntries.Reset();
        ReceivedContexts.Reset();
    }

    TArray<FLogEntry> ReceivedEntries;
    TArray<FLogContext> ReceivedContexts;
};

struct FFoundationMetrics
{
    FLIGHT_REFLECT_BODY(FFoundationMetrics);

    int32 RetryCount = 3;
    bool bHealthy = true;

    bool operator==(const FFoundationMetrics& Other) const
    {
        return RetryCount == Other.RetryCount && bHealthy == Other.bHealthy;
    }
};

struct FFoundationPayload
{
    FLIGHT_REFLECT_BODY(FFoundationPayload);

    FString Label = TEXT("Payload");
    float Score = 17.5f;
    FFoundationMetrics Metrics;
};

} // namespace

namespace Flight::Reflection
{

FLIGHT_REFLECT_FIELDS_ATTR(
    FFoundationMetrics,
    FLIGHT_FIELD_ATTR(int32, RetryCount, ::Flight::Reflection::Attr::EditAnywhere),
    FLIGHT_FIELD_ATTR(bool, bHealthy, ::Flight::Reflection::Attr::EditAnywhere)
)

FLIGHT_REFLECT_FIELDS_ATTR(
    FFoundationPayload,
    FLIGHT_FIELD_ATTR(FString, Label, ::Flight::Reflection::Attr::EditAnywhere),
    FLIGHT_FIELD_ATTR(float, Score, ::Flight::Reflection::Attr::EditAnywhere),
    FLIGHT_FIELD_ATTR(FFoundationMetrics, Metrics, ::Flight::Reflection::Attr::EditAnywhere)
)

} // namespace Flight::Reflection

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FFlightFoundationComplexTest,
    "FlightProject.Complex.Foundations.LoggingReflectionFunctional",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightFoundationComplexTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("CustomLogBufferBridge"));
    OutTestCommands.Add(TEXT("CustomLogBufferBridge"));

    OutBeautifiedNames.Add(TEXT("UnrealCaptureTokenizedFilter"));
    OutTestCommands.Add(TEXT("UnrealCaptureTokenizedFilter"));

    OutBeautifiedNames.Add(TEXT("ReactiveStreamMirror"));
    OutTestCommands.Add(TEXT("ReactiveStreamMirror"));

    OutBeautifiedNames.Add(TEXT("ConstRefReflectiveLogging"));
    OutTestCommands.Add(TEXT("ConstRefReflectiveLogging"));

    OutBeautifiedNames.Add(TEXT("FunctionalResultLogging"));
    OutTestCommands.Add(TEXT("FunctionalResultLogging"));

    OutBeautifiedNames.Add(TEXT("ReflectionPatchLoggingRoundTrip"));
    OutTestCommands.Add(TEXT("ReflectionPatchLoggingRoundTrip"));
}

bool FFlightFoundationComplexTest::RunTest(const FString& Parameters)
{
    auto FindExactMessage = [](const TArray<FLogEntry>& Entries, FName Category, const FString& Message) -> const FLogEntry*
    {
        for (const FLogEntry& Entry : Entries)
        {
            if (Entry.Category == Category && Entry.Message == Message)
            {
                return &Entry;
            }
        }
        return nullptr;
    };

    if (Parameters == TEXT("CustomLogBufferBridge"))
    {
        const FName Category(TEXT("LogFoundationBoundary"));
        const FString Message = FString::Printf(TEXT("CustomBoundary-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

        FGlobalLogCapture::Initialize();
        FGlobalLogCapture::Get().Clear();

        FLogContext Context;
        Context.KeyValues.Add(TEXT("domain"), TEXT("custom"));
        Context.KeyValues.Add(TEXT("transport"), TEXT("buffer"));

        FLogger::Get().Log(ELogLevel::Warning, Category, Message, Context);

        FLogFilter Filter;
        Filter.Category.IncludeCategories.Add(Category);
        Filter.Text.SetSearchText(Message);
        const TArray<FLogEntry> Entries = FGlobalLogCapture::Get().GetBuffer().GetFiltered(Filter);

        const FLogEntry* Match = FindExactMessage(Entries, Category, Message);
        TestNotNull("Custom logger should feed the shared capture buffer", Match);
        if (Match)
        {
            TestEqual("Custom logger should preserve structured context", Match->Context.FindRef(TEXT("domain")), TEXT("custom"));
            TestEqual("Custom logger should preserve transport marker", Match->Context.FindRef(TEXT("transport")), TEXT("buffer"));
        }
    }
    else if (Parameters == TEXT("UnrealCaptureTokenizedFilter"))
    {
        const FName Category(TEXT("LogFoundationFilter"));
        const FString MatchMessage = FString::Printf(TEXT("critical alpha %s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        const FString NoiseMessage = FString::Printf(TEXT("noise beta %s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

        FGlobalLogCapture::Initialize();
        FGlobalLogCapture::Get().Clear();

        FMsg::Logf(nullptr, 0, Category, ELogVerbosity::Warning, TEXT("%s"), *MatchMessage);
        FMsg::Logf(nullptr, 0, FName(TEXT("LogFoundationNoise")), ELogVerbosity::Display, TEXT("%s"), *NoiseMessage);

        FLogFilter Filter;
        Filter.Text.SetSearchText(TEXT("critical cat:FoundationFilter -noise"));
        const TArray<FLogEntry> Entries = FGlobalLogCapture::Get().GetBuffer().GetFiltered(Filter);

        TestEqual("Tokenized filter should isolate the matching Unreal log", Entries.Num(), 1);
        if (Entries.Num() == 1)
        {
            TestEqual("Filtered entry category should match", Entries[0].Category, Category);
            TestEqual("Filtered entry message should match", Entries[0].Message, MatchMessage);
        }
    }
    else if (Parameters == TEXT("ReactiveStreamMirror"))
    {
        FLogCaptureDevice Device;
        FReactiveLogStream Stream(Device);
        Stream.SetVerbosityLevel(EVerbosity::Warning);

        Device.Serialize(TEXT("warning-signal"), ELogVerbosity::Warning, FName(TEXT("LogReactiveMirror")));
        Device.Serialize(TEXT("verbose-noise"), ELogVerbosity::Log, FName(TEXT("LogReactiveMirror")));

        TestEqual("Reactive stream should track total entry count", Stream.TotalCount().Get(), 2);
        TestEqual("Reactive stream should track warning count", Stream.WarningCount().Get(), 1);
        TestEqual("Warning filter should keep only warning entries", Stream.FilteredLogs().Get().Num(), 1);

        Stream.SetSearchText(TEXT("warning"));
        TestEqual("Text filter should keep the matching warning", Stream.FilteredLogs().Get().Num(), 1);
        if (Stream.FilteredLogs().Get().Num() == 1)
        {
            TestEqual("Filtered warning message should match", Stream.FilteredLogs().Get()[0].Message, TEXT("warning-signal"));
        }

        Stream.SetSearchText(TEXT("missing"));
        TestEqual("Text filter should remove unmatched entries", Stream.FilteredLogs().Get().Num(), 0);
    }
    else if (Parameters == TEXT("ConstRefReflectiveLogging"))
    {
        auto TestSink = MakeShared<FCapturingSink>();
        FLogger::Get().AddSink(TestSink);
        ON_SCOPE_EXIT
        {
            FLogger::Get().RemoveSink(TestSink);
        };

        TestSink->Reset();

        FFoundationPayload Payload;
        Payload.Label = TEXT("ConstRefPayload");
        Payload.Score = 23.25f;
        Payload.Metrics.RetryCount = 7;
        const FFoundationPayload& ConstPayload = Payload;

        TestTrue("Const-ref payload should satisfy CReflectable", CReflectable<const FFoundationPayload&>);
        FLogger::Get().LogStruct(ELogLevel::Display, FName(TEXT("LogFoundationReflect")), ConstPayload, TEXT("ConstRef"));

        TestEqual("Reflective logging should emit a single sink event", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() == 1)
        {
            const FLogEntry& Entry = TestSink->ReceivedEntries[0];
            TestEqual("Message should include prefix and reflected type", Entry.Message, TEXT("ConstRef: FFoundationPayload"));
            TestEqual("Reflected string field should be present", Entry.Context.FindRef(TEXT("Label")), TEXT("ConstRefPayload"));
            TestEqual("Reflected arithmetic field should be present", Entry.Context.FindRef(TEXT("Score")), TEXT("23.25"));
            TestEqual("Nested reflectable field should use placeholder formatting", Entry.Context.FindRef(TEXT("Metrics")), TEXT("{...}"));
        }
    }
    else if (Parameters == TEXT("FunctionalResultLogging"))
    {
        auto TestSink = MakeShared<FCapturingSink>();
        FLogger::Get().AddSink(TestSink);
        ON_SCOPE_EXIT
        {
            FLogger::Get().RemoveSink(TestSink);
        };

        TestSink->Reset();

        TResult<int32, FString> OkResult = TResult<int32, FString>::Ok(7);
        TResult<int32, FString> ReturnedOk = TapLog(MoveTemp(OkResult), FName(TEXT("LogFoundationFunctional")));
        TestTrue("TapLog should preserve successful TResult state", ReturnedOk.IsOk());
        if (ReturnedOk.IsOk())
        {
            TestEqual("TapLog should preserve the ok payload", ReturnedOk.Unwrap(), 7);
        }

        TResult<int32, FString> ErrResult = TResult<int32, FString>::Err(TEXT("Exploded"));
        TGuardValue<bool> SuppressRecursiveLogging(bIsLoggingInternal, true);
        TResult<int32, FString> ReturnedErr = LogErr(MoveTemp(ErrResult), FName(TEXT("LogFoundationFunctional")));
        TestTrue("LogErr should preserve error TResult state", ReturnedErr.IsErr());
        if (ReturnedErr.IsErr())
        {
            TestEqual("LogErr should preserve the error payload", ReturnedErr.GetError(), TEXT("Exploded"));
        }

        TestEqual("Functional logging should emit the success log when logging is enabled", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() == 1)
        {
            TestTrue("Success message should be logged", TestSink->ReceivedEntries[0].Message.Contains(TEXT("Success")));
        }
    }
    else if (Parameters == TEXT("ReflectionPatchLoggingRoundTrip"))
    {
        auto TestSink = MakeShared<FCapturingSink>();
        FLogger::Get().AddSink(TestSink);
        ON_SCOPE_EXIT
        {
            FLogger::Get().RemoveSink(TestSink);
        };

        TestSink->Reset();

        FFoundationPayload Baseline;
        Baseline.Label = TEXT("Before");
        Baseline.Score = 1.0f;
        Baseline.Metrics.RetryCount = 1;

        FFoundationPayload Updated = Baseline;
        Updated.Label = TEXT("After");
        Updated.Score = 9.5f;
        Updated.Metrics.RetryCount = 4;

        auto Patch = Diff(Baseline, Updated);
        Apply(Baseline, Patch);

        TestEqual("Patch should apply updated label", Baseline.Label, TEXT("After"));
        TestEqual("Patch should apply updated score", Baseline.Score, 9.5f);
        TestEqual("Patch should apply updated nested payload", Baseline.Metrics.RetryCount, 4);

        FLogger::Get().LogStruct(ELogLevel::Log, FName(TEXT("LogFoundationPatch")), Baseline, TEXT("Patched"));
        TestEqual("Patched payload should be loggable after reflection apply", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() == 1)
        {
            TestEqual("Patched payload should log the updated label", TestSink->ReceivedEntries[0].Context.FindRef(TEXT("Label")), TEXT("After"));
            TestEqual("Patched payload should log the updated score", TestSink->ReceivedEntries[0].Context.FindRef(TEXT("Score")), TEXT("9.5"));
        }
    }

    return true;
}

#endif // WITH_AUTOMATION_TESTS
