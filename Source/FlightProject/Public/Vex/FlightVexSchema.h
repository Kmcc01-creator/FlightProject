// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexSchemaTypes.h"
#include "Vex/FlightVexTypes.h"
#include "UObject/WeakObjectPtr.h"

namespace Flight::Vex
{

struct FVexRuntimeValue
{
	EVexValueType Type = EVexValueType::Unknown;
	float FloatValue = 0.0f;
	int32 IntValue = 0;
	bool BoolValue = false;
	FVector2f Vec2Value = FVector2f::ZeroVector;
	FVector3f Vec3Value = FVector3f::ZeroVector;
	FVector4f Vec4Value = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);

	static FVexRuntimeValue FromFloat(const float Value)
	{
		FVexRuntimeValue Result;
		Result.Type = EVexValueType::Float;
		Result.FloatValue = Value;
		Result.IntValue = static_cast<int32>(Value);
		Result.BoolValue = !FMath::IsNearlyZero(Value);
		return Result;
	}

	static FVexRuntimeValue FromInt(const int32 Value)
	{
		FVexRuntimeValue Result;
		Result.Type = EVexValueType::Int;
		Result.IntValue = Value;
		Result.FloatValue = static_cast<float>(Value);
		Result.BoolValue = Value != 0;
		return Result;
	}

	static FVexRuntimeValue FromBool(const bool Value)
	{
		FVexRuntimeValue Result;
		Result.Type = EVexValueType::Bool;
		Result.BoolValue = Value;
		Result.IntValue = Value ? 1 : 0;
		Result.FloatValue = Value ? 1.0f : 0.0f;
		return Result;
	}

	static FVexRuntimeValue FromVec2(const FVector2f& Value)
	{
		FVexRuntimeValue Result;
		Result.Type = EVexValueType::Float2;
		Result.Vec2Value = Value;
		Result.FloatValue = Value.Size();
		Result.BoolValue = !Value.IsNearlyZero();
		return Result;
	}

	static FVexRuntimeValue FromVec3(const FVector3f& Value)
	{
		FVexRuntimeValue Result;
		Result.Type = EVexValueType::Float3;
		Result.Vec3Value = Value;
		Result.FloatValue = Value.Size();
		Result.BoolValue = !Value.IsNearlyZero();
		return Result;
	}

	static FVexRuntimeValue FromVec4(const FVector4f& Value)
	{
		FVexRuntimeValue Result;
		Result.Type = EVexValueType::Float4;
		Result.Vec4Value = Value;
		Result.FloatValue = FMath::Sqrt((Value.X * Value.X) + (Value.Y * Value.Y) + (Value.Z * Value.Z) + (Value.W * Value.W));
		Result.BoolValue = !Value.IsNearlyZero();
		return Result;
	}

	float AsFloat() const
	{
		switch (Type)
		{
		case EVexValueType::Float: return FloatValue;
		case EVexValueType::Int: return static_cast<float>(IntValue);
		case EVexValueType::Bool: return BoolValue ? 1.0f : 0.0f;
		case EVexValueType::Float2: return Vec2Value.Size();
		case EVexValueType::Float3: return Vec3Value.Size();
		case EVexValueType::Float4:
			return FMath::Sqrt((Vec4Value.X * Vec4Value.X) + (Vec4Value.Y * Vec4Value.Y) + (Vec4Value.Z * Vec4Value.Z) + (Vec4Value.W * Vec4Value.W));
		default:
			return 0.0f;
		}
	}

	int32 AsInt() const
	{
		switch (Type)
		{
		case EVexValueType::Int: return IntValue;
		case EVexValueType::Bool: return BoolValue ? 1 : 0;
		case EVexValueType::Float: return static_cast<int32>(FloatValue);
		case EVexValueType::Float2: return static_cast<int32>(Vec2Value.Size());
		case EVexValueType::Float3: return static_cast<int32>(Vec3Value.Size());
		case EVexValueType::Float4: return static_cast<int32>(AsFloat());
		default: return 0;
		}
	}

	bool AsBool() const
	{
		switch (Type)
		{
		case EVexValueType::Bool: return BoolValue;
		case EVexValueType::Int: return IntValue != 0;
		case EVexValueType::Float: return !FMath::IsNearlyZero(FloatValue);
		case EVexValueType::Float2: return !Vec2Value.IsNearlyZero();
		case EVexValueType::Float3: return !Vec3Value.IsNearlyZero();
		case EVexValueType::Float4: return !Vec4Value.IsNearlyZero();
		default: return false;
		}
	}

	FVector2f AsVec2() const
	{
		switch (Type)
		{
		case EVexValueType::Float2: return Vec2Value;
		case EVexValueType::Float3: return FVector2f(Vec3Value.X, Vec3Value.Y);
		case EVexValueType::Float4: return FVector2f(Vec4Value.X, Vec4Value.Y);
		case EVexValueType::Float: return FVector2f(FloatValue);
		case EVexValueType::Int: return FVector2f(static_cast<float>(IntValue));
		case EVexValueType::Bool: return FVector2f(BoolValue ? 1.0f : 0.0f);
		default: return FVector2f::ZeroVector;
		}
	}

