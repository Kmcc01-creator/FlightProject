// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightLogging - Structured, Functional, and Reflective Logging System
//
// Designed to decouple FlightProject logic from Unreal's logging macros
// while providing deep integration with our reflection and functional systems.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Core/FlightReflection.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightRowTypes.h"
#include "UI/FlightLogTypes.h"

namespace Flight::Logging
{

using namespace Flight::Reflection;
using namespace Flight::Functional;
using namespace Flight::RowTypes;
using namespace Flight::Log; // For EVerbosity, FLogEntry

// ============================================================================
// Log Level - Type-safe level matching EVerbosity
// ============================================================================

using ELogLevel = EVerbosity;

// ============================================================================
// Structured Log Data - Extensible via Row Types
// ============================================================================

// Common labels for structured data
inline constexpr TStaticString Label_Actor{"Actor"};
inline constexpr TStaticString Label_Component{"Component"};
inline constexpr TStaticString Label_Location{"Location"};
inline constexpr TStaticString Label_Value{"Value"};
inline constexpr TStaticString Label_Error{"Error"};

/**
 * FLogContext - A type-erased container for structured log data.
 * Can be built using row types or reflection.
 */
struct FLogContext
{
    FString StructuredMessage;
    TMap<FString, FString> KeyValues;

    bool IsEmpty() const { return StructuredMessage.IsEmpty() && KeyValues.Num() == 0; }

    FString Format() const
    {
        if (IsEmpty()) return TEXT("");
        
        FString Result = TEXT(" | Context: {");
        bool bFirst = true;
        for (auto& KVP : KeyValues)
        {
            if (!bFirst) Result += TEXT(", ");
            Result += FString::Printf(TEXT("%s: %s"), *KVP.Key, *KVP.Value);
            bFirst = false;
        }
        Result += TEXT("}");
        return Result;
    }
};

// ============================================================================
// ILogSink - Abstract destination for logs
// ============================================================================

class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Receive(const FLogEntry& Entry, const FLogContext& Context) = 0;
};

// ============================================================================
// FInternalLogService - Integration with existing FLogBuffer
// ============================================================================

class FInternalLogService : public ILogSink
{
public:
    virtual void Receive(const FLogEntry& Entry, const FLogContext& Context) override;
};

// ============================================================================
// FLogger - Global Logging Hub
// ============================================================================

class FLIGHTPROJECT_API FLogger
{
public:
    static FLogger& Get()
    {
        static FLogger Instance;
        return Instance;
    }

    /** Register a new output destination */
    void AddSink(TSharedPtr<ILogSink> Sink)
    {
        Sinks.Add(MoveTemp(Sink));
    }

    /** Remove a sink */
    void RemoveSink(TSharedPtr<ILogSink> Sink)
    {
        Sinks.Remove(Sink);
    }

    /**
     * Primary logging entry point.
     * Use this instead of UE_LOG for core FlightProject logic.
     */
    void Log(ELogLevel Level, FName Category, const FString& Message, const FLogContext& Context = {})
    {
        EnsureDefaultSinks();

        if (::Flight::Log::bIsLoggingInternal)
        {
            return;
        }

        TGuardValue<bool> RecursionGuard(::Flight::Log::bIsLoggingInternal, true);

        FLogEntry Entry(0, FPlatformTime::Seconds(), Level, Category, Message);
        Entry.Context = Context.KeyValues;
        
        // Broadcast to all sinks
        for (auto& Sink : Sinks)
        {
            if (Sink.IsValid())
            {
                Sink->Receive(Entry, Context);
            }
        }
    }

