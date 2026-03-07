// FlightLogExamples.h
// Examples demonstrating the FlightProject logging and debugging framework

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlightLogCapture.h"
#include "FlightLogViewer.h"
#include "FlightLogCategories.h"
#include "FlightLogExamples.generated.h"

namespace Flight::Log::Examples
{
    // ============================================================================
    // Example 1: Basic Log Capture and Filtering
    // ============================================================================

    inline void BasicLogFilteringExample()
    {
        // Initialize global log capture (typically done at game startup)
        FGlobalLogCapture::Initialize();

        // Generate some test logs
        UE_LOG(LogFlight, Log, TEXT("Flight system initialized"));
        UE_LOG(LogFlightMovement, Warning, TEXT("Velocity exceeded safe limits"));
        UE_LOG(LogFlightMass, Error, TEXT("Failed to spawn entity batch"));
        UE_LOG(LogFlightNav, Verbose, TEXT("Recalculating path segment 42"));

        // Create a filter
        FLogFilter Filter;
        Filter.Verbosity.MinLevel = EVerbosity::Warning;  // Only warnings and above
        Filter.Text.SearchText = TEXT("spawn");           // Must contain "spawn"

        // Get filtered results
        auto Results = FGlobalLogCapture::Get().GetBuffer().GetFiltered(Filter);

        // Results will contain only the error about "Failed to spawn entity batch"
        for (const auto& Entry : Results)
        {
            UE_LOG(LogFlightDebug, Display, TEXT("Filtered: %s"), *Entry.Format());
        }
    }

    // ============================================================================
    // Example 2: Scoped Log Capture for Testing
    // ============================================================================

    inline void ScopedLogCaptureExample()
    {
        // Capture logs within a specific scope
        {
            FScopedLogCapture Capture;

            // Do some work that generates logs
            UE_LOG(LogFlightData, Log, TEXT("Loading CSV file..."));
            UE_LOG(LogFlightData, Warning, TEXT("Missing optional column 'Altitude'"));
            UE_LOG(LogFlightData, Log, TEXT("Loaded 100 entries"));

            // Check results within scope
            if (Capture.HasWarnings())
            {
                UE_LOG(LogFlightDebug, Display, TEXT("Data loading had %lld warnings"),
                    Capture.GetStats().WarningCount());
            }

            auto Warnings = Capture.GetWarnings();
            for (const auto& W : Warnings)
            {
                UE_LOG(LogFlightDebug, Display, TEXT("  - %s"), *W.Message);
            }
        }
        // Capture automatically detaches when scope ends
    }

    // ============================================================================
    // Example 3: Reactive Log Stream for UI
    // ============================================================================

    inline void ReactiveLogStreamExample()
    {
        // Create reactive stream connected to global capture
        auto Stream = MakeShared<FReactiveLogStream>(FGlobalLogCapture::Get());

        // Subscribe to count changes
        Stream->ErrorCount().SubscribeLambda([](const int32& OldCount, const int32& NewCount) {
            if (NewCount > 0)
            {
                // Could trigger UI notification, sound, etc.
                UE_LOG(LogFlightUI, Warning, TEXT("Error count increased to %d"), NewCount);
            }
        });

        // Configure filtering
        Stream->SetVerbosityLevel(EVerbosity::Warning);
        Stream->SetSearchText(TEXT("flight"));

        // Toggle specific categories
        Stream->ToggleCategory(TEXT("LogFlightMass"), true);   // Include
        Stream->ToggleCategory(TEXT("LogFlightInput"), false); // Exclude

        // Access reactive values for binding
        const auto& Logs = Stream->FilteredLogs().Get();
        int32 Total = Stream->TotalCount().Get();
        int32 Errors = Stream->ErrorCount().Get();
    }

    // ============================================================================
    // Example 4: Category-Based Filtering
    // ============================================================================

