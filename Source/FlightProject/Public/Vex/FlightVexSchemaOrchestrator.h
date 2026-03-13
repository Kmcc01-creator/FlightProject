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

	template<Flight::Reflection::CReflectable T>
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

				FVexSymbolRecord Record;
				if constexpr (DescType::template HasAttrTemplate<Attr::VexSymbol>)
				{
					using AttrType = typename DescType::template GetAttr<Attr::VexSymbol>;
					Record.SymbolName = FString(AttrType::Value.data());
				}
					Record.ValueType = ResolveValueType<FieldType>();
					Record.Storage.Kind = DescType::IsOffsetEligible ? EVexStorageKind::AosOffset : EVexStorageKind::Accessor;
					Record.Storage.MemberOffset = DescType::IsOffsetEligible ? DescType::GetOffset() : INDEX_NONE;
					Record.Storage.ElementStride = sizeof(FieldType);
					Record.ReadValue = [](const void* Ptr) -> FVexRuntimeValue
					{
						const T& Instance = *static_cast<const T*>(Ptr);
						return MakeRuntimeValue<FieldType>(DescType::Get(Instance));
					};
					Record.WriteValue = [](void* Ptr, const FVexRuntimeValue& Value) -> bool
					{
						T& Instance = *static_cast<T*>(Ptr);
						DescType::Set(Instance, ConvertRuntimeValue<FieldType>(Value));
						return true;
					};
					Record.Getter = [](const void* Ptr) -> float
					{
						const T& Instance = *static_cast<const T*>(Ptr);
						return MakeRuntimeValue<FieldType>(DescType::Get(Instance)).AsFloat();
					};
					Record.Setter = [](void* Ptr, float Value)
					{
						T& Instance = *static_cast<T*>(Ptr);
						DescType::Set(Instance, ConvertRuntimeValue<FieldType>(FVexRuntimeValue::FromFloat(Value)));
					};
					Record.bReadable = Record.ReadValue != nullptr;
					Record.bWritable = Record.WriteValue != nullptr;

				if constexpr (DescType::template HasAttrTemplate<Attr::VexResidency>)
				{
					Record.Residency = DescType::template GetAttr<Attr::VexResidency>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::HlslIdentifier>)
				{
					Record.BackendBinding.HlslIdentifier = FString(DescType::template GetAttr<Attr::HlslIdentifier>::Value.data());
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::VerseIdentifier>)
				{
					Record.BackendBinding.VerseIdentifier = FString(DescType::template GetAttr<Attr::VerseIdentifier>::Value.data());
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::ThreadAffinity>)
				{
					Record.Affinity = DescType::template GetAttr<Attr::ThreadAffinity>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::SimdReadAllowed>)
				{
					Record.bSimdReadAllowed = DescType::template GetAttr<Attr::SimdReadAllowed>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::SimdWriteAllowed>)
				{
					Record.bSimdWriteAllowed = DescType::template GetAttr<Attr::SimdWriteAllowed>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::GpuTier1Allowed>)
				{
					Record.bGpuTier1Allowed = DescType::template GetAttr<Attr::GpuTier1Allowed>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::VexAlignment>)
				{
					Record.AlignmentRequirement = DescType::template GetAttr<Attr::VexAlignment>::Value;
				}

				if constexpr (DescType::template HasAttrTemplate<Attr::MathDeterminism>)
				{
					Record.MathDeterminismProfile = DescType::template GetAttr<Attr::MathDeterminism>::Value;
				}

				Record.VectorPack = BuildDefaultVectorPackContract(
					Record.Storage,
					Record.ValueType,
					Record.AlignmentRequirement,
					Record.bSimdReadAllowed,
					Record.bSimdWriteAllowed,
					Record.bGpuTier1Allowed);

				Schema.SymbolRecords.Add(Record.SymbolName, Record);
			});
		}

		Schema.RebuildLegacyViews();
		Schema.LayoutHash = ComputeSchemaLayoutHash(Schema);
		return Schema;
	}

	private:
	template<typename TField>
	static FVexRuntimeValue MakeRuntimeValue(const TField& Value)
	{
		using BaseFieldType = std::remove_cvref_t<TField>;

		if constexpr (std::is_same_v<BaseFieldType, float>) return FVexRuntimeValue::FromFloat(Value);
		if constexpr (std::is_same_v<BaseFieldType, int32>) return FVexRuntimeValue::FromInt(Value);
		if constexpr (std::is_same_v<BaseFieldType, uint32>) return FVexRuntimeValue::FromInt(static_cast<int32>(Value));
		if constexpr (std::is_same_v<BaseFieldType, bool>) return FVexRuntimeValue::FromBool(Value);
		if constexpr (std::is_same_v<BaseFieldType, FVector2f>) return FVexRuntimeValue::FromVec2(Value);
		if constexpr (std::is_same_v<BaseFieldType, FVector3f>) return FVexRuntimeValue::FromVec3(Value);
		if constexpr (std::is_same_v<BaseFieldType, FVector4f>) return FVexRuntimeValue::FromVec4(Value);
		return FVexRuntimeValue();
	}

	template<typename TField>
	static std::remove_cvref_t<TField> ConvertRuntimeValue(const FVexRuntimeValue& Value)
	{
		using BaseFieldType = std::remove_cvref_t<TField>;

		if constexpr (std::is_same_v<BaseFieldType, float>) return Value.AsFloat();
		if constexpr (std::is_same_v<BaseFieldType, int32>) return Value.AsInt();
		if constexpr (std::is_same_v<BaseFieldType, uint32>) return static_cast<uint32>(FMath::Max(0, Value.AsInt()));
		if constexpr (std::is_same_v<BaseFieldType, bool>) return Value.AsBool();
		if constexpr (std::is_same_v<BaseFieldType, FVector2f>) return Value.AsVec2();
		if constexpr (std::is_same_v<BaseFieldType, FVector3f>) return Value.AsVec3();
		if constexpr (std::is_same_v<BaseFieldType, FVector4f>) return Value.AsVec4();
		return BaseFieldType{};
	}

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