    /**
     * Reflective Logging - Logs all fields of a reflectable struct.
     */
    template<CReflectable T>
    void LogStruct(ELogLevel Level, FName Category, const T& Value, const FString& Prefix = TEXT(""))
    {
        FLogContext Context;
        
        // Build structured message using reflection
        FString StructName = TReflectTraits<T>::Name;
        FString Message = Prefix.IsEmpty() ? StructName : FString::Printf(TEXT("%s: %s"), *Prefix, *StructName);

        ForEachField(Value, [&Context](auto& FieldValue, const char* Name) {
            using FieldType = std::decay_t<decltype(FieldValue)>;
            
            FString ValueStr;
            if constexpr (std::is_same_v<FieldType, FString>) ValueStr = FieldValue;
            else if constexpr (std::is_same_v<FieldType, FVector>) ValueStr = FieldValue.ToString();
            else if constexpr (std::is_same_v<FieldType, bool>) ValueStr = FieldValue ? TEXT("true") : TEXT("false");
            else if constexpr (std::is_arithmetic_v<FieldType>) ValueStr = FString::Printf(TEXT("%g"), (double)FieldValue);
            else if constexpr (CReflectable<FieldType>) ValueStr = TEXT("{...}"); // Could recurse if needed
            else ValueStr = TEXT("?");

            Context.KeyValues.Add(FString(UTF8_TO_TCHAR(Name)), ValueStr);
        });

        Log(Level, Category, Message, Context);
    }

    /**
     * Row Logging - Logs a TRow as structured data.
     */
    template<typename... Fields>
    void LogRow(ELogLevel Level, FName Category, const TRow<Fields...>& Row, const FString& Message = TEXT(""))
    {
        FLogContext Context;
        Row.ForEach([&Context](const auto& Value, std::string_view Name) {
            using FieldType = std::decay_t<decltype(Value)>;
            
            FString ValueStr;
            if constexpr (std::is_same_v<FieldType, FString>) ValueStr = Value;
            else if constexpr (std::is_same_v<FieldType, FVector>) ValueStr = Value.ToString();
            else if constexpr (std::is_same_v<FieldType, bool>) ValueStr = Value ? TEXT("true") : TEXT("false");
            else if constexpr (std::is_arithmetic_v<FieldType>) ValueStr = FString::Printf(TEXT("%g"), (double)Value);
            else ValueStr = TEXT("?");

            Context.KeyValues.Add(FString(UTF8_TO_TCHAR(Name.data())), ValueStr);
        });

        Log(Level, Category, Message, Context);
    }

private:
    FLogger() = default;
    void EnsureDefaultSinks();

    TSharedPtr<ILogSink> DefaultInternalSink;
    TSharedPtr<ILogSink> DefaultUnrealSink;
    TArray<TSharedPtr<ILogSink>> Sinks;
};

// ============================================================================
// UnrealLogSink - Integration with GLog/UE_LOG
// ============================================================================

class FUnrealLogSink : public ILogSink
{
public:
    virtual void Receive(const FLogEntry& Entry, const FLogContext& Context) override
    {
        // Convert back to UE verbosity
        ELogVerbosity::Type UEVerbosity = ELogVerbosity::Log;
        switch (Entry.Verbosity)
        {
            case ELogLevel::Fatal:   UEVerbosity = ELogVerbosity::Fatal; break;
            case ELogLevel::Error:   UEVerbosity = ELogVerbosity::Error; break;
            case ELogLevel::Warning: UEVerbosity = ELogVerbosity::Warning; break;
            case ELogLevel::Display: UEVerbosity = ELogVerbosity::Display; break;
            case ELogLevel::Log:     UEVerbosity = ELogVerbosity::Log; break;
            case ELogLevel::Verbose: UEVerbosity = ELogVerbosity::Verbose; break;
            case ELogLevel::VeryVerbose: UEVerbosity = ELogVerbosity::VeryVerbose; break;
        }

        // Output to Unreal Log
        // If we have context, format it into the string for UE_LOG
        FString FinalMessage = Entry.Message;
        if (!Context.IsEmpty())
        {
            FinalMessage += Context.Format();
        }

        if (GLog)
        {
            GLog->Serialize(*FinalMessage, UEVerbosity, Entry.Category);
        }
    }
};

