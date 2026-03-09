// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FlightVexTypes.generated.h"

UENUM(BlueprintType)
enum class EFlightVexSymbolResidency : uint8
{
	Shared,  // Available to both Verse (CPU) and HLSL (GPU)
	GpuOnly, // Only available in @gpu blocks or HLSL kernels
	CpuOnly  // Only available in @cpu blocks or Verse scripts
};

UENUM(BlueprintType)
enum class EFlightVexSymbolAffinity : uint8
{
	Any,          // Safe to access from any thread
	GameThread,   // Must only be accessed from the Game Thread
	RenderThread, // Must only be accessed from the Render Thread
	WorkerThread  // Designed for background task parallelism
};

UENUM(BlueprintType)
enum class EFlightVexAlignmentRequirement : uint8
{
	Any,
	Align16,
	Align32
};

UENUM(BlueprintType)
enum class EFlightVexMathDeterminismProfile : uint8
{
	Fast,
	Precise,
	StrictParity
};

namespace Flight::Vex
{

enum class EVexValueType : uint8 { Unknown, Float, Float2, Float3, Float4, Int, Bool };

inline EVexValueType ParseValueType(const FString& TypeName)
{
	if (TypeName == TEXT("float")) return EVexValueType::Float;
	if (TypeName == TEXT("float2")) return EVexValueType::Float2;
	if (TypeName == TEXT("float3")) return EVexValueType::Float3;
	if (TypeName == TEXT("float4")) return EVexValueType::Float4;
	if (TypeName == TEXT("int")) return EVexValueType::Int;
	if (TypeName == TEXT("bool")) return EVexValueType::Bool;
	return EVexValueType::Unknown;
}

inline FString ToTypeString(const EVexValueType Type)
{
	switch (Type)
	{
	case EVexValueType::Float: return TEXT("float");
	case EVexValueType::Float2: return TEXT("float2");
	case EVexValueType::Float3: return TEXT("float3");
	case EVexValueType::Float4: return TEXT("float4");
	case EVexValueType::Int: return TEXT("int");
	case EVexValueType::Bool: return TEXT("bool");
	default: return TEXT("unknown");
	}
}

inline bool IsVectorType(const EVexValueType Type)
{
	return Type == EVexValueType::Float2 || Type == EVexValueType::Float3 || Type == EVexValueType::Float4;
}

inline bool IsDeclarationTypeToken(const FString& Lexeme)
{
	return Lexeme == TEXT("float") || Lexeme == TEXT("float2") || Lexeme == TEXT("float3")
		|| Lexeme == TEXT("float4") || Lexeme == TEXT("int") || Lexeme == TEXT("bool");
}

} // namespace Flight::Vex