    inline void CategoryFilteringExample()
    {
        FLogFilter Filter;

        // Include only ECS-related categories
        for (const FName& Cat : FCategoryGroups::ECS())
        {
            Filter.Category.IncludeCategories.Add(Cat);
        }

        // Or use preset groups
        FLogFilter NavFilter;
        for (const FName& Cat : FCategoryGroups::Navigation())
        {
            NavFilter.Category.IncludeCategories.Add(Cat);
        }

        // Exclude verbose engine logs
        FLogFilter QuietFilter;
        QuietFilter.Category.ExcludeCategories.Add(TEXT("LogTemp"));
        QuietFilter.Category.ExcludeCategories.Add(TEXT("LogStreaming"));
        QuietFilter.Category.ExcludeCategories.Add(TEXT("LogLoad"));

        // Apply to capture
        auto Results = FGlobalLogCapture::Get().GetBuffer().GetFiltered(Filter);
    }

    // ============================================================================
    // Example 5: Functional-Style Log Processing
    // ============================================================================

    inline void FunctionalLogProcessingExample()
    {
        auto& Buffer = FGlobalLogCapture::Get().GetBuffer();

        // Filter using lambda predicate
        auto RecentErrors = Buffer.Filter([](const FLogEntry& E) {
            return E.Verbosity <= EVerbosity::Error
                && E.FrameNumber > GFrameNumber - 100;  // Last 100 frames
        });

        // Reduce to compute statistics
        struct FCustomStats
        {
            int32 ErrorCount = 0;
            int32 WarningCount = 0;
            double TotalTime = 0.0;
        };

        auto Stats = Buffer.Reduce(
            [](FCustomStats Acc, const FLogEntry& E) {
                if (E.Verbosity == EVerbosity::Error) Acc.ErrorCount++;
                if (E.Verbosity == EVerbosity::Warning) Acc.WarningCount++;
                Acc.TotalTime = FMath::Max(Acc.TotalTime, E.Timestamp);
                return Acc;
            },
            FCustomStats{});

        // Chain filters
        FLogFilter BaseFilter;
        BaseFilter.Verbosity.MinLevel = EVerbosity::Warning;

        auto CustomFilter = BaseFilter.And([](const FLogEntry& E) {
            return E.Category.ToString().StartsWith(TEXT("LogFlight"));
        });

        Buffer.ForEach([&CustomFilter](const FLogEntry& E) {
            if (CustomFilter(E))
            {
                // Process matching entry
            }
        });
    }

    // ============================================================================
    // Example 6: Performance Logging
    // ============================================================================

    inline void PerformanceLoggingExample()
    {
        // Scoped timing that logs on scope exit
        {
            FLIGHT_SCOPED_PERF("EntityBatchSpawn");

            // Simulate work
            FPlatformProcess::Sleep(0.005f);
        }
        // Logs: "EntityBatchSpawn: 5.xxx ms"

        // Conditional logging only if threshold exceeded
        {
            FLIGHT_SCOPED_PERF_THRESHOLD("PathCalculation", 2.0);  // 2ms threshold

            // Quick work - no log
            FPlatformProcess::Sleep(0.001f);
        }

        {
            FLIGHT_SCOPED_PERF_THRESHOLD("SlowOperation", 2.0);

            // Slow work - will log warning
            FPlatformProcess::Sleep(0.005f);
        }
        // Logs warning: "SlowOperation exceeded threshold: 5.xxx ms (limit: 2.000 ms)"
    }

    // ============================================================================
    // Example 7: Structured Logging Helpers
    // ============================================================================

    inline void StructuredLoggingExample()
    {
        FVector Position(1234.5f, -678.9f, 500.0f);
        FRotator Rotation(15.0f, 90.0f, 0.0f);
        FTransform Transform(Rotation, Position, FVector::OneVector);

        // Format helpers for common types
        UE_LOG(LogFlightMovement, Log, TEXT("Entity at %s facing %s"),
            *FormatVector(Position),
            *FormatRotator(Rotation));

        UE_LOG(LogFlightMovement, Log, TEXT("Transform: %s"),
            *FormatTransform(Transform));

        // Memory and time formatting
        uint64 MemUsage = 1024 * 1024 * 128;  // 128 MB
        double Duration = 0.0234;  // 23.4 ms

        UE_LOG(LogFlightPerf, Log, TEXT("Buffer allocation: %s in %s"),
            *FormatBytes(MemUsage),
            *FormatDuration(Duration));
    }

} // namespace Flight::Log::Examples

