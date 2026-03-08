// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UI/FlightLogTypes.h"

#if WITH_AUTOMATION_TESTS

using namespace Flight::Log;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLogQueryFilterTest,
    "FlightProject.Logging.Core.QueryFilter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FLogQueryFilterTest::RunTest(const FString& Parameters)
{
    FLogFilter Filter;
    Filter.Text.SetSearchText(TEXT("cat:LogFlightAI warning -deprecated thread:42 frame:100-200"));

    FLogEntry Matching(
        0,
        1.0,
        EVerbosity::Warning,
        TEXT("LogFlightAI"),
        TEXT("warning: steering target acquired"));
    Matching.ThreadId = 42;
    Matching.FrameNumber = 150;

    TestTrue("Tokenized query should match valid entry", Filter.Matches(Matching));

    FLogEntry WrongCategory = Matching;
    WrongCategory.Category = TEXT("LogFlightMass");
    TestFalse("cat: token should reject unmatched categories", Filter.Matches(WrongCategory));

    FLogEntry WrongThread = Matching;
    WrongThread.ThreadId = 77;
    TestFalse("thread: token should reject unmatched thread IDs", Filter.Matches(WrongThread));

    FLogEntry WrongFrame = Matching;
    WrongFrame.FrameNumber = 250;
    TestFalse("frame: token should reject entries outside range", Filter.Matches(WrongFrame));

    FLogEntry ExcludedTerm = Matching;
    ExcludedTerm.Message = TEXT("warning: deprecated steering path");
    TestFalse("Negative terms should reject matching entries", Filter.Matches(ExcludedTerm));

    FLogFilter LegacyFilter;
    LegacyFilter.Text.SetSearchText(TEXT("target acquired"));
    TestTrue("Legacy substring search should still match", LegacyFilter.Matches(Matching));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLogRingBufferStatsTest,
    "FlightProject.Logging.Core.RingBufferStats",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FLogRingBufferStatsTest::RunTest(const FString& Parameters)
{
    TLogRingBuffer<3> Buffer;

    auto MakeEntry = [](EVerbosity Verbosity, FName Category, const TCHAR* Message, uint32 Frame) {
        FLogEntry Entry(0, static_cast<double>(Frame) * 0.01, Verbosity, Category, FString(Message));
        Entry.FrameNumber = Frame;
        Entry.ThreadId = 1;
        return Entry;
    };

    Buffer.Add(MakeEntry(EVerbosity::Error, TEXT("LogFlightAI"), TEXT("error one"), 10));
    Buffer.Add(MakeEntry(EVerbosity::Warning, TEXT("LogFlightNav"), TEXT("warn one"), 11));
    Buffer.Add(MakeEntry(EVerbosity::Log, TEXT("LogFlightAI"), TEXT("info one"), 12));

    FLogStats InitialStats = Buffer.GetStatsSnapshot();
    TestEqual("Initial total should be 3", static_cast<int32>(InitialStats.TotalEntries), 3);
    TestEqual("Initial AI category count should be 2", static_cast<int32>(InitialStats.CountByCategory.FindRef(TEXT("LogFlightAI"))), 2);
    TestEqual("Initial warning count should be 1", static_cast<int32>(InitialStats.WarningCount()), 1);
    TestEqual("Initial error count should be 1", static_cast<int32>(InitialStats.ErrorCount()), 1);

    Buffer.Add(MakeEntry(EVerbosity::Log, TEXT("LogFlightMass"), TEXT("info two"), 13));

    FLogStats OverwriteStats = Buffer.GetStatsSnapshot();
    TestEqual("Total should stay bounded by ring capacity", static_cast<int32>(OverwriteStats.TotalEntries), 3);
    TestEqual("AI category count should drop to 1 after overwrite", static_cast<int32>(OverwriteStats.CountByCategory.FindRef(TEXT("LogFlightAI"))), 1);
    TestEqual("Nav category count should remain 1", static_cast<int32>(OverwriteStats.CountByCategory.FindRef(TEXT("LogFlightNav"))), 1);
    TestEqual("Mass category count should be 1", static_cast<int32>(OverwriteStats.CountByCategory.FindRef(TEXT("LogFlightMass"))), 1);
    TestEqual("Error count should be 0 after overwriting the error entry", static_cast<int32>(OverwriteStats.ErrorCount()), 0);
    TestEqual("First frame should advance to remaining oldest entry", static_cast<int32>(OverwriteStats.FirstFrame), 11);
    TestEqual("Last frame should be latest entry frame", static_cast<int32>(OverwriteStats.LastFrame), 13);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLogAutomationResultColorTest,
    "FlightProject.Logging.Core.AutomationResultColoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FLogAutomationResultColorTest::RunTest(const FString& Parameters)
{
    const EAutomationResult SuccessResult = DetectAutomationResult(
        TEXT("LogAutomationController"),
        TEXT("Test Completed. Result={Success} Name={Example}"));
    TestEqual("Automation success message should classify as success", SuccessResult, EAutomationResult::Success);

    const EAutomationResult FailureResult = DetectAutomationResult(
        TEXT("LogAutomationController"),
        TEXT("Error: Test Completed. Result={Fail} Name={Example}"));
    TestEqual("Automation fail message should classify as failure", FailureResult, EAutomationResult::Failure);

    const EAutomationResult SkippedResult = DetectAutomationResult(
        TEXT("LogAutomationController"),
        TEXT("Skipping benchmark: GPU Perception not available."));
    TestEqual("Automation skip message should classify as skipped", SkippedResult, EAutomationResult::Skipped);

    const EAutomationResult NonAutomationResult = DetectAutomationResult(
        TEXT("LogFlightAI"),
        TEXT("Test Completed. Result={Success}"));
    TestEqual("Non-automation category should not classify as automation result", NonAutomationResult, EAutomationResult::None);

    const FLinearColor SuccessColor = ResolveLogEntryColor(EVerbosity::Display, TEXT("LogAutomationController"), TEXT("Test Completed. Result={Success}"));
    const FLinearColor FailureColor = ResolveLogEntryColor(EVerbosity::Display, TEXT("LogAutomationController"), TEXT("Result={Fail}"));
    const FLinearColor SkippedColor = ResolveLogEntryColor(EVerbosity::Display, TEXT("LogAutomationController"), TEXT("Skipping unavailable test."));
    const FLinearColor DefaultColor = ResolveLogEntryColor(EVerbosity::Log, TEXT("LogFlightAI"), TEXT("Autopilot tick."));

    TestTrue("Success color should emphasize green channel", SuccessColor.G > SuccessColor.R);
    TestTrue("Failure color should emphasize red channel", FailureColor.R > FailureColor.G);
    TestTrue("Skipped color should remain warm (R >= G > B)", SkippedColor.R >= SkippedColor.G && SkippedColor.G > SkippedColor.B);
    TestEqual("Non-automation color should fall back to verbosity color", DefaultColor, VerbosityColor(EVerbosity::Log));

    return true;
}

#endif // WITH_AUTOMATION_TESTS