	FVector3f AsVec3() const
	{
		switch (Type)
		{
		case EVexValueType::Float3: return Vec3Value;
		case EVexValueType::Float4: return FVector3f(Vec4Value.X, Vec4Value.Y, Vec4Value.Z);
		case EVexValueType::Float2: return FVector3f(Vec2Value.X, Vec2Value.Y, 0.0f);
		case EVexValueType::Float: return FVector3f(FloatValue);
		case EVexValueType::Int: return FVector3f(static_cast<float>(IntValue));
		case EVexValueType::Bool: return FVector3f(BoolValue ? 1.0f : 0.0f);
		default: return FVector3f::ZeroVector;
		}
	}

	FVector4f AsVec4() const
	{
		switch (Type)
		{
		case EVexValueType::Float4: return Vec4Value;
		case EVexValueType::Float3: return FVector4f(Vec3Value.X, Vec3Value.Y, Vec3Value.Z, 0.0f);
		case EVexValueType::Float2: return FVector4f(Vec2Value.X, Vec2Value.Y, 0.0f, 0.0f);
		case EVexValueType::Float: return FVector4f(FloatValue, FloatValue, FloatValue, FloatValue);
		case EVexValueType::Int:
		{
			const float Scalar = static_cast<float>(IntValue);
			return FVector4f(Scalar, Scalar, Scalar, Scalar);
		}
		case EVexValueType::Bool:
		{
			const float Scalar = BoolValue ? 1.0f : 0.0f;
			return FVector4f(Scalar, Scalar, Scalar, Scalar);
		}
		default:
			return FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		}
	}
};

/**
 * FVexSymbolAccessor: Handle for a specific symbol's Get/Set logic.
 * Enhanced with offset for zero-cost direct memory access.
 */
struct FVexSymbolAccessor
{
	using GetterFn = TFunction<float(const void*)>;
	using SetterFn = TFunction<void(void*, float)>;

	FString SymbolName;
	GetterFn Getter;
	SetterFn Setter;
	EVexValueType ValueType = EVexValueType::Unknown;

	/** Offset from the start of the struct (INDEX_NONE if not a simple POD field) */
	int32 MemberOffset = INDEX_NONE;

	/** Metadata for compiler validation */
	EFlightVexSymbolResidency Residency = EFlightVexSymbolResidency::Shared;
	FString HlslIdentifier;
	FString VerseIdentifier;
};

/**
 * FVexSymbolRecord: Canonical symbol contract for schema construction.
 * Legacy accessor/logical views are derived from this record for compatibility.
 */
struct FVexSymbolRecord
{
	using ReadValueFn = TFunction<FVexRuntimeValue(const void*)>;
	using WriteValueFn = TFunction<bool(void*, const FVexRuntimeValue&)>;

	FString SymbolName;
	EVexValueType ValueType = EVexValueType::Unknown;
	bool bReadable = true;
	bool bWritable = true;
	bool bRequired = false;
	bool bSimdReadAllowed = true;
	bool bSimdWriteAllowed = true;
	bool bGpuTier1Allowed = true;
	EFlightVexSymbolResidency Residency = EFlightVexSymbolResidency::Shared;
	EFlightVexSymbolAffinity Affinity = EFlightVexSymbolAffinity::Any;
	EFlightVexAlignmentRequirement AlignmentRequirement = EFlightVexAlignmentRequirement::Any;
	EFlightVexMathDeterminismProfile MathDeterminismProfile = EFlightVexMathDeterminismProfile::Fast;
	FVexBackendBinding BackendBinding;
	FVexStorageBinding Storage;
	FVexVectorPackContract VectorPack;
	ReadValueFn ReadValue;
	WriteValueFn WriteValue;
	FVexSymbolAccessor::GetterFn Getter;
	FVexSymbolAccessor::SetterFn Setter;

	FVexRuntimeValue ReadRuntimeValue(const void* Instance) const
	{
		if (ReadValue)
		{
			return ReadValue(Instance);
		}

		if (Getter)
		{
			return FVexRuntimeValue::FromFloat(Getter(Instance));
		}

		return FVexRuntimeValue();
	}

	bool WriteRuntimeValue(void* Instance, const FVexRuntimeValue& Value) const
	{
		if (WriteValue)
		{
			return WriteValue(Instance, Value);
		}

		if (Setter)
		{
			Setter(Instance, Value.AsFloat());
			return true;
		}

		return false;
	}

