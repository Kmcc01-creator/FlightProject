// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/FlightLogging.h"
#include "UI/FlightLogCapture.h"
#include "UI/FlightVexUI.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Logging;

/**
 * Custom sink for test verification
 */
class FTestLogSink : public ILogSink
{
public:
    virtual void Receive(const FLogEntry& Entry, const FLogContext& Context) override
    {
        ReceivedEntries.Add(Entry);
        ReceivedContexts.Add(Context);
    }

    TArray<FLogEntry> ReceivedEntries;
    TArray<FLogContext> ReceivedContexts;

    void Reset()
    {
        ReceivedEntries.Empty();
        ReceivedContexts.Empty();
    }
};

/**
 * Reflectable test struct
 */
struct FLoggingTestStruct
{
    FLIGHT_REFLECT_BODY(FLoggingTestStruct);

    FString Name = TEXT("TestInstance");
    FVector Position = FVector(100.0f, 200.0f, 300.0f);
    int32 Count = 42;
    bool bActive = true;
};

FLIGHT_REFLECT_FIELDS_ATTR(
    FLoggingTestStruct,
    FLIGHT_FIELD(FString, Name),
    FLIGHT_FIELD(FVector, Position),
    FLIGHT_FIELD(int32, Count),
    FLIGHT_FIELD(bool, bActive)
)

