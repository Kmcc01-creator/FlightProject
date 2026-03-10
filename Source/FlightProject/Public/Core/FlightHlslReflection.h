// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Schema/FlightRequirementSchema.h"

namespace Flight::Reflection::HLSL
{

namespace Detail
{

template<typename T, typename = void>
struct THasTypeAttributes : std::false_type
{
};

template<typename T>
struct THasTypeAttributes<T, std::void_t<typename TReflectTraits<T>::Attributes>> : std::true_type
{
};

template<CReflectable T>
using TTypeAttributes = typename TReflectTraits<T>::Attributes;

inline FString GpuResourceKindToString(const EFlightGpuResourceKind Kind)
{
	switch (Kind)
	{
	case EFlightGpuResourceKind::StructuredBuffer:
		return TEXT("StructuredBuffer");
	case EFlightGpuResourceKind::StorageBuffer:
		return TEXT("StorageBuffer");
	case EFlightGpuResourceKind::SampledImage:
		return TEXT("SampledImage");
	case EFlightGpuResourceKind::StorageImage:
		return TEXT("StorageImage");
	case EFlightGpuResourceKind::UniformBuffer:
		return TEXT("UniformBuffer");
	case EFlightGpuResourceKind::ReadbackBuffer:
		return TEXT("ReadbackBuffer");
	case EFlightGpuResourceKind::StagingBuffer:
		return TEXT("StagingBuffer");
	case EFlightGpuResourceKind::TransientAttachment:
		return TEXT("TransientAttachment");
	case EFlightGpuResourceKind::PersistentTexture:
		return TEXT("PersistentTexture");
	default:
		return TEXT("Unknown");
	}
}

inline FString GpuResourceLifetimeToString(const EFlightGpuResourceLifetime Lifetime)
{
	switch (Lifetime)
	{
	case EFlightGpuResourceLifetime::Transient:
		return TEXT("Transient");
	case EFlightGpuResourceLifetime::Persistent:
		return TEXT("Persistent");
	case EFlightGpuResourceLifetime::ExtractedPersistent:
		return TEXT("ExtractedPersistent");
	case EFlightGpuResourceLifetime::ExternalInterop:
		return TEXT("ExternalInterop");
	default:
		return TEXT("Unknown");
	}
}

inline FString GpuExecutionDomainToString(const EFlightGpuExecutionDomain Domain)
{
	switch (Domain)
	{
	case EFlightGpuExecutionDomain::Cpu:
		return TEXT("Cpu");
	case EFlightGpuExecutionDomain::RenderGraph:
		return TEXT("RenderGraph");
	case EFlightGpuExecutionDomain::GpuCompute:
		return TEXT("GpuCompute");
	case EFlightGpuExecutionDomain::ExternalInterop:
		return TEXT("ExternalInterop");
	default:
		return TEXT("Any");
	}
}

inline FString GpuAccessClassToString(const EFlightGpuAccessClass Access)
{
	switch (Access)
	{
	case EFlightGpuAccessClass::HostRead:
		return TEXT("HostRead");
	case EFlightGpuAccessClass::HostWrite:
		return TEXT("HostWrite");
	case EFlightGpuAccessClass::TransferSource:
		return TEXT("TransferSource");
	case EFlightGpuAccessClass::TransferDestination:
		return TEXT("TransferDestination");
	case EFlightGpuAccessClass::ShaderRead:
		return TEXT("ShaderRead");
	case EFlightGpuAccessClass::ShaderWrite:
		return TEXT("ShaderWrite");
	case EFlightGpuAccessClass::SampledRead:
		return TEXT("SampledRead");
	case EFlightGpuAccessClass::ColorAttachmentWrite:
		return TEXT("ColorAttachmentWrite");
	case EFlightGpuAccessClass::DepthStencilRead:
		return TEXT("DepthStencilRead");
	case EFlightGpuAccessClass::DepthStencilWrite:
		return TEXT("DepthStencilWrite");
	case EFlightGpuAccessClass::IndirectArgumentRead:
		return TEXT("IndirectArgumentRead");
	case EFlightGpuAccessClass::ExternalSync:
		return TEXT("ExternalSync");
	default:
		return TEXT("Unknown");
	}
}

} // namespace Detail

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
	FString StructName = InStructName.IsEmpty() ? FString(TReflectTraits<T>::Name) : InStructName;
	FString Hlsl = FString::Printf(TEXT("struct %s\n{\n"), *StructName);