inline void FLogger::EnsureDefaultSinks()
{
    if (!DefaultInternalSink.IsValid())
    {
        DefaultInternalSink = MakeShared<FInternalLogService>();
        Sinks.Add(DefaultInternalSink);
    }

    if (!DefaultUnrealSink.IsValid())
    {
        DefaultUnrealSink = MakeShared<FUnrealLogSink>();
        Sinks.Add(DefaultUnrealSink);
    }
}

// ============================================================================
// Functional Extensions for TResult
// ============================================================================

/**
 * TResultExtensions - Monadic logging operators for TResult.
 */
template<typename T, typename E>
struct TResultLogging
{
    /** Log the result (Ok or Err) and return it unchanged */
    static TResult<T, E> TapLog(TResult<T, E>&& Result, FName Category, ELogLevel Level = ELogLevel::Log)
    {
        if (Result.IsOk())
        {
            FLogger::Get().Log(Level, Category, TEXT("Success: Operation completed."));
        }
        else
        {
            if constexpr (std::is_same_v<E, FString>)
            {
                FLogger::Get().Log(ELogLevel::Error, Category, FString::Printf(TEXT("Error: %s"), *Result.GetError()));
            }
            else
            {
                FLogger::Get().Log(ELogLevel::Error, Category, FString::Printf(TEXT("Error: %s"), *Result.GetError().ToString()));
            }
        }
        return MoveTemp(Result);
    }

    /** Log only if the result is an error */
    static TResult<T, E> LogErr(TResult<T, E>&& Result, FName Category)
    {
        if (Result.IsErr())
        {
            if constexpr (std::is_same_v<E, FString>)
            {
                FLogger::Get().Log(ELogLevel::Error, Category, FString::Printf(TEXT("Operation Failed: %s"), *Result.GetError()));
            }
            else
            {
                FLogger::Get().Log(ELogLevel::Error, Category, FString::Printf(TEXT("Operation Failed: %s"), *Result.GetError().ToString()));
            }
        }
        return MoveTemp(Result);
    }
};

/**
 * Global functional logging helpers.
 */
template<typename T, typename E>
inline TResult<T, E> TapLog(TResult<T, E>&& Result, FName Category, ELogLevel Level = ELogLevel::Log)
{
    return TResultLogging<T, E>::TapLog(MoveTemp(Result), Category, Level);
}

template<typename T, typename E>
inline TResult<T, E> LogErr(TResult<T, E>&& Result, FName Category)
{
    return TResultLogging<T, E>::LogErr(MoveTemp(Result), Category);
}

// ============================================================================
// Global Convenience Macros
// ============================================================================

/**
 * FLIGHT_LOG - Main logging macro for FlightProject
 * Decoupled from UE_LOG, can be redirected to any sink.
 */
#define FLIGHT_LOG_NEW(Category, Level, Format, ...) \
    ::Flight::Logging::FLogger::Get().Log(::Flight::Logging::ELogLevel::Level, Category, FString::Printf(TEXT(Format), ##__VA_ARGS__))

/**
 * FLIGHT_LOG_STRUCT - Logs all fields of a reflectable struct
 */
#define FLIGHT_LOG_STRUCT(Category, Level, StructInstance) \
    ::Flight::Logging::FLogger::Get().LogStruct(::Flight::Logging::ELogLevel::Level, Category, StructInstance)

/**
 * FLIGHT_LOG_STRUCT_MSG - Logs all fields with a custom prefix message
 */
#define FLIGHT_LOG_STRUCT_MSG(Category, Level, StructInstance, Format, ...) \
    ::Flight::Logging::FLogger::Get().LogStruct(::Flight::Logging::ELogLevel::Level, Category, StructInstance, FString::Printf(TEXT(Format), ##__VA_ARGS__))

} // namespace Flight::Logging