// ============================================================================
// Example Actor Component - Demonstrates Runtime Integration
// ============================================================================

UCLASS(ClassGroup=(Flight), meta=(BlueprintSpawnableComponent))
class FLIGHTPROJECT_API UFlightLogDemoComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override
    {
        Super::BeginPlay();

        // Initialize capture if not already done
        Flight::Log::FGlobalLogCapture::Initialize();

        // Create reactive stream
        LogStream = MakeShared<Flight::Log::FReactiveLogStream>(
            Flight::Log::FGlobalLogCapture::Get());

        // Log component initialization
        UE_LOG(LogFlight, Log, TEXT("LogDemoComponent initialized on %s"),
            *GetOwner()->GetName());
    }

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override
    {
        LogStream.Reset();
        Super::EndPlay(EndPlayReason);
    }

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override
    {
        Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

        // Periodic verbose logging
        if (bLogVerbose)
        {
            UE_LOG(LogFlightDebug, Verbose, TEXT("Tick: %.3f ms delta"),
                DeltaTime * 1000.0f);
        }

        // Check for new errors
        int32 CurrentErrors = LogStream->ErrorCount().Get();
        if (CurrentErrors > LastErrorCount)
        {
            OnNewErrors(CurrentErrors - LastErrorCount);
            LastErrorCount = CurrentErrors;
        }
    }

    // Get current log stats
    UFUNCTION(BlueprintCallable, Category = "Flight|Logging")
    FString GetLogSummary() const
    {
        const auto& Stats = Flight::Log::FGlobalLogCapture::Get().GetStats();
        return FString::Printf(
            TEXT("Total: %llu | Errors: %llu | Warnings: %llu"),
            Stats.TotalEntries,
            Stats.ErrorCount(),
            Stats.WarningCount());
    }

    // Clear all captured logs
    UFUNCTION(BlueprintCallable, Category = "Flight|Logging")
    void ClearLogs()
    {
        Flight::Log::FGlobalLogCapture::Get().Clear();
        LastErrorCount = 0;
        UE_LOG(LogFlightUI, Log, TEXT("Logs cleared"));
    }

    // Enable/disable verbose tick logging
    UPROPERTY(EditAnywhere, Category = "Flight|Logging")
    bool bLogVerbose = false;

protected:
    virtual void OnNewErrors(int32 NewErrorCount)
    {
        UE_LOG(LogFlightUI, Warning, TEXT("%d new error(s) detected"), NewErrorCount);

        // Could trigger UI notification, sound, etc.
    }

private:
    TSharedPtr<Flight::Log::FReactiveLogStream> LogStream;
    int32 LastErrorCount = 0;
};

// ============================================================================
// Creating a Log Viewer Window (Editor or Runtime)
// ============================================================================

namespace Flight::Log::Examples
{
    inline TSharedRef<SWidget> CreateLogViewerWidget()
    {
        // Full log viewer with all features
        return SNew(SLogViewer);
    }

    inline TSharedRef<SWidget> CreateCompactLogBar(FSimpleDelegate OnExpand)
    {
        // Compact status bar for embedding in other UI
        return SNew(SLogBar)
            .OnExpandClicked(OnExpand);
    }

    inline TSharedRef<SWidget> CreateFilteredLogViewer(const TArray<FName>& Categories)
    {
        // Create viewer with pre-configured category filter
        auto Stream = MakeShared<FReactiveLogStream>(FGlobalLogCapture::Get());

        for (const FName& Cat : Categories)
        {
            Stream->ToggleCategory(Cat, true);
        }

        return SNew(SLogViewer)
            .LogStream(Stream);
    }

    // Example: Create ECS-focused log viewer
    inline TSharedRef<SWidget> CreateMassLogViewer()
    {
        return CreateFilteredLogViewer(FCategoryGroups::ECS());
    }

    // Example: Create navigation log viewer
    inline TSharedRef<SWidget> CreateNavLogViewer()
    {
        return CreateFilteredLogViewer(FCategoryGroups::Navigation());
    }
}
