// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Vex/FlightVexTypes.h"
#include "Vex/FlightVexSchema.h"
#include "Core/FlightReflection.h"

namespace Flight::Vex
{

/**
 * FVexSymbolRegistry: Manages the mapping of VEX @symbols to reflected C++ fields.
 * Now formalized via FVexTypeSchema.
 */
class FLIGHTPROJECT_API FVexSymbolRegistry
{
public:
	static FVexSymbolRegistry& Get();

	/** Find or create a schema for a specific type key. */
	FVexTypeSchema& FindOrAddSchema(const void* TypeKey);

	/** Find an existing schema for a specific type key. */
	const FVexTypeSchema* GetSchema(const void* TypeKey) const;

	/** Find an accessor by name for a specific type key. */
	const FVexSymbolAccessor* FindSymbol(const void* TypeKey, const FString& Name) const;

private:
	/** Map: TypeKey -> Formal Schema */
	TMap<const void*, FVexTypeSchema> Schemas;
};

/**
 * TTypeVexRegistry: Template helper to automatically register all VEX-tagged fields
 * for a reflectable C++ type.
 */
template<typename T>
struct TTypeVexRegistry
{
	static const void* GetTypeKey()
	{
		static uint8 TypeKeyMarker = 0;
		return &TypeKeyMarker;
	}

	static void Register()
	{
		using namespace Flight::Reflection;
		
		if constexpr (CHasFields<T>)
		{
			const void* TypeKey = GetTypeKey();
			FVexTypeSchema& Schema = FVexSymbolRegistry::Get().FindOrAddSchema(TypeKey);
			
			// Initialize Basic Schema Metadata
			Schema.TypeName = FString(TReflectTraits<T>::Name);
			Schema.Size = sizeof(T);
			Schema.Alignment = alignof(T);

			TReflectTraits<T>::Fields::ForEachDescriptor([&Schema](auto Descriptor) {
				using DescType = decltype(Descriptor);
				
				if constexpr (DescType::template HasAttrTemplate<Attr::VexSymbol>)
				{
					FVexSymbolAccessor Accessor;
					const auto& SymbolNameAttr = DescType::template GetAttr<Attr::VexSymbol>::Value;
					Accessor.SymbolName = FString(SymbolNameAttr.data());
					
					// Zero-cost direct offset
					Accessor.MemberOffset = DescType::GetOffset();

					// Type-erased Getter
					Accessor.Getter = [](const void* Ptr) -> float {
						const T& Instance = *static_cast<const T*>(Ptr);
						if constexpr (std::is_convertible_v<typename DescType::FieldType, float>)
						{
							return static_cast<float>(DescType::Get(Instance));
						}
						return 0.0f;
					};

					// Type-erased Setter
					Accessor.Setter = [](void* Ptr, float Value) {
						T& Instance = *static_cast<T*>(Ptr);
						if constexpr (std::is_convertible_v<float, typename DescType::FieldType>)
						{
							DescType::Set(Instance, static_cast<typename DescType::FieldType>(Value));
						}
					};

					// Metadata
					if constexpr (DescType::template HasAttrTemplate<Attr::VexResidency>)
					{
						Accessor.Residency = DescType::template GetAttr<Attr::VexResidency>::Value;
					}
					if constexpr (DescType::template HasAttrTemplate<Attr::HlslIdentifier>)
					{
						Accessor.HlslIdentifier = FString(DescType::template GetAttr<Attr::HlslIdentifier>::Value.data());
					}
					if constexpr (DescType::template HasAttrTemplate<Attr::VerseIdentifier>)
					{
						Accessor.VerseIdentifier = FString(DescType::template GetAttr<Attr::VerseIdentifier>::Value.data());
					}

					Schema.Symbols.Add(Accessor.SymbolName, Accessor);
				}
			});
		}
	}
};

} // namespace Flight::Vex