    TReflectTraits<T>::Fields::ForEachDescriptor([&Hlsl](auto Descriptor)
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
    TReflectTraits<T>::Fields::ForEachDescriptor([&LayoutStr](auto Descriptor)
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

	TReflectTraits<T>::Fields::ForEachDescriptor([&](auto Descriptor)
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

template<CReflectable T>
constexpr bool HasGpuResourceContract()
{
	if constexpr (!Detail::THasTypeAttributes<T>::value)
	{
		return false;
	}
	else
	{
		return Detail::TTypeAttributes<T>::template HasTemplate<Attr::GpuResourceId>;
	}
}

template<CReflectable T>
TOptional<FFlightGpuResourceContractRow> GenerateGpuResourceContractFromStruct(FName Owner)
{
	if constexpr (!HasGpuResourceContract<T>())
	{
		return {};
	}
	else
	{
		using Attrs = Detail::TTypeAttributes<T>;
		using ResourceIdAttr = typename Attrs::template GetTemplate<Attr::GpuResourceId>;

		FFlightGpuResourceContractRow Row;
		Row.Owner = Owner;
		Row.ResourceId = FString(ResourceIdAttr::Value.data());
		Row.RequirementId = FName(*FString::Printf(TEXT("%s.Contract"), *Row.ResourceId));
		Row.ValueTypeName = ANSI_TO_TCHAR(TReflectTraits<T>::Name);
		Row.HlslStructName = ANSI_TO_TCHAR(TReflectTraits<T>::Name);
		Row.BindingName = Row.HlslStructName;
		Row.ElementStrideBytes = sizeof(T);
		Row.LayoutHash = static_cast<int32>(ComputeLayoutHash<T>());
		Row.bRequired = true;
		Row.bPreferUnrealRdg = true;
		Row.bRequiresRawVulkanInterop = false;

		if constexpr (Attrs::template HasTemplate<Attr::GpuResourceKind>)
		{
			using KindAttr = typename Attrs::template GetTemplate<Attr::GpuResourceKind>;
			Row.ResourceKind = KindAttr::Value;
		}

		if constexpr (Attrs::template HasTemplate<Attr::GpuResourceLifetime>)
		{
			using LifetimeAttr = typename Attrs::template GetTemplate<Attr::GpuResourceLifetime>;
			Row.ResourceLifetime = LifetimeAttr::Value;
		}

		if constexpr (Attrs::template HasTemplate<Attr::GpuBindingName>)
		{
			using BindingAttr = typename Attrs::template GetTemplate<Attr::GpuBindingName>;
			Row.BindingName = FString(BindingAttr::Value.data());
		}

		if constexpr (Attrs::template HasTemplate<Attr::PreferUnrealRdg>)
		{
			using PreferRdgAttr = typename Attrs::template GetTemplate<Attr::PreferUnrealRdg>;
			Row.bPreferUnrealRdg = PreferRdgAttr::Value;
		}

		if constexpr (Attrs::template HasTemplate<Attr::RawVulkanInteropRequired>)
		{
			using RawInteropAttr = typename Attrs::template GetTemplate<Attr::RawVulkanInteropRequired>;
			Row.bRequiresRawVulkanInterop = RawInteropAttr::Value;
		}

		return Row;
	}
}

template<CReflectable T>
TArray<FFlightGpuAccessRuleRow> GenerateGpuAccessRulesFromStruct(FName Owner)
{
	TArray<FFlightGpuAccessRuleRow> Rows;

	if constexpr (!HasGpuResourceContract<T>())
	{
		return Rows;
	}
	else
	{
		using Attrs = Detail::TTypeAttributes<T>;
		using ResourceIdAttr = typename Attrs::template GetTemplate<Attr::GpuResourceId>;
		const FString ResourceId = FString(ResourceIdAttr::Value.data());

		Attrs::template ForEachTemplate<Attr::GpuAccessRule>([&](auto AccessAttr)
		{
			using AccessAttrType = decltype(AccessAttr);

			FFlightGpuAccessRuleRow Row;
			Row.Owner = Owner;
			Row.ResourceId = ResourceId;
			Row.ExecutionDomain = AccessAttrType::DomainValue;
			Row.AccessClass = AccessAttrType::AccessValue;
			Row.bRequired = true;
			Row.RequirementId = FName(*FString::Printf(
				TEXT("%s.%s.%s"),
				*ResourceId,
				*Detail::GpuExecutionDomainToString(Row.ExecutionDomain),
				*Detail::GpuAccessClassToString(Row.AccessClass)));
			Rows.Add(MoveTemp(Row));
		});

		return Rows;
	}
}

template<CReflectable T>
FString GenerateGpuResourceContractInclude(const FString& InMacroPrefix = FString())
{
	const TOptional<FFlightGpuResourceContractRow> ContractRow = GenerateGpuResourceContractFromStruct<T>(TEXT("Reflection.Generated"));
	if (!ContractRow.IsSet())
	{
		return FString();
	}

	const TArray<FFlightGpuAccessRuleRow> AccessRows = GenerateGpuAccessRulesFromStruct<T>(TEXT("Reflection.Generated"));
	const FString MacroPrefix = InMacroPrefix.IsEmpty()
		? FString(ANSI_TO_TCHAR(TReflectTraits<T>::Name)).ToUpper()
		: InMacroPrefix;

	FString Source;
	Source += FString::Printf(TEXT("// GPU Resource Contract: %s\n"), *ContractRow->ResourceId);
	Source += FString::Printf(TEXT("// BindingName: %s\n"), *ContractRow->BindingName);
	Source += FString::Printf(TEXT("// ValueType: %s\n"), *ContractRow->ValueTypeName);
	Source += FString::Printf(TEXT("// ResourceKind: %s\n"), *Detail::GpuResourceKindToString(ContractRow->ResourceKind));
	Source += FString::Printf(TEXT("// ResourceLifetime: %s\n"), *Detail::GpuResourceLifetimeToString(ContractRow->ResourceLifetime));
	Source += FString::Printf(TEXT("#define %s_RESOURCE_KIND %d\n"), *MacroPrefix, static_cast<int32>(ContractRow->ResourceKind));
	Source += FString::Printf(TEXT("#define %s_RESOURCE_LIFETIME %d\n"), *MacroPrefix, static_cast<int32>(ContractRow->ResourceLifetime));
	Source += FString::Printf(TEXT("#define %s_RESOURCE_ELEMENT_STRIDE %d\n"), *MacroPrefix, ContractRow->ElementStrideBytes);
	Source += FString::Printf(TEXT("#define %s_RESOURCE_ELEMENT_STRIDE_FLOAT4 %d\n"), *MacroPrefix, FMath::DivideAndRoundUp(ContractRow->ElementStrideBytes, 16));
	Source += FString::Printf(TEXT("#define %s_RESOURCE_LAYOUT_HASH 0x%08X\n"), *MacroPrefix, static_cast<uint32>(ContractRow->LayoutHash));
	Source += FString::Printf(TEXT("#define %s_RESOURCE_PREFER_UNREAL_RDG %d\n"), *MacroPrefix, ContractRow->bPreferUnrealRdg ? 1 : 0);
	Source += FString::Printf(TEXT("#define %s_RESOURCE_REQUIRES_RAW_VULKAN %d\n"), *MacroPrefix, ContractRow->bRequiresRawVulkanInterop ? 1 : 0);
	Source += FString::Printf(TEXT("#define %s_RESOURCE_ACCESS_RULE_COUNT %d\n"), *MacroPrefix, AccessRows.Num());

	for (int32 Index = 0; Index < AccessRows.Num(); ++Index)
	{
		const FFlightGpuAccessRuleRow& Row = AccessRows[Index];
		Source += FString::Printf(
			TEXT("// AccessRule[%d]: Domain=%s Access=%s\n"),
			Index,
			*Detail::GpuExecutionDomainToString(Row.ExecutionDomain),
			*Detail::GpuAccessClassToString(Row.AccessClass));
	}

	return Source;
}

} // namespace Flight::Reflection::HLSL
