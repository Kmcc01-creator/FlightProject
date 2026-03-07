// FlightLogTypes.h
// Log entry data structures using Row types and functional patterns
// Part of FlightProject's logging and debugging framework

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogVerbosity.h"
#include "Core/FlightRowTypes.h"

using namespace Flight::RowTypes;

namespace Flight::Log
{
    // ============================================================================
    // Verbosity Level Wrapper
    // ============================================================================

    enum class EVerbosity : uint8
    {
        Fatal       = 0,
        Error       = 1,
        Warning     = 2,
        Display     = 3,
        Log         = 4,
        Verbose     = 5,
        VeryVerbose = 6
    };

    constexpr EVerbosity FromUE(ELogVerbosity::Type V)
    {
        switch (V)
        {
            case ELogVerbosity::Fatal:       return EVerbosity::Fatal;
            case ELogVerbosity::Error:       return EVerbosity::Error;
            case ELogVerbosity::Warning:     return EVerbosity::Warning;
            case ELogVerbosity::Display:     return EVerbosity::Display;
            case ELogVerbosity::Log:         return EVerbosity::Log;
            case ELogVerbosity::Verbose:     return EVerbosity::Verbose;
            case ELogVerbosity::VeryVerbose: return EVerbosity::VeryVerbose;
            default:                         return EVerbosity::Log;
        }
    }

    constexpr const TCHAR* VerbosityName(EVerbosity V)
    {
        switch (V)
        {
            case EVerbosity::Fatal:       return TEXT("Fatal");
            case EVerbosity::Error:       return TEXT("Error");
            case EVerbosity::Warning:     return TEXT("Warning");
            case EVerbosity::Display:     return TEXT("Display");
            case EVerbosity::Log:         return TEXT("Log");
            case EVerbosity::Verbose:     return TEXT("Verbose");
            case EVerbosity::VeryVerbose: return TEXT("VeryVerbose");
            default:                      return TEXT("Unknown");
        }
    }

