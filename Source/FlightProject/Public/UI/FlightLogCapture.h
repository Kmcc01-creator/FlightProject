// FlightLogCapture.h
// Custom FOutputDevice for intercepting and capturing UE logs
// Integrates with FlightProject's reactive UI system

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "FlightLogTypes.h"
#include "FlightProject/Public/UI/FlightReactiveUI.h"

namespace Flight::Log
{
    // ============================================================================
    // Log Capture Device - Intercepts all UE_LOG output
    // ============================================================================

    class FLogCaptureDevice : public FOutputDevice
    {
    public:
        using FOnLogReceived = TMulticastDelegate<void(const FLogEntry&)>;

        FLogCaptureDevice()
            : StartTime(FPlatformTime::Seconds())
        {}

        virtual ~FLogCaptureDevice() override
        {
            Detach();
        }

        // Attach to global log output
        void Attach()
        {
            if (!bAttached)
            {
                GLog->AddOutputDevice(this);
                bAttached = true;
            }
        }

        // Detach from global log output
        void Detach()
        {
            if (bAttached)
            {
                GLog->RemoveOutputDevice(this);
                bAttached = false;
            }
        }

        bool IsAttached() const { return bAttached; }

        // FOutputDevice interface
        virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            const double Timestamp = FPlatformTime::Seconds() - StartTime;

            FLogEntry Entry(
                0,  // ID assigned by buffer
                Timestamp,
                FromUE(Verbosity),
                Category,
                FString(V)
            );
            Entry.FrameNumber = GFrameNumber;
            Entry.ThreadId = FPlatformTLS::GetCurrentThreadId();

            // Add to buffer
            Buffer.Add(Entry);

            // Notify subscribers
            OnLogReceived.Broadcast(Buffer.GetFiltered(FLogFilter{})[Buffer.Num() - 1]);
        }

        virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category,
                               const double Time) override
        {
            Serialize(V, Verbosity, Category);
        }

        virtual bool CanBeUsedOnAnyThread() const override
        {
            return true;
        }

        virtual bool CanBeUsedOnMultipleThreads() const override
        {
            return true;
        }

        // Access the log buffer
        FLogBuffer& GetBuffer() { return Buffer; }
        const FLogBuffer& GetBuffer() const { return Buffer; }

        // Event subscription
        FOnLogReceived& OnLog() { return OnLogReceived; }

        // Get stats
        const FLogStats& GetStats() const { return Buffer.GetStats(); }

        // Clear captured logs
        void Clear()
        {
            Buffer.Clear();
        }

    private:
        FLogBuffer Buffer;
        FOnLogReceived OnLogReceived;
        double StartTime;
        bool bAttached = false;
    };

    // ============================================================================
    // Global Log Capture Singleton
    // ============================================================================

    class FGlobalLogCapture
    {
    public:
        static FLogCaptureDevice& Get()
        {
            static FLogCaptureDevice Instance;
            return Instance;
        }

        static void Initialize()
        {
            Get().Attach();
        }

        static void Shutdown()
        {
            Get().Detach();
        }

    private:
        FGlobalLogCapture() = delete;
    };

    // ============================================================================
    // Scoped Log Capture - Capture logs for a specific scope
    // ============================================================================

    class FScopedLogCapture
    {
    public:
        explicit FScopedLogCapture(bool bCaptureGlobal = false)
            : bUseGlobal(bCaptureGlobal)
        {
            if (bUseGlobal)
            {
                FGlobalLogCapture::Get().Clear();
            }
            else
            {
                LocalDevice.Attach();
            }
        }

        ~FScopedLogCapture()
        {
            if (!bUseGlobal)
            {
                LocalDevice.Detach();
            }
        }

        // Non-copyable
        FScopedLogCapture(const FScopedLogCapture&) = delete;
        FScopedLogCapture& operator=(const FScopedLogCapture&) = delete;

        // Get captured logs
        TArray<FLogEntry> GetLogs(const FLogFilter& Filter = FLogFilter{}) const
        {
            return GetDevice().GetBuffer().GetFiltered(Filter);
        }

        // Get logs with specific verbosity
        TArray<FLogEntry> GetErrors() const
        {
            FLogFilter Filter;
            Filter.Verbosity.MinLevel = EVerbosity::Error;
            return GetLogs(Filter);
        }

        TArray<FLogEntry> GetWarnings() const
        {
            FLogFilter Filter;
            Filter.Verbosity.MinLevel = EVerbosity::Warning;
            return GetDevice().GetBuffer().Filter([](const FLogEntry& E) {
                return E.Verbosity == EVerbosity::Warning;
            });
        }

        // Check if any errors were captured
        bool HasErrors() const
        {
            return GetDevice().GetStats().ErrorCount() > 0;
        }

        bool HasWarnings() const
        {
            return GetDevice().GetStats().WarningCount() > 0;
        }

        const FLogStats& GetStats() const
        {
            return GetDevice().GetStats();
        }

    private:
        FLogCaptureDevice& GetDevice() const
        {
            return bUseGlobal ? FGlobalLogCapture::Get() : const_cast<FLogCaptureDevice&>(LocalDevice);
        }

        FLogCaptureDevice LocalDevice;
        bool bUseGlobal;
    };

    // ============================================================================
    // Reactive Log Stream - For UI binding
    // ============================================================================

    class FReactiveLogStream
    {
    public:
        using FFilteredLogs = TArray<FLogEntry>;

        explicit FReactiveLogStream(FLogCaptureDevice& InDevice)
            : Device(InDevice)
        {
            // Subscribe to new log events
            DelegateHandle = Device.OnLog().AddRaw(this, &FReactiveLogStream::OnLogReceived);
        }

        ~FReactiveLogStream()
        {
            Device.OnLog().Remove(DelegateHandle);
        }

        // Reactive values for UI binding
        ReactiveUI::TReactiveValue<FFilteredLogs>& FilteredLogs() { return FilteredLogsValue; }
        ReactiveUI::TReactiveValue<int32>& TotalCount() { return TotalCountValue; }
        ReactiveUI::TReactiveValue<int32>& ErrorCount() { return ErrorCountValue; }
        ReactiveUI::TReactiveValue<int32>& WarningCount() { return WarningCountValue; }

        // Update filter and refresh
        void SetFilter(const FLogFilter& NewFilter)
        {
            CurrentFilter = NewFilter;
            RefreshFilteredLogs();
        }

        const FLogFilter& GetFilter() const { return CurrentFilter; }

        // Modify filter components
        void SetVerbosityLevel(EVerbosity Level)
        {
            CurrentFilter.Verbosity.MinLevel = Level;
            RefreshFilteredLogs();
        }

        void SetSearchText(const FString& Text)
        {
            CurrentFilter.Text.SearchText = Text;
            RefreshFilteredLogs();
        }

        void ToggleCategory(FName Category, bool bInclude)
        {
            if (bInclude)
            {
                CurrentFilter.Category.ExcludeCategories.Remove(Category);
                CurrentFilter.Category.IncludeCategories.Add(Category);
            }
            else
            {
                CurrentFilter.Category.IncludeCategories.Remove(Category);
                CurrentFilter.Category.ExcludeCategories.Add(Category);
            }
            RefreshFilteredLogs();
        }

        void ClearCategoryFilters()
        {
            CurrentFilter.Category.IncludeCategories.Empty();
            CurrentFilter.Category.ExcludeCategories.Empty();
            RefreshFilteredLogs();
        }

        // Get all unique categories
        TArray<FName> GetAllCategories() const
        {
            TArray<FName> Categories;
            Device.GetStats().CountByCategory.GetKeys(Categories);
            return Categories;
        }

    private:
        void OnLogReceived(const FLogEntry& Entry)
        {
            // Update counts
            TotalCountValue = Device.GetStats().TotalEntries;
            ErrorCountValue = Device.GetStats().ErrorCount();
            WarningCountValue = Device.GetStats().WarningCount();

            // Add to filtered if matches
            if (CurrentFilter.Matches(Entry))
            {
                auto Logs = FilteredLogsValue.Get();
                Logs.Add(Entry);

                // Limit displayed entries to prevent memory issues
                constexpr int32 MaxDisplayedLogs = 1000;
                if (Logs.Num() > MaxDisplayedLogs)
                {
                    Logs.RemoveAt(0, Logs.Num() - MaxDisplayedLogs);
                }

                FilteredLogsValue = MoveTemp(Logs);
            }
        }

        void RefreshFilteredLogs()
        {
            auto Logs = Device.GetBuffer().GetFiltered(CurrentFilter);

            // Limit displayed entries
            constexpr int32 MaxDisplayedLogs = 1000;
            if (Logs.Num() > MaxDisplayedLogs)
            {
                const int32 StartIndex = Logs.Num() - MaxDisplayedLogs;
                Logs.RemoveAt(0, StartIndex);
            }

            FilteredLogsValue = MoveTemp(Logs);
        }

        FLogCaptureDevice& Device;
        FLogFilter CurrentFilter;
        FDelegateHandle DelegateHandle;

        ReactiveUI::TReactiveValue<FFilteredLogs> FilteredLogsValue;
        ReactiveUI::TReactiveValue<int32> TotalCountValue{0};
        ReactiveUI::TReactiveValue<int32> ErrorCountValue{0};
        ReactiveUI::TReactiveValue<int32> WarningCountValue{0};
    };

    // ============================================================================
    // Log Macros with Extended Context
    // ============================================================================

    // Helper to capture source location
    #define FLIGHT_LOG_IMPL(CategoryName, Verbosity, Format, ...) \
        do { \
            UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
        } while(0)

    // Convenience macros that match UE_LOG style
    #define FLIGHT_LOG(CategoryName, Format, ...) \
        FLIGHT_LOG_IMPL(CategoryName, Log, Format, ##__VA_ARGS__)

    #define FLIGHT_LOG_VERBOSE(CategoryName, Format, ...) \
        FLIGHT_LOG_IMPL(CategoryName, Verbose, Format, ##__VA_ARGS__)

    #define FLIGHT_LOG_WARNING(CategoryName, Format, ...) \
        FLIGHT_LOG_IMPL(CategoryName, Warning, Format, ##__VA_ARGS__)

    #define FLIGHT_LOG_ERROR(CategoryName, Format, ...) \
        FLIGHT_LOG_IMPL(CategoryName, Error, Format, ##__VA_ARGS__)

} // namespace Flight::Log
