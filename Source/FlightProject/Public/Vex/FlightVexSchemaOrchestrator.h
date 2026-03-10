// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightHlslReflection.h"
#include "Core/FlightReflection.h"
#include "Schema/FlightRequirementSchema.h"
#include "Vex/FlightVexSchema.h"

namespace Flight::Vex
{

class FLIGHTPROJECT_API FVexSchemaOrchestrator
{
public:
	static FVexTypeId MakeTypeId(const void* RuntimeKey, FName StableName);
	static FVexTypeSchema MergeManifestRequirements(FVexTypeSchema BaseSchema, const TArray<FFlightVexSymbolRow>& Rows);
	static uint32 ComputeSchemaLayoutHash(const FVexTypeSchema& Schema);

	template<typename T>
	static FVexTypeSchema BuildSchemaFromReflection(const void* RuntimeKey)
	{
		using namespace Flight::Reflection;

		FVexTypeSchema Schema;
		Schema.TypeId = MakeTypeId(RuntimeKey, FName(TReflectTraits<T>::Name));
		Schema.TypeName = FString(TReflectTraits<T>::Name);
		Schema.Size = sizeof(T);
		Schema.Alignment = alignof(T);

		if constexpr (requires { T::StaticStruct(); })
		{
			Schema.NativeStruct = T::StaticStruct();
		}

		if constexpr (CHasFields<T>)
		{
			TReflectTraits<T>::Fields::ForEachDescriptor([&Schema](auto Descriptor)
			{
				using DescType = decltype(Descriptor);
				using FieldType = std::remove_cvref_t<typename DescType::FieldType>;

				if constexpr (!DescType::template HasAttrTemplate<Attr::VexSymbol>)
				{
					return;
				}

				FVexSymbolAccessor Accessor;
				FVexLogicalSymbolSchema Logical;
				const auto& SymbolNameAttr = DescType::template GetAttr<Attr::VexSymbol>::Value;

				Accessor.SymbolName = FString(SymbolNameAttr.data());
				Accessor.ValueType = ResolveValueType<FieldType>();
				Accessor.MemberOffset = DescType::IsOffsetEligible ? DescType::GetOffset() : INDEX_NONE;
				Accessor.Getter = [](const void* Ptr) -> float
				{
					const T& Instance = *static_cast<const T*>(Ptr);
					if constexpr (std::is_convertible_v<FieldType, float>)
					{
						return static_cast<float>(DescType::Get(Instance));
					}
					return 0.0f;
				};
				Accessor.Setter = [](void* Ptr, float Value)
				{
					T& Instance = *static_cast<T*>(Ptr);
					if constexpr (std::is_convertible_v<float, FieldType>)
					{
						DescType::Set(Instance, static_cast<FieldType>(Value));
					}
				};

				Logical.SymbolName = Accessor.SymbolName;
				Logical.ValueType = Accessor.ValueType;
				Logical.bReadable = Accessor.Getter != nullptr;
				Logical.bWritable = Accessor.Setter != nullptr;
				Logical.Storage.Kind = DescType::IsOffsetEligible ? EVexStorageKind::AosOffset : EVexStorageKind::Accessor;
				Logical.Storage.MemberOffset = Accessor.MemberOffset;
				Logical.Storage.ElementStride = sizeof(FieldType);

				if constexpr (DescType::template HasAttrTemplate<Attr::VexResidency>)
				{
					Accessor.Residency = DescType::template GetAttr<Attr::VexResidency>::Value;
					Logical.Residency = Accessor.Residency;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::HlslIdentifier>)
				{
					Accessor.HlslIdentifier = FString(DescType::template GetAttr<Attr::HlslIdentifier>::Value.data());
					Logical.BackendBinding.HlslIdentifier = Accessor.HlslIdentifier;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::VerseIdentifier>)
				{
					Accessor.VerseIdentifier = FString(DescType::template GetAttr<Attr::VerseIdentifier>::Value.data());
					Logical.BackendBinding.VerseIdentifier = Accessor.VerseIdentifier;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::ThreadAffinity>)
				{
					Logical.Affinity = DescType::template GetAttr<Attr::ThreadAffinity>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::SimdReadAllowed>)
				{
					Logical.bSimdReadAllowed = DescType::template GetAttr<Attr::SimdReadAllowed>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::SimdWriteAllowed>)
				{
					Logical.bSimdWriteAllowed = DescType::template GetAttr<Attr::SimdWriteAllowed>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::GpuTier1Allowed>)
				{
					Logical.bGpuTier1Allowed = DescType::template GetAttr<Attr::GpuTier1Allowed>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::VexAlignment>)
				{
					Logical.AlignmentRequirement = DescType::template GetAttr<Attr::VexAlignment>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::MathDeterminism>)
				{
					Logical.MathDeterminismProfile = DescType::template GetAttr<Attr::MathDeterminism>::Value;
				}

				Schema.Symbols.Add(Accessor.SymbolName, Accessor);
				Schema.LogicalSymbols.Add(Logical.SymbolName, Logical);
			});
		}

		Schema.LayoutHash = ComputeSchemaLayoutHash(Schema);
		return Schema;
	}

private:
	template<typename TField>
	static EVexValueType ResolveValueType()
	{
		using BaseFieldType = std::remove_cvref_t<TField>;

		if constexpr (std::is_same_v<BaseFieldType, float>) return EVexValueType::Float;
		if constexpr (std::is_same_v<BaseFieldType, FVector2f>) return EVexValueType::Float2;
		if constexpr (std::is_same_v<BaseFieldType, FVector3f>) return EVexValueType::Float3;
		if constexpr (std::is_same_v<BaseFieldType, FVector4f>) return EVexValueType::Float4;
		if constexpr (std::is_same_v<BaseFieldType, int32>) return EVexValueType::Int;
		if constexpr (std::is_same_v<BaseFieldType, uint32>) return EVexValueType::Int;
		if constexpr (std::is_same_v<BaseFieldType, bool>) return EVexValueType::Bool;
		return EVexValueType::Unknown;
	}
};

} // namespace Flight::Vex
