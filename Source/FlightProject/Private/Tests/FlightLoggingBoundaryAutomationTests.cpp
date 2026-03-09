// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/FlightLogging.h"
#include "UI/FlightLogCapture.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Logging;
using namespace Flight::Log;

namespace Flight::Logging::Test
{

class FCollectingSink : public ILogSink
{
public:
    virtual void Receive(const FLogEntry& Entry, const FLogContext& Context) override
    {
        Entries.Add(Entry);
        Contexts.Add(Context);
    }

    void Reset()
    {
        Entries.Reset();
        Contexts.Reset();
    }

    TArray<FLogEntry> Entries;
    TArray<FLogContext> Contexts;
};

class FOutputCaptureDevice : public FOutputDevice
{
public:
    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        Categories.Add(Category);
        Messages.Add(FString(V));
        Verbosities.Add(Verbosity);
    }

    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) override
    {
        Serialize(V, Verbosity, Category);
    }

    virtual bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }

    void Reset()
    {
        Categories.Reset();
        Messages.Reset();
        Verbosities.Reset();
    }

    bool HasMessage(const FName Category, const FString& Needle) const
    {
        for (int32 Index = 0; Index < Messages.Num(); ++Index)
        {
            if (Categories.IsValidIndex(Index)
                && Categories[Index] == Category
                && Messages[Index].Contains(Needle))
            {
                return true;
            }
        }

        return false;
    }

    bool HasMessageContaining(const FString& Needle) const
    {
        for (const FString& Message : Messages)
        {
            if (Message.Contains(Needle))
            {
                return true;
            }
        }
        return false;
    }

    int32 CountForCategory(const FName Category) const
    {
        int32 Count = 0;
        for (const FName& EntryCategory : Categories)
        {
            if (EntryCategory == Category)
            {
                ++Count;
            }
        }
        return Count;
    }

    int32 CountMessagesContaining(const FString& Needle) const
    {
        int32 Count = 0;
        for (const FString& Message : Messages)
        {
            if (Message.Contains(Needle))
            {
                ++Count;
            }
        }
        return Count;
    }

    TArray<FName> Categories;
    TArray<FString> Messages;
    TArray<ELogVerbosity::Type> Verbosities;
};

struct FBoundaryLogStruct
{
    FLIGHT_REFLECT_BODY(FBoundaryLogStruct);

    FString Name = TEXT("Boundary");
    bool bEnabled = true;
    int32 Count = 7;
};

} // namespace Flight::Logging::Test

namespace Flight::Reflection
{

FLIGHT_REFLECT_FIELDS_ATTR(Flight::Logging::Test::FBoundaryLogStruct,
    FLIGHT_FIELD_ATTR(FString, Name, Flight::Reflection::Attr::EditAnywhere),
    FLIGHT_FIELD_ATTR(bool, bEnabled, Flight::Reflection::Attr::EditAnywhere),
    FLIGHT_FIELD_ATTR(int32, Count, Flight::Reflection::Attr::EditAnywhere)
)

} // namespace Flight::Reflection

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FFlightLoggingBoundaryComplexTest,
    "FlightProject.Logging.Boundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightLoggingBoundaryComplexTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("InternalBufferBridge"));
    OutTestCommands.Add(TEXT("InternalBufferBridge"));

    OutBeautifiedNames.Add(TEXT("UnrealOutputBridge"));
    OutTestCommands.Add(TEXT("UnrealOutputBridge"));

    OutBeautifiedNames.Add(TEXT("DualSinkNoDuplication"));
    OutTestCommands.Add(TEXT("DualSinkNoDuplication"));

    OutBeautifiedNames.Add(TEXT("ReflectiveConstRefLogging"));
    OutTestCommands.Add(TEXT("ReflectiveConstRefLogging"));
}

bool FFlightLoggingBoundaryComplexTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Logging::Test;

    const FName BoundaryCategory(TEXT("LogFlightBoundary"));

    if (Parameters == TEXT("InternalBufferBridge"))
    {
        FGlobalLogCapture::Get().Clear();

        FLogContext Context;
        Context.KeyValues.Add(TEXT("Subsystem"), TEXT("Boundary"));
        Context.KeyValues.Add(TEXT("Mode"), TEXT("InternalOnly"));

        FLogger::Get().Log(ELogLevel::Display, BoundaryCategory, TEXT("Structured Buffer Message"), Context);

        const TArray<FLogEntry> Entries = FGlobalLogCapture::Get().GetBuffer().GetFiltered(FLogFilter{});
        const int32 MatchCount = Entries.FilterByPredicate([&BoundaryCategory](const FLogEntry& Entry)
        {
            return Entry.Category == BoundaryCategory && Entry.Message == TEXT("Structured Buffer Message");
        }).Num();
        const FLogEntry* Match = Entries.FindByPredicate([&BoundaryCategory](const FLogEntry& Entry)
        {
            return Entry.Category == BoundaryCategory && Entry.Message == TEXT("Structured Buffer Message");
        });

        TestEqual("Default runtime logger should write one structured entry into the internal buffer", MatchCount, 1);
        TestNotNull("Structured log should reach the internal buffer", Match);
        if (Match)
        {
            TestEqual("Structured context should be preserved in the internal buffer", Match->Context.FindRef(TEXT("Subsystem")), TEXT("Boundary"));
            TestEqual("Structured mode should be preserved in the internal buffer", Match->Context.FindRef(TEXT("Mode")), TEXT("InternalOnly"));
        }
    }
    else if (Parameters == TEXT("UnrealOutputBridge"))
    {
        FOutputCaptureDevice OutputCapture;
        GLog->AddOutputDevice(&OutputCapture);
        ON_SCOPE_EXIT
        {
            GLog->RemoveOutputDevice(&OutputCapture);
        };

        FLogContext Context;
        Context.KeyValues.Add(TEXT("Subsystem"), TEXT("Boundary"));

        FLogger::Get().Log(ELogLevel::Warning, BoundaryCategory, TEXT("Bridged Unreal Message"), Context);

        TestEqual("Default runtime logger should emit one Unreal output line for the bridged message", OutputCapture.CountMessagesContaining(TEXT("Bridged Unreal Message")), 1);
        TestTrue("Custom logger should bridge to Unreal output devices", OutputCapture.HasMessageContaining(TEXT("Bridged Unreal Message")));
        TestTrue("Formatted Unreal message should include structured context text", OutputCapture.HasMessageContaining(TEXT("Subsystem: Boundary")));
    }
    else if (Parameters == TEXT("DualSinkNoDuplication"))
    {
        FGlobalLogCapture::Get().Clear();

        FOutputCaptureDevice OutputCapture;
        GLog->AddOutputDevice(&OutputCapture);
        ON_SCOPE_EXIT
        {
            GLog->RemoveOutputDevice(&OutputCapture);
        };

        FLogger::Get().Log(ELogLevel::Log, BoundaryCategory, TEXT("Dual Sink Message"));

        const TArray<FLogEntry> Entries = FGlobalLogCapture::Get().GetBuffer().GetFiltered(FLogFilter{});
        const int32 BufferMatches = Entries.FilterByPredicate([&BoundaryCategory](const FLogEntry& Entry)
        {
            return Entry.Category == BoundaryCategory && Entry.Message == TEXT("Dual Sink Message");
        }).Num();

        TestEqual("Default runtime logger should not duplicate structured entries across the internal buffer bridge", BufferMatches, 1);
        TestEqual("Default runtime logger should not duplicate Unreal bridge output", OutputCapture.CountMessagesContaining(TEXT("Dual Sink Message")), 1);
    }
    else if (Parameters == TEXT("ReflectiveConstRefLogging"))
    {
        auto CollectingSink = MakeShared<FCollectingSink>();
        FLogger::Get().AddSink(CollectingSink);
        ON_SCOPE_EXIT
        {
            FLogger::Get().RemoveSink(CollectingSink);
        };

        FBoundaryLogStruct Value;
        Value.Name = TEXT("ConstRef");
        const FBoundaryLogStruct& ConstRef = Value;

        FLogger::Get().LogStruct(ELogLevel::Display, BoundaryCategory, ConstRef, TEXT("BoundaryStruct"));

        TestEqual("Reflective logging should emit a single structured entry", CollectingSink->Entries.Num(), 1);
        if (CollectingSink->Entries.Num() == 1)
        {
            const FLogEntry& Entry = CollectingSink->Entries[0];
            TestEqual(
                "Reflective message should include the reflected type name",
                Entry.Message,
                FString::Printf(TEXT("BoundaryStruct: %s"), UTF8_TO_TCHAR(TReflectTraits<FBoundaryLogStruct>::Name))
            );
            TestEqual("Const-ref reflective logging should serialize bool fields semantically", Entry.Context.FindRef(TEXT("bEnabled")), TEXT("true"));
            TestEqual("Const-ref reflective logging should serialize string fields", Entry.Context.FindRef(TEXT("Name")), TEXT("ConstRef"));
        }
    }

    return true;
}

#endif // WITH_AUTOMATION_TESTS
