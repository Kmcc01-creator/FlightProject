// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"

class UStruct;

namespace Flight::Reflection::Debug
{

FLIGHTPROJECT_API TArray<FString> DumpNativeStructFields(const UStruct& StructType, const void* StructMemory);
FLIGHTPROJECT_API FString DumpNativeStructFieldsToString(
	const UStruct& StructType,
	const void* StructMemory,
	const FString& Delimiter = TEXT(", "));

template<CReflectable T>
FString DumpReflectableFieldsToString(const T& Value, const FString& Delimiter = TEXT(", "));

namespace Detail
{
	template<typename T>
	FString FormatReflectableValue(const T& Value)
	{
		using TValue = std::remove_cvref_t<T>;

		if constexpr (std::is_same_v<TValue, bool>)
		{
			return Value ? TEXT("true") : TEXT("false");
		}
		else if constexpr (std::is_same_v<TValue, FString>)
		{
			return Value;
		}
		else if constexpr (std::is_same_v<TValue, FName>)
		{
			return Value.ToString();
		}
		else if constexpr (std::is_same_v<TValue, FGuid>)
		{
			return Value.ToString(EGuidFormats::DigitsWithHyphensLower);
		}
		else if constexpr (std::is_enum_v<TValue>)
		{
			return FString::FromInt(static_cast<int32>(Value));
		}
		else if constexpr (std::is_integral_v<TValue> || std::is_floating_point_v<TValue>)
		{
			return LexToString(Value);
		}
		else if constexpr (CReflectable<TValue>)
		{
			return FString::Printf(TEXT("{%s}"), *DumpReflectableFieldsToString(Value));
		}
		else if constexpr (requires { Value.ToString(); })
		{
			return Value.ToString();
		}
		else
		{
			return TEXT("<unformatted>");
		}
	}
}

template<CReflectable T>
TArray<FString> DumpReflectableFields(const T& Value)
{
	TArray<FString> Lines;

	TReflectTraits<T>::Fields::ForEachValue(Value, [&Lines](const auto& FieldValue, auto Descriptor)
	{
		using DescType = decltype(Descriptor);
		Lines.Add(FString::Printf(
			TEXT("%s=%s"),
			UTF8_TO_TCHAR(DescType::NameCStr),
			*Detail::FormatReflectableValue(FieldValue)));
	});

	return Lines;
}

template<CReflectable T>
FString DumpReflectableFieldsToString(const T& Value, const FString& Delimiter)
{
	return FString::Join(DumpReflectableFields(Value), *Delimiter);
}

}