    constexpr FLinearColor VerbosityColor(EVerbosity V)
    {
        switch (V)
        {
            case EVerbosity::Fatal:       return FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);    // Red
            case EVerbosity::Error:       return FLinearColor(1.0f, 0.3f, 0.3f, 1.0f);    // Light red
            case EVerbosity::Warning:     return FLinearColor(1.0f, 0.8f, 0.0f, 1.0f);    // Yellow
            case EVerbosity::Display:     return FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);    // Light gray
            case EVerbosity::Log:         return FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);    // Gray
            case EVerbosity::Verbose:     return FLinearColor(0.4f, 0.6f, 0.8f, 1.0f);    // Light blue
            case EVerbosity::VeryVerbose: return FLinearColor(0.3f, 0.4f, 0.6f, 1.0f);    // Dark blue
            default:                      return FLinearColor::White;
        }
    }

    // ============================================================================
    // Log Entry - Core Data Structure
    // ============================================================================

    struct FLogEntry
    {
        uint64      Id;          // Monotonic entry ID
        double      Timestamp;   // Seconds since session start
        EVerbosity  Verbosity;
        FName       Category;
        FString     Message;
        FString     File;        // Source file (if available)
        int32       Line;        // Source line (if available)
        uint32      ThreadId;
        uint32      FrameNumber;

        FLogEntry() = default;

        FLogEntry(uint64 InId, double InTimestamp, EVerbosity InVerbosity,
                  FName InCategory, FString InMessage)
            : Id(InId)
            , Timestamp(InTimestamp)
            , Verbosity(InVerbosity)
            , Category(InCategory)
            , Message(SanitizeMessage(MoveTemp(InMessage)))
            , Line(0)
            , ThreadId(FPlatformTLS::GetCurrentThreadId())
            , FrameNumber(GFrameNumber)
        {}

        static FString SanitizeMessage(FString InMessage)
        {
            // Remove non-printable control characters that trigger Slate font warnings (e.g., U+15)
            // We keep \t (9), \n (10), \r (13)
            FString Sanitized;
            Sanitized.Reserve(InMessage.Len());
            for (TCHAR C : InMessage)
            {
                if (C < 32 && C != 9 && C != 10 && C != 13)
                {
                    continue;
                }
                Sanitized.AppendChar(C);
            }
            return Sanitized;
        }

        // Format timestamp as HH:MM:SS.mmm
        FString FormatTimestamp() const
        {
            const int32 TotalMs = static_cast<int32>(Timestamp * 1000.0);
            const int32 Ms = TotalMs % 1000;
            const int32 Seconds = (TotalMs / 1000) % 60;
            const int32 Minutes = (TotalMs / 60000) % 60;
            const int32 Hours = TotalMs / 3600000;

            return FString::Printf(TEXT("%02d:%02d:%02d.%03d"), Hours, Minutes, Seconds, Ms);
        }

        // Full formatted line for display
        FString Format() const
        {
            return FString::Printf(TEXT("[%s][%s][%s] %s"),
                *FormatTimestamp(),
                VerbosityName(Verbosity),
                *Category.ToString(),
                *Message);
        }

        bool operator==(const FLogEntry& Other) const
        {
            return Id == Other.Id
                && Timestamp == Other.Timestamp
                && Verbosity == Other.Verbosity
                && Category == Other.Category
                && Message == Other.Message
                && File == Other.File
                && Line == Other.Line
                && ThreadId == Other.ThreadId
                && FrameNumber == Other.FrameNumber;
        }
    };

    // ============================================================================
    // Row-Based Log Entry (for extensible metadata)
    // ============================================================================

    // Define common log field labels
    inline constexpr TStaticString Label_Id{"Id"};
    inline constexpr TStaticString Label_Timestamp{"Timestamp"};
    inline constexpr TStaticString Label_Verbosity{"Verbosity"};
    inline constexpr TStaticString Label_Category{"Category"};
    inline constexpr TStaticString Label_Message{"Message"};
    inline constexpr TStaticString Label_Tags{"Tags"};
    inline constexpr TStaticString Label_Context{"Context"};

    // Base log row with common fields
    using TBaseLogRow = TRow<
        TRowField<Label_Id, uint64>,
        TRowField<Label_Timestamp, double>,
        TRowField<Label_Verbosity, EVerbosity>,
        TRowField<Label_Category, FName>,
        TRowField<Label_Message, FString>
    >;

    // Extended log row with tags for filtering
    using TTaggedLogRow = TBaseLogRow::Add<Label_Tags, TArray<FName>>;

    // ============================================================================
    // Filter Predicate Types
    // ============================================================================

    // Filter by verbosity level
    struct FVerbosityFilter
    {
        EVerbosity MinLevel = EVerbosity::Log;

        bool Matches(const FLogEntry& Entry) const
        {
            return static_cast<uint8>(Entry.Verbosity) <= static_cast<uint8>(MinLevel);
        }
    };

    // Filter by category
    struct FCategoryFilter
    {
        TSet<FName> IncludeCategories;  // Empty = include all
        TSet<FName> ExcludeCategories;

        bool Matches(const FLogEntry& Entry) const
        {
            if (ExcludeCategories.Contains(Entry.Category))
            {
                return false;
            }
            if (IncludeCategories.Num() > 0)
            {
                return IncludeCategories.Contains(Entry.Category);
            }
            return true;
        }
    };

    // Filter by text search
    struct FTextFilter
    {
        FString SearchText;
        bool bCaseSensitive = false;

        bool Matches(const FLogEntry& Entry) const
        {
            if (SearchText.IsEmpty())
            {
                return true;
            }

            if (bCaseSensitive)
            {
                return Entry.Message.Contains(SearchText);
            }
            else
            {
                return Entry.Message.Contains(SearchText, ESearchCase::IgnoreCase);
            }
        }
    };

    // Filter by frame range
    struct FFrameFilter
    {
        TOptional<uint32> MinFrame;
        TOptional<uint32> MaxFrame;

        bool Matches(const FLogEntry& Entry) const
        {
            if (MinFrame.IsSet() && Entry.FrameNumber < MinFrame.GetValue())
            {
                return false;
            }
            if (MaxFrame.IsSet() && Entry.FrameNumber > MaxFrame.GetValue())
            {
                return false;
            }
            return true;
        }
    };

    // ============================================================================
    // Composite Filter - Combines multiple predicates
    // ============================================================================

    struct FLogFilter
    {
        FVerbosityFilter Verbosity;
        FCategoryFilter  Category;
        FTextFilter      Text;
        FFrameFilter     Frame;

        bool Matches(const FLogEntry& Entry) const
        {
            return Verbosity.Matches(Entry)
                && Category.Matches(Entry)
                && Text.Matches(Entry)
                && Frame.Matches(Entry);
        }

        // Functional-style filter composition
        template<typename Pred>
        auto And(Pred&& P) const
        {
            auto Self = *this;
            return [Self, P = Forward<Pred>(P)](const FLogEntry& Entry) {
                return Self.Matches(Entry) && P(Entry);
            };
        }
    };

    // ============================================================================
    // Log Statistics
    // ============================================================================

    struct FLogStats
    {
        uint64 TotalEntries = 0;
        uint64 CountByVerbosity[7] = {0};  // Indexed by EVerbosity
        TMap<FName, uint64> CountByCategory;
        uint32 FirstFrame = 0;
        uint32 LastFrame = 0;
        double FirstTimestamp = 0.0;
        double LastTimestamp = 0.0;

        void AddEntry(const FLogEntry& Entry)
        {
            TotalEntries++;
            CountByVerbosity[static_cast<uint8>(Entry.Verbosity)]++;
            CountByCategory.FindOrAdd(Entry.Category)++;

            if (TotalEntries == 1)
            {
                FirstFrame = Entry.FrameNumber;
                FirstTimestamp = Entry.Timestamp;
            }
            LastFrame = Entry.FrameNumber;
            LastTimestamp = Entry.Timestamp;
        }

        uint64 ErrorCount() const
        {
            return CountByVerbosity[static_cast<uint8>(EVerbosity::Error)]
                 + CountByVerbosity[static_cast<uint8>(EVerbosity::Fatal)];
        }

        uint64 WarningCount() const
        {
            return CountByVerbosity[static_cast<uint8>(EVerbosity::Warning)];
        }
    };

    // ============================================================================
    // Ring Buffer for Log Storage
    // ============================================================================

    template<int32 Capacity = 10000>
    class TLogRingBuffer
    {
    public:
        void Add(FLogEntry Entry)
        {
            FScopeLock Lock(&CriticalSection);

            Entry.Id = NextId++;
            const int32 Index = WriteIndex % Capacity;

            if (Count < Capacity)
            {
                Entries[Index] = MoveTemp(Entry);
                Stats.AddEntry(Entries[Index]);
                Count++;
            }
            else
            {
                // Overwriting old entry - would need to recalculate stats
                Entries[Index] = MoveTemp(Entry);
            }

            WriteIndex++;
        }

        // Get filtered view as array
        TArray<FLogEntry> GetFiltered(const FLogFilter& Filter) const
        {
            FScopeLock Lock(&CriticalSection);

            TArray<FLogEntry> Result;
            Result.Reserve(Count);

            const int32 Start = (Count < Capacity) ? 0 : (WriteIndex % Capacity);
            for (int32 i = 0; i < Count; i++)
            {
                const int32 Index = (Start + i) % Capacity;
                if (Filter.Matches(Entries[Index]))
                {
                    Result.Add(Entries[Index]);
                }
            }

            return Result;
        }

        // Iterate over all entries
        template<typename Func>
        void ForEach(Func&& F) const
        {
            FScopeLock Lock(&CriticalSection);

            const int32 Start = (Count < Capacity) ? 0 : (WriteIndex % Capacity);
            for (int32 i = 0; i < Count; i++)
            {
                const int32 Index = (Start + i) % Capacity;
                F(Entries[Index]);
            }
        }

        // Functional map/filter/reduce
        template<typename Pred>
        TArray<FLogEntry> Filter(Pred&& P) const
        {
            TArray<FLogEntry> Result;
            ForEach([&](const FLogEntry& E) {
                if (P(E)) Result.Add(E);
            });
            return Result;
        }

        template<typename Func, typename T>
        T Reduce(Func&& F, T Initial) const
        {
            T Acc = MoveTemp(Initial);
            ForEach([&](const FLogEntry& E) {
                Acc = F(MoveTemp(Acc), E);
            });
            return Acc;
        }

        int32 Num() const { return Count; }
        const FLogStats& GetStats() const { return Stats; }

        void Clear()
        {
            FScopeLock Lock(&CriticalSection);
            Count = 0;
            WriteIndex = 0;
            Stats = FLogStats{};
        }

    private:
        mutable FCriticalSection CriticalSection;
        TStaticArray<FLogEntry, Capacity> Entries;
        int32 WriteIndex = 0;
        int32 Count = 0;
        uint64 NextId = 1;
        FLogStats Stats;
    };

    // Default buffer type
    using FLogBuffer = TLogRingBuffer<10000>;

} // namespace Flight::Log
