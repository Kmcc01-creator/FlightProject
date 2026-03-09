// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Schema/FlightRequirementSchema.h"

namespace Flight::Reflection::HLSL
{

template<typename T>
FString GetHlslTypeName()
{
	if constexpr (std::is_same_v<T, float>) return TEXT("float");
	if constexpr (std::is_same_v<T, FVector2f>) return TEXT("float2");
	if constexpr (std::is_same_v<T, FVector3f>) return TEXT("float3");
	if constexpr (std::is_same_v<T, FVector4f>) return TEXT("float4");
	if constexpr (std::is_same_v<T, int32>) return TEXT("int");
	if constexpr (std::is_same_v<T, uint32>) return TEXT("uint");
	if constexpr (std::is_same_v<T, bool>) return TEXT("bool");
	return TEXT("/* unknown */");
}

template<typename T>
EFlightVexSymbolValueType GetVexValueType()
{
	if constexpr (std::is_same_v<T, float>) return EFlightVexSymbolValueType::Float;
	if constexpr (std::is_same_v<T, FVector2f>) return EFlightVexSymbolValueType::Float2;
	if constexpr (std::is_same_v<T, FVector3f>) return EFlightVexSymbolValueType::Float3;
	if constexpr (std::is_same_v<T, FVector4f>) return EFlightVexSymbolValueType::Float4;
	if constexpr (std::is_same_v<T, int32>) return EFlightVexSymbolValueType::Int;
	if constexpr (std::is_same_v<T, uint32>) return EFlightVexSymbolValueType::Int;
	if constexpr (std::is_same_v<T, bool>) return EFlightVexSymbolValueType::Bool;
	return EFlightVexSymbolValueType::Float;
}

/** 
 * Generate HLSL struct definition from C++ traits.
 */
template<CReflectable T>
FString GenerateStructHLSL(const FString& InStructName = FString())
{
	FString StructName = InStructName.IsEmpty() ? FString(TReflectionTraits<T>::Name) : InStructName;
	FString Hlsl = FString::Printf(TEXT("struct %s\n{\n"), *StructName);

    TReflectionTraits<T>::Fields::ForEachDescriptor([&Hlsl](auto Descriptor)
    {
        using DescType = decltype(Descriptor);
		if constexpr (requires { typename DescType::FieldType; })
		{
			using FieldType = std::remove_cvref_t<typename DescType::FieldType>;
			
			Hlsl += FString::Printf(TEXT("    %s %s;\n"), 
				*GetHlslTypeName<FieldType>(), 
				ANSI_TO_TCHAR(DescType::NameCStr));
		}
    });

	Hlsl += TEXT("};\n");
	return Hlsl;
}

/** 
 * Computes a hash of the struct layout to detect CPU/GPU parity mismatches.
 */
template<CReflectable T>
uint32 ComputeLayoutHash()
{
	FString LayoutStr;
    TReflectionTraits<T>::Fields::ForEachDescriptor([&LayoutStr](auto Descriptor)
    {
        using DescType = decltype(Descriptor);
		if constexpr (requires { typename DescType::FieldType; })
		{
			using FieldType = std::remove_cvref_t<typename DescType::FieldType>;
			LayoutStr += GetHlslTypeName<FieldType>();
			LayoutStr += ANSI_TO_TCHAR(DescType::NameCStr);
		}
    });
	return FCrc::StrCrc32(*LayoutStr);
}

/**
 * Generate HLSL with a contract hash embedded as a comment.
 */
template<CReflectable T>
FString GenerateStructContractHLSL(const FString& StructName, const FString& HashMacroName)
{
	uint32 Hash = ComputeLayoutHash<T>();
	FString Hlsl = GenerateStructHLSL<T>(StructName);
	Hlsl += FString::Printf(TEXT("\n// LayoutHash: 0x%08X\n"), Hash);
	Hlsl += FString::Printf(TEXT("#define %s 0x%08X\n"), *HashMacroName, Hash);
	return Hlsl;
}

/**
 * Extract a layout hash from a source string given a macro name.
 */
inline bool ExtractLayoutHashFromContract(const FString& Source, const FString& HashMacroName, uint32& OutHash)
{
	FString SearchStr = FString::Printf(TEXT("#define %s 0x"), *HashMacroName);
	int32 FoundIdx = Source.Find(SearchStr);
	if (FoundIdx == INDEX_NONE) return false;

	FString HexStr = Source.Mid(FoundIdx + SearchStr.Len(), 8);
	OutHash = static_cast<uint32>(FCString::Strtoui64(*HexStr, nullptr, 16));
	return true;
}

/**
 * Generates VEX symbol requirements from C++ reflection.
 */
template<CReflectable T>
TArray<FFlightVexSymbolRow> GenerateVexSymbolsFromStruct(FName Owner)
{
	TArray<FFlightVexSymbolRow> Rows;

	TReflectionTraits<T>::Fields::ForEachDescriptor([&](auto Descriptor)
	{
		using DescType = decltype(Descriptor);
		if constexpr (requires { typename DescType::FieldType; } && DescType::template HasAttrTemplate<Attr::VexSymbol>)
		{
			using VexAttr = typename DescType::template GetAttr<Attr::VexSymbol>;
			using FieldType = std::remove_cvref_t<typename DescType::FieldType>;

			FFlightVexSymbolRow Row;
			Row.Owner = Owner;
			Row.RequirementId = FName(ANSI_TO_TCHAR(DescType::NameCStr));
			Row.SymbolName = FString(VexAttr::Value.data());
			Row.ValueType = GetVexValueType<FieldType>();
			
			if constexpr (DescType::template HasAttrTemplate<Attr::HlslIdentifier>)
			{
				using HlslAttr = typename DescType::template GetAttr<Attr::HlslIdentifier>;
				Row.HlslIdentifier = FString(HlslAttr::Value.data());
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::VerseIdentifier>)
			{
				using VerseAttr = typename DescType::template GetAttr<Attr::VerseIdentifier>;
				Row.VerseIdentifier = FString(VerseAttr::Value.data());
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::VexResidency>)
			{
				using ResidencyAttr = typename DescType::template GetAttr<Attr::VexResidency>;
				Row.Residency = ResidencyAttr::Value;
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::ThreadAffinity>)
			{
				using AffinityAttr = typename DescType::template GetAttr<Attr::ThreadAffinity>;
				Row.Affinity = AffinityAttr::Value;
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::SimdReadAllowed>)
			{
				using SimdReadAttr = typename DescType::template GetAttr<Attr::SimdReadAllowed>;
				Row.bSimdReadAllowed = SimdReadAttr::Value;
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::SimdWriteAllowed>)
			{
				using SimdWriteAttr = typename DescType::template GetAttr<Attr::SimdWriteAllowed>;
				Row.bSimdWriteAllowed = SimdWriteAttr::Value;
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::GpuTier1Allowed>)
			{
				using GpuTier1Attr = typename DescType::template GetAttr<Attr::GpuTier1Allowed>;
				Row.bGpuTier1Allowed = GpuTier1Attr::Value;
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::VexAlignment>)
			{
				using AlignmentAttr = typename DescType::template GetAttr<Attr::VexAlignment>;
				Row.AlignmentRequirement = AlignmentAttr::Value;
			}

			if constexpr (DescType::template HasAttrTemplate<Attr::MathDeterminism>)
			{
				using DeterminismAttr = typename DescType::template GetAttr<Attr::MathDeterminism>;
				Row.MathDeterminismProfile = DeterminismAttr::Value;
			}

			Row.bWritable = DescType::IsEditable;

			Row.bRequired = true;

			Rows.Add(MoveTemp(Row));
		}
	});

	return Rows;
}

} // namespace Flight::Reflection::HLSL
