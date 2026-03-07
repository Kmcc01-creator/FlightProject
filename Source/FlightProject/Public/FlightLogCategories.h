// FlightLogCategories.h
// Custom log categories for FlightProject
// Provides structured logging for all subsystems

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "HAL/PlatformTime.h"

// ============================================================================
// Log Category Declarations
// ============================================================================

// Core flight simulation
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlight, Log, All);

// Movement and physics
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightMovement, Log, All);
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightPhysics, Log, All);

// Navigation and pathfinding
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightNav, Log, All);
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightPath, Log, All);

// Mass Entity ECS
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightMass, Log, All);
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightMassSpawn, Log, All);
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightMassProcessor, Verbose, All);

// Data loading and CSV
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightData, Log, All);

// Swarm systems
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightSwarm, Log, All);

// Autopilot and AI
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightAI, Log, All);
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightAutopilot, Log, All);

// Input handling
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightInput, Verbose, All);

// Networking
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightNet, Log, All);

// GPU and compute
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightGPU, Log, All);
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightCompute, Log, All);

// io_uring integration
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightIoUring, Log, All);

// UI and Slate
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightUI, Log, All);

// Platform (Linux/Wayland)
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightPlatform, Log, All);

// Subsystems
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightSubsystem, Log, All);

// Debug and development
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightDebug, Verbose, All);

// Performance profiling
FLIGHTPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogFlightPerf, Log, All);

// ============================================================================
// Category Groups for Filtering
// ============================================================================

namespace Flight::Log
{
    // Category group definitions for filter presets
    struct FCategoryGroups
    {
        static TArray<FName> Core()
        {
            return {
                TEXT("LogFlight"),
                TEXT("LogFlightMovement"),
                TEXT("LogFlightPhysics")
            };
        }

        static TArray<FName> Navigation()
        {
            return {
                TEXT("LogFlightNav"),
                TEXT("LogFlightPath")
            };
        }

        static TArray<FName> ECS()
        {
            return {
                TEXT("LogFlightMass"),
                TEXT("LogFlightMassSpawn"),
                TEXT("LogFlightMassProcessor")
            };
        }

        static TArray<FName> AI()
        {
            return {
                TEXT("LogFlightAI"),
                TEXT("LogFlightAutopilot"),
                TEXT("LogFlightSwarm")
            };
        }

        static TArray<FName> Platform()
        {
            return {
                TEXT("LogFlightPlatform"),
                TEXT("LogFlightIoUring"),
                TEXT("LogFlightGPU"),
                TEXT("LogFlightCompute")
            };
        }

        static TArray<FName> All()
        {
            TArray<FName> Result;
            Result.Append(Core());
            Result.Append(Navigation());
            Result.Append(ECS());
            Result.Append(AI());
            Result.Append(Platform());
            Result.Add(TEXT("LogFlightData"));
            Result.Add(TEXT("LogFlightInput"));
            Result.Add(TEXT("LogFlightNet"));
            Result.Add(TEXT("LogFlightUI"));
            Result.Add(TEXT("LogFlightSubsystem"));
            Result.Add(TEXT("LogFlightDebug"));
            Result.Add(TEXT("LogFlightPerf"));
            return Result;
        }
    };
}

// ============================================================================
// Convenience Logging Macros
// ============================================================================