	FVexSymbolAccessor MakeAccessor() const
	{
		FVexSymbolAccessor Accessor;
		Accessor.SymbolName = SymbolName;
		if (Getter)
		{
			Accessor.Getter = Getter;
		}
		else if (ReadValue)
		{
			const ReadValueFn ReadValueCopy = ReadValue;
			Accessor.Getter = [ReadValueCopy](const void* Ptr) -> float
			{
				return ReadValueCopy(Ptr).AsFloat();
			};
		}

		if (Setter)
		{
			Accessor.Setter = Setter;
		}
		else if (WriteValue)
		{
			const WriteValueFn WriteValueCopy = WriteValue;
			Accessor.Setter = [WriteValueCopy](void* Ptr, float Value)
			{
				WriteValueCopy(Ptr, FVexRuntimeValue::FromFloat(Value));
			};
		}
		Accessor.ValueType = ValueType;
		Accessor.MemberOffset = Storage.MemberOffset;
		Accessor.Residency = Residency;
		Accessor.HlslIdentifier = BackendBinding.HlslIdentifier;
		Accessor.VerseIdentifier = BackendBinding.VerseIdentifier;
		return Accessor;
	}

	FVexLogicalSymbolSchema MakeLogicalSymbol() const
	{
		FVexLogicalSymbolSchema Logical;
		Logical.SymbolName = SymbolName;
		Logical.ValueType = ValueType;
		Logical.bReadable = bReadable;
		Logical.bWritable = bWritable;
		Logical.bRequired = bRequired;
		Logical.bSimdReadAllowed = bSimdReadAllowed;
		Logical.bSimdWriteAllowed = bSimdWriteAllowed;
		Logical.bGpuTier1Allowed = bGpuTier1Allowed;
		Logical.Residency = Residency;
		Logical.Affinity = Affinity;
		Logical.AlignmentRequirement = AlignmentRequirement;
		Logical.MathDeterminismProfile = MathDeterminismProfile;
		Logical.BackendBinding = BackendBinding;
		Logical.Storage = Storage;
		Logical.VectorPack = VectorPack.StorageClass == EVexVectorStorageClass::None
			? BuildDefaultVectorPackContract(Storage, ValueType, AlignmentRequirement, bSimdReadAllowed, bSimdWriteAllowed, bGpuTier1Allowed)
			: VectorPack;
		return Logical;
	}
};

/**
 * FVexTypeSchema: The definitive "Identity Card" for a reflectable C++ type in the VEX system.
 * This bridges pure C++ traits with runtime orchestration and asset generation.
 */
struct FLIGHTPROJECT_API FVexTypeSchema
{
	/** Stable runtime and reporting identity for this type schema */
	FVexTypeId TypeId;

	/** Formal name of the C++ type (from traits) */
	FString TypeName;

	/** Total size of the struct in memory */
	uint32 Size = 0;

	/** Alignment requirement */
	uint32 Alignment = 0;

	/** Canonical map of symbol contracts; preferred source of truth for new code. */
	TMap<FString, FVexSymbolRecord> SymbolRecords;

	/** Map of @symbols to their accessors and metadata */
	TMap<FString, FVexSymbolAccessor> Symbols;

	/** Compiler-facing logical symbol records */
	TMap<FString, FVexLogicalSymbolSchema> LogicalSymbols;

	/** Optional bridge back to Unreal's reflection if it's a USTRUCT */
	TWeakObjectPtr<UScriptStruct> NativeStruct;

	/** Unique hash representing the data layout (for versioning/binary compatibility) */
	uint32 LayoutHash = 0;

	/** Returns true if this schema has a direct memory mapping for a symbol */
	bool HasSymbol(const FString& Name) const { return SymbolRecords.Contains(Name) || Symbols.Contains(Name) || LogicalSymbols.Contains(Name); }

	const FVexSymbolRecord* FindSymbolRecord(const FString& Name) const
	{
		return SymbolRecords.Find(Name);
	}

	const FVexSymbolAccessor* FindAccessor(const FString& Name) const
	{
		if (const FVexSymbolRecord* Record = FindSymbolRecord(Name))
		{
			return Symbols.Find(Record->SymbolName);
		}
		return Symbols.Find(Name);
	}

	const FVexLogicalSymbolSchema* FindLogicalSymbol(const FString& Name) const
	{
		if (const FVexSymbolRecord* Record = FindSymbolRecord(Name))
		{
			return LogicalSymbols.Find(Record->SymbolName);
		}
		return LogicalSymbols.Find(Name);
	}

	void RebuildLegacyViews()
	{
		Symbols.Reset();
		LogicalSymbols.Reset();

		for (TPair<FString, FVexSymbolRecord>& Pair : SymbolRecords)
		{
			if (Pair.Value.VectorPack.StorageClass == EVexVectorStorageClass::None)
			{
				Pair.Value.VectorPack = BuildDefaultVectorPackContract(
					Pair.Value.Storage,
					Pair.Value.ValueType,
					Pair.Value.AlignmentRequirement,
					Pair.Value.bSimdReadAllowed,
					Pair.Value.bSimdWriteAllowed,
					Pair.Value.bGpuTier1Allowed);
			}
			Symbols.Add(Pair.Key, Pair.Value.MakeAccessor());
			LogicalSymbols.Add(Pair.Key, Pair.Value.MakeLogicalSymbol());
		}
	}
};

} // namespace Flight::Vex