/**
 * Complex Automation Test for Flight Logging System
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FFlightLoggingComplexTest,
    "FlightProject.Logging.Complex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightLoggingComplexTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("SinkDispatch"));
    OutTestCommands.Add(TEXT("SinkDispatch"));

    OutBeautifiedNames.Add(TEXT("RecursionGuard"));
    OutTestCommands.Add(TEXT("RecursionGuard"));

    OutBeautifiedNames.Add(TEXT("ReflectiveLogging"));
    OutTestCommands.Add(TEXT("ReflectiveLogging"));

    OutBeautifiedNames.Add(TEXT("ReflectiveLoggingConstRef"));
    OutTestCommands.Add(TEXT("ReflectiveLoggingConstRef"));

    OutBeautifiedNames.Add(TEXT("RowLogging"));
    OutTestCommands.Add(TEXT("RowLogging"));

    OutBeautifiedNames.Add(TEXT("MonadicLogging"));
    OutTestCommands.Add(TEXT("MonadicLogging"));

    OutBeautifiedNames.Add(TEXT("UnrealBridge"));
    OutTestCommands.Add(TEXT("UnrealBridge"));

    OutBeautifiedNames.Add(TEXT("InternalBufferBridge"));
    OutTestCommands.Add(TEXT("InternalBufferBridge"));

    OutBeautifiedNames.Add(TEXT("VexUIIntegration"));
    OutTestCommands.Add(TEXT("VexUIIntegration"));
}

bool FFlightLoggingComplexTest::RunTest(const FString& Parameters)
{
    auto TestSink = MakeShared<FTestLogSink>();
    FLogger::Get().AddSink(TestSink);

    // Ensure we clean up the sink after the test
    ON_SCOPE_EXIT
    {
        FLogger::Get().RemoveSink(TestSink);
    };

    if (Parameters == TEXT("SinkDispatch"))
    {
        TestSink->Reset();
        
        FLogContext Context;
        Context.KeyValues.Add(TEXT("TestKey"), TEXT("TestValue"));
        
        FLogger::Get().Log(ELogLevel::Warning, "LogTest", TEXT("Dispatched Message"), Context);

        TestEqual("Should receive exactly one entry", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() > 0)
        {
            TestEqual("Message should match", TestSink->ReceivedEntries[0].Message, TEXT("Dispatched Message"));
            TestEqual("Category should match", TestSink->ReceivedEntries[0].Category.ToString(), TEXT("LogTest"));
            TestEqual("Verbosity should match", (int32)TestSink->ReceivedEntries[0].Verbosity, (int32)ELogLevel::Warning);
            TestTrue("Context should contain TestKey", TestSink->ReceivedEntries[0].Context.Contains(TEXT("TestKey")));
            TestEqual("Context value should match", TestSink->ReceivedEntries[0].Context.FindRef(TEXT("TestKey")), TEXT("TestValue"));
        }
    }
    else if (Parameters == TEXT("RecursionGuard"))
    {
        TestSink->Reset();
        
        // Register a sink that logs again (infinite recursion if not guarded)
        struct FRecursiveSink : public ILogSink
        {
            virtual void Receive(const FLogEntry& Entry, const FLogContext& Context) override
            {
                FLogger::Get().Log(ELogLevel::Log, "LogRecursive", TEXT("Nested Log"));
            }
        };
        auto RecSink = MakeShared<FRecursiveSink>();
        FLogger::Get().AddSink(RecSink);

        FLogger::Get().Log(ELogLevel::Log, "LogTest", TEXT("Root Log"));

        FLogger::Get().RemoveSink(RecSink);

        // We should have 1 entry in TestSink (the Root Log).
        // The RecSink was called, tried to log "Nested Log", but FLogger's guard should have blocked it.
        TestEqual("Recursion guard should limit total logs to 1", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() > 0)
        {
            TestEqual("Only the root log should be present", TestSink->ReceivedEntries[0].Message, TEXT("Root Log"));
        }
    }
    else if (Parameters == TEXT("ReflectiveLogging"))
    {
        TestSink->Reset();
        
        FLoggingTestStruct Instance;
        Instance.Name = TEXT("ReflectedInstance");
        Instance.Count = 99;

        FLogger::Get().LogStruct(ELogLevel::Log, "LogTest", Instance, TEXT("MyStruct"));

        TestEqual("Should receive entry from LogStruct", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() > 0)
        {
            TestEqual("Message should include prefix and type", TestSink->ReceivedEntries[0].Message, TEXT("MyStruct: FLoggingTestStruct"));
            const auto& Context = TestSink->ReceivedEntries[0].Context;
            TestEqual("Reflected Name should match", Context.FindRef(TEXT("Name")), TEXT("ReflectedInstance"));
            TestEqual("Reflected Count should match", Context.FindRef(TEXT("Count")), TEXT("99"));
            TestEqual("Reflected bActive should match", Context.FindRef(TEXT("bActive")), TEXT("true"));
            TestTrue("Reflected Position should be present", Context.Contains(TEXT("Position")));
        }
    }
    else if (Parameters == TEXT("ReflectiveLoggingConstRef"))
    {
        TestSink->Reset();

        FLoggingTestStruct Instance;
        Instance.Name = TEXT("ConstRefInstance");
        const FLoggingTestStruct& ConstRef = Instance;

        FLogger::Get().LogStruct(ELogLevel::Display, "LogTest", ConstRef, TEXT("ConstRef"));

        TestEqual("Const-ref reflective logging should produce one entry", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() > 0)
        {
            const auto& Context = TestSink->ReceivedEntries[0].Context;
            TestEqual("Message should include prefix and type", TestSink->ReceivedEntries[0].Message, TEXT("ConstRef: FLoggingTestStruct"));
            TestEqual("Bool values should preserve semantic formatting", Context.FindRef(TEXT("bActive")), TEXT("true"));
            TestEqual("Name should match through const-ref reflection", Context.FindRef(TEXT("Name")), TEXT("ConstRefInstance"));
        }
    }
    else if (Parameters == TEXT("RowLogging"))
    {
        TestSink->Reset();

        using MyRow = TRow<
            TRowField<"Label", FString>,
            TRowField<"Metric", float>
        >;

        MyRow Row;
        Row.Set<"Label">(TEXT("Performance"));
        Row.Set<"Metric">(120.5f);

        FLogger::Get().LogRow(ELogLevel::Display, "LogTest", Row, TEXT("Ad-hoc Data"));

        TestEqual("Should receive entry from LogRow", TestSink->ReceivedEntries.Num(), 1);
        if (TestSink->ReceivedEntries.Num() > 0)
        {
            TestEqual("Message should match", TestSink->ReceivedEntries[0].Message, TEXT("Ad-hoc Data"));
            const auto& Context = TestSink->ReceivedEntries[0].Context;
            TestEqual("Row Label should match", Context.FindRef(TEXT("Label")), TEXT("Performance"));
            TestEqual("Row Metric should match", Context.FindRef(TEXT("Metric")), TEXT("120.5"));
        }
    }
    else if (Parameters == TEXT("MonadicLogging"))
    {
        TestSink->Reset();

        using namespace Flight::Functional;

        // Success path
        auto SuccessResult = TResult<int32, FString>::Ok(42);
        TapLog(MoveTemp(SuccessResult), "LogTest");
        TestEqual("Should log success", TestSink->ReceivedEntries.Num(), 1);
        TestTrue("Message should indicate success", TestSink->ReceivedEntries[0].Message.Contains(TEXT("Success")));

        // Error path
        TestSink->Reset();
        auto ErrorResult = TResult<int32, FString>::Err(TEXT("Critical Failure"));
        AddExpectedError(TEXT("Operation Failed: Critical Failure"), EAutomationExpectedErrorFlags::Contains, 1);
        LogErr(MoveTemp(ErrorResult), "LogTest");
        TestEqual("Should log error", TestSink->ReceivedEntries.Num(), 1);
        TestTrue("Message should contain error message", TestSink->ReceivedEntries[0].Message.Contains(TEXT("Critical Failure")));
    }
    else if (Parameters == TEXT("UnrealBridge"))
    {
        Flight::Log::FScopedLogCapture Capture;

        FUnrealLogSink UnrealSink;
        FLogContext Context;
        Context.KeyValues.Add(TEXT("BridgeKey"), TEXT("BridgeValue"));

        FLogEntry Entry(0, 0.0, ELogLevel::Warning, TEXT("LogBridgeTest"), TEXT("Bridge Message"));
        UnrealSink.Receive(Entry, Context);

        const TArray<FLogEntry> Logs = Capture.GetLogs();
        const FLogEntry* MatchingEntry = nullptr;
        for (const FLogEntry& Captured : Logs)
        {
            if (Captured.Category == TEXT("LogBridgeTest") && Captured.Message.Contains(TEXT("Bridge Message")))
            {
                MatchingEntry = &Captured;
                break;
            }
        }

        TestNotNull("Unreal bridge should emit a captured Unreal log entry", MatchingEntry);
        if (MatchingEntry)
        {
            TestTrue("Captured Unreal log should contain formatted context", MatchingEntry->Message.Contains(TEXT("BridgeKey: BridgeValue")));
        }
    }
    else if (Parameters == TEXT("InternalBufferBridge"))
    {
        Flight::Log::FGlobalLogCapture::Get().Clear();

        FInternalLogService InternalSink;
        FLogContext Context;
        Context.KeyValues.Add(TEXT("InternalKey"), TEXT("InternalValue"));

        FLogEntry Entry(0, 0.0, ELogLevel::Log, TEXT("LogInternalBridge"), TEXT("Internal bridge message"));
        Entry.Context = Context.KeyValues;

        InternalSink.Receive(Entry, Context);

        const TArray<FLogEntry> Logs = Flight::Log::FGlobalLogCapture::Get().GetBuffer().Filter([](const FLogEntry& Captured) {
            return Captured.Category == TEXT("LogInternalBridge");
        });

        TestEqual("Internal buffer bridge should append exactly one matching entry", Logs.Num(), 1);
        if (Logs.Num() == 1)
        {
            TestEqual("Buffered message should match", Logs[0].Message, TEXT("Internal bridge message"));
            TestEqual("Buffered context should retain structured values", Logs[0].Context.FindRef(TEXT("InternalKey")), TEXT("InternalValue"));
        }
    }
    else if (Parameters == TEXT("VexUIIntegration"))
    {
        TestSink->Reset();

        using namespace Flight::VexUI;
        
        const FString Script =
            TEXT("log(\"Static Message\")\n")
            TEXT("log(@my_symbol)\n");

        FVexUiCompileResult CompileResult = FVexUiCompiler::Compile(Script);
        TestTrue("Vex UI log script should compile", CompileResult.bSuccess);
        TestEqual("Should have 2 nodes", CompileResult.Nodes.Num(), 2);

        if (CompileResult.Nodes.Num() == 2)
        {
            TestEqual("First node should be Log", CompileResult.Nodes[0].Type, EVexUiNodeType::Log);
            TestEqual("First node text should match", CompileResult.Nodes[0].Text, TEXT("Static Message"));
            TestEqual("Second node symbol should match", CompileResult.Nodes[1].Symbol, TEXT("@my_symbol"));
        }
    }

    return true;
}

#endif // WITH_AUTOMATION_TESTS