// Terse logging macros for common categories
#define FLIGHT_LOG_CORE(Verbosity, Format, ...) \
    UE_LOG(LogFlight, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_MOVE(Verbosity, Format, ...) \
    UE_LOG(LogFlightMovement, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_NAV(Verbosity, Format, ...) \
    UE_LOG(LogFlightNav, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_MASS(Verbosity, Format, ...) \
    UE_LOG(LogFlightMass, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_AI(Verbosity, Format, ...) \
    UE_LOG(LogFlightAI, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_DATA(Verbosity, Format, ...) \
    UE_LOG(LogFlightData, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_UI(Verbosity, Format, ...) \
    UE_LOG(LogFlightUI, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_PLATFORM(Verbosity, Format, ...) \
    UE_LOG(LogFlightPlatform, Verbosity, Format, ##__VA_ARGS__)

#define FLIGHT_LOG_PERF(Verbosity, Format, ...) \
    UE_LOG(LogFlightPerf, Verbosity, Format, ##__VA_ARGS__)

// ============================================================================
// Scoped Performance Logging
// ============================================================================

namespace Flight::Log
{
    // RAII scope timer that logs elapsed time
    class FScopedPerfLog
    {
    public:
        FScopedPerfLog(const TCHAR* InName, const FName& InCategory = TEXT("LogFlightPerf"))
            : Name(InName)
            , Category(InCategory)
            , StartTime(FPlatformTime::Seconds())
        {}

        ~FScopedPerfLog()
        {
            const double Elapsed = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            UE_LOG(LogFlightPerf, Log, TEXT("%s: %.3fms"), Name, Elapsed);
        }

    private:
        const TCHAR* Name;
        FName Category;
        double StartTime;
    };

    // Conditional performance log (only logs if above threshold)
    class FConditionalPerfLog
    {
    public:
        FConditionalPerfLog(const TCHAR* InName, double ThresholdMs = 1.0)
            : Name(InName)
            , Threshold(ThresholdMs)
            , StartTime(FPlatformTime::Seconds())
        {}

        ~FConditionalPerfLog()
        {
            const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            if (ElapsedMs >= Threshold)
            {
                UE_LOG(LogFlightPerf, Warning, TEXT("%s exceeded threshold: %.3fms (limit: %.3fms)"),
                    Name, ElapsedMs, Threshold);
            }
        }

    private:
        const TCHAR* Name;
        double Threshold;
        double StartTime;
    };
}

// Convenience macros for scoped performance logging
#define FLIGHT_SCOPED_PERF(Name) \
    Flight::Log::FScopedPerfLog PREPROCESSOR_JOIN(ScopedPerfLog_, __LINE__)(TEXT(Name))

#define FLIGHT_SCOPED_PERF_THRESHOLD(Name, ThresholdMs) \
    Flight::Log::FConditionalPerfLog PREPROCESSOR_JOIN(CondPerfLog_, __LINE__)(TEXT(Name), ThresholdMs)

// ============================================================================
// Structured Logging Helpers
// ============================================================================

namespace Flight::Log
{
    // Format a vector for logging
    inline FString FormatVector(const FVector& V)
    {
        return FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), V.X, V.Y, V.Z);
    }

    // Format a rotator for logging
    inline FString FormatRotator(const FRotator& R)
    {
        return FString::Printf(TEXT("(P=%.2f, Y=%.2f, R=%.2f)"), R.Pitch, R.Yaw, R.Roll);
    }

    // Format a transform for logging
    inline FString FormatTransform(const FTransform& T)
    {
        return FString::Printf(TEXT("Loc=%s Rot=%s Scale=%s"),
            *FormatVector(T.GetLocation()),
            *FormatRotator(T.GetRotation().Rotator()),
            *FormatVector(T.GetScale3D()));
    }

    // Format memory size
    inline FString FormatBytes(uint64 Bytes)
    {
        if (Bytes >= 1024 * 1024 * 1024)
            return FString::Printf(TEXT("%.2f GB"), Bytes / (1024.0 * 1024.0 * 1024.0));
        if (Bytes >= 1024 * 1024)
            return FString::Printf(TEXT("%.2f MB"), Bytes / (1024.0 * 1024.0));
        if (Bytes >= 1024)
            return FString::Printf(TEXT("%.2f KB"), Bytes / 1024.0);
        return FString::Printf(TEXT("%llu bytes"), Bytes);
    }

    // Format time duration
    inline FString FormatDuration(double Seconds)
    {
        if (Seconds >= 60.0)
            return FString::Printf(TEXT("%.2f min"), Seconds / 60.0);
        if (Seconds >= 1.0)
            return FString::Printf(TEXT("%.3f s"), Seconds);
        if (Seconds >= 0.001)
            return FString::Printf(TEXT("%.3f ms"), Seconds * 1000.0);
        return FString::Printf(TEXT("%.3f us"), Seconds * 1000000.0);
    }
}
