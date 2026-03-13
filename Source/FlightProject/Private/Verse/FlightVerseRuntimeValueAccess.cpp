// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "Verse/FlightVerseRuntimeValueAccess.h"

namespace Flight::VerseRuntime
{

uint32 ResolveRuntimeValueStride(const Flight::Vex::FVexSymbolRecord& Symbol)
{
	if (Symbol.Storage.ElementStride != 0)
	{
		return Symbol.Storage.ElementStride;
	}

	switch (Symbol.ValueType)
	{
	case Flight::Vex::EVexValueType::Float:
		return sizeof(float);
	case Flight::Vex::EVexValueType::Float2:
		return sizeof(FVector2f);
	case Flight::Vex::EVexValueType::Float3:
		return sizeof(FVector3f);
	case Flight::Vex::EVexValueType::Float4:
		return sizeof(FVector4f);
	case Flight::Vex::EVexValueType::Int:
		return sizeof(int32);
	case Flight::Vex::EVexValueType::Bool:
		return sizeof(bool);
	default:
		return 0;
	}
}

Flight::Vex::FVexRuntimeValue ReadRuntimeValueFromAddress(
	const uint8* Bytes,
	const Flight::Vex::EVexValueType ValueType,
	const uint32 ElementStride)
{
	using FNativeValue = Flight::Vex::FVexRuntimeValue;

	switch (ValueType)
	{
	case Flight::Vex::EVexValueType::Float:
		return FNativeValue::FromFloat(*reinterpret_cast<const float*>(Bytes));
	case Flight::Vex::EVexValueType::Int:
	{
		if (ElementStride == sizeof(uint32))
		{
			return FNativeValue::FromInt(static_cast<int32>(*reinterpret_cast<const uint32*>(Bytes)));
		}
		return FNativeValue::FromInt(*reinterpret_cast<const int32*>(Bytes));
	}
	case Flight::Vex::EVexValueType::Bool:
		return FNativeValue::FromBool(*reinterpret_cast<const bool*>(Bytes));
	case Flight::Vex::EVexValueType::Float2:
	{
		if (ElementStride == sizeof(FVector2D))
		{
			const FVector2D& Value = *reinterpret_cast<const FVector2D*>(Bytes);
			return FNativeValue::FromVec2(FVector2f(Value));
		}
		return FNativeValue::FromVec2(*reinterpret_cast<const FVector2f*>(Bytes));
	}
	case Flight::Vex::EVexValueType::Float3:
	{
		if (ElementStride == sizeof(FVector))
		{
			const FVector& Value = *reinterpret_cast<const FVector*>(Bytes);
			return FNativeValue::FromVec3(FVector3f(Value));
		}
		return FNativeValue::FromVec3(*reinterpret_cast<const FVector3f*>(Bytes));
	}
	case Flight::Vex::EVexValueType::Float4:
	{
		if (ElementStride == sizeof(FVector4))
		{
			const FVector4& Value = *reinterpret_cast<const FVector4*>(Bytes);
			return FNativeValue::FromVec4(FVector4f(Value));
		}
		return FNativeValue::FromVec4(*reinterpret_cast<const FVector4f*>(Bytes));
	}
	default:
		return FNativeValue();
	}
}

bool WriteRuntimeValueToAddress(
	uint8* Bytes,
	const Flight::Vex::EVexValueType ValueType,
	const uint32 ElementStride,
	const Flight::Vex::FVexRuntimeValue& Value)
{
	switch (ValueType)
	{
	case Flight::Vex::EVexValueType::Float:
		*reinterpret_cast<float*>(Bytes) = Value.AsFloat();
		return true;
	case Flight::Vex::EVexValueType::Int:
		if (ElementStride == sizeof(uint32))
		{
			*reinterpret_cast<uint32*>(Bytes) = static_cast<uint32>(FMath::Max(0, Value.AsInt()));
			return true;
		}
		*reinterpret_cast<int32*>(Bytes) = Value.AsInt();
		return true;
	case Flight::Vex::EVexValueType::Bool:
		*reinterpret_cast<bool*>(Bytes) = Value.AsBool();
		return true;
	case Flight::Vex::EVexValueType::Float2:
		if (ElementStride == sizeof(FVector2D))
		{
			const FVector2f Vec2Value = Value.AsVec2();
			*reinterpret_cast<FVector2D*>(Bytes) = FVector2D(Vec2Value.X, Vec2Value.Y);
			return true;
		}
		*reinterpret_cast<FVector2f*>(Bytes) = Value.AsVec2();
		return true;
	case Flight::Vex::EVexValueType::Float3:
		if (ElementStride == sizeof(FVector))
		{
			const FVector3f Vec3Value = Value.AsVec3();
			*reinterpret_cast<FVector*>(Bytes) = FVector(Vec3Value.X, Vec3Value.Y, Vec3Value.Z);
			return true;
		}
		*reinterpret_cast<FVector3f*>(Bytes) = Value.AsVec3();
		return true;
	case Flight::Vex::EVexValueType::Float4:
		if (ElementStride == sizeof(FVector4))
		{
			const FVector4f Vec4Value = Value.AsVec4();
			*reinterpret_cast<FVector4*>(Bytes) = FVector4(Vec4Value.X, Vec4Value.Y, Vec4Value.Z, Vec4Value.W);
			return true;
		}
		*reinterpret_cast<FVector4f*>(Bytes) = Value.AsVec4();
		return true;
	default:
		return false;
	}
}

} // namespace Flight::VerseRuntime
