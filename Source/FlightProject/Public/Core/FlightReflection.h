// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightReflection - Trait-based reflection system prototype
//
// What if UE reflection was designed with modern C++ from the start?
// No USTRUCT() macros, no generated code, no UHT.
// Just traits, concepts, and zero-cost abstractions.
//
// This is a design exploration - the reflection system we wish we had.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightFunctional.h"
#include <type_traits>
#include <tuple>
#include <utility>
#include <concepts>
#include <string_view>

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Vex/FlightVexTypes.h"

namespace Flight::Reflection
{

using namespace Flight::Functional;

// ============================================================================
// Compile-Time String - For static field names
// ============================================================================

template<size_t N>
struct TStaticString
{
	char Data[N]{};

	constexpr TStaticString(const char (&Str)[N])
	{
		for (size_t i = 0; i < N; ++i) Data[i] = Str[i];
	}

	constexpr operator std::string_view() const { return {Data, N - 1}; }
	constexpr const char* CStr() const { return Data; }
	static constexpr size_t Size = N - 1;
};

// Deduction guide
template<size_t N>
TStaticString(const char (&)[N]) -> TStaticString<N>;


// ============================================================================
// Core Concepts - What makes a type reflectable?
// ============================================================================

/**
 * TReflectBase - The canonical base type for reflection lookup (removes const/volatile/reference).
 */
template<typename T>
using TReflectBase = std::remove_cvref_t<T>;

// A type is reflectable if it opts in via a trait specialization
template<typename T>
struct TReflectionTraits
{
	static constexpr bool IsReflectable = false;
};

/**
 * TReflectTraits - Primary alias for trait lookup that ensures base-type normalization.
 */
template<typename T>
using TReflectTraits = TReflectionTraits<TReflectBase<T>>;

// Concept: type has reflection traits
template<typename T>
concept CReflectable = TReflectTraits<T>::IsReflectable;

// Concept: type has fields we can iterate
template<typename T>
concept CHasFields = CReflectable<T> && requires {
	typename TReflectTraits<T>::Fields;
};

// Concept: type can be serialized (requires non-const T for the Ar mutation)
template<typename T>
concept CSerializable = CReflectable<T> && requires(T& t, FArchive& Ar) {
	{ TReflectionTraits<TReflectBase<T>>::Serialize(t, Ar) };
};


// Forward declarations for policy checks used before full attribute definitions.
namespace Attr
{
	struct Transient;
	struct DuplicateTransient;
	struct SkipSerialization;
}


// ============================================================================
// Field Descriptor - Compile-time field metadata (TYPE, not instance)
// ============================================================================
//
// Each descriptor is a TYPE with static members. No instance needed.
// This fixes the issue where macro-generated values couldn't be template args.

template<typename TOwner, typename TField, auto MemberPtr, TStaticString FieldName>
struct TFieldDescriptor
{
	using OwnerType = TOwner;
	using FieldType = TField;
	static constexpr auto Pointer = MemberPtr;

	// Name is now static constexpr - no instance needed!
	static constexpr std::string_view Name{FieldName};
	static constexpr const char* NameCStr = FieldName.CStr();

	// Access the field on an instance
	static TField& Get(TOwner& Owner)
	{
		return Owner.*MemberPtr;
	}

	static const TField& Get(const TOwner& Owner)
	{
		return Owner.*MemberPtr;
	}

	// Set with perfect forwarding - works with non-copyable types
	template<typename U>
		requires std::is_assignable_v<TField&, U&&>
	static void Set(TOwner& Owner, U&& Value)
	{
		Owner.*MemberPtr = Forward<U>(Value);
	}
};


// ============================================================================
// Field List - Compile-time list of field TYPES
// ============================================================================

template<typename... Fields>
struct TFieldList
{
	static constexpr size_t Count = sizeof...(Fields);

	using FieldTuple = std::tuple<Fields...>;

	// Apply a function to each field descriptor TYPE (no instantiation needed)
	template<typename F>
	static void ForEach(F&& Func)
	{
		// Pass type tag, not instance - Func receives the type info
		(Func.template operator()<Fields>(), ...);
	}

	// Alternative: pass a default-constructed descriptor if Func expects an instance
	// This works because all data is static constexpr now
	template<typename F>
	static void ForEachDescriptor(F&& Func)
	{
		(Func(Fields{}), ...);
	}

	// Apply a function to each field value on an instance
	template<typename TOwner, typename F>
	static void ForEachValue(TOwner& Owner, F&& Func)
	{
		// Func receives: (field_value_ref, FieldDescriptor type tag)
		(Func(Fields::Get(Owner), Fields{}), ...);
	}

	template<typename TOwner, typename F>
	static void ForEachValue(const TOwner& Owner, F&& Func)
	{
		(Func(Fields::Get(Owner), Fields{}), ...);
	}

	// Fold over fields
	template<typename TAccum, typename F>
	static TAccum Fold(TAccum Initial, F&& Func)
	{
		TAccum Accum = MoveTemp(Initial);
		((Accum = Func(MoveTemp(Accum), Fields{})), ...);
		return Accum;
	}

	// Get field type by index
	template<size_t I>
	using At = std::tuple_element_t<I, FieldTuple>;
};


// ============================================================================
// Reflection Macros - Two-part pattern (user controls struct brace)
// ============================================================================
//
// Usage:
//   struct FMyStruct {
//       float Value;
//       FVector Position;
//
//       FLIGHT_REFLECT_BODY(FMyStruct)
//   };
//
//   FLIGHT_REFLECT_FIELDS(FMyStruct,
//       FLIGHT_FIELD(float, Value),
//       FLIGHT_FIELD(FVector, Position)
//   )
//
// The struct's closing brace remains under user control!

// Declares the Self alias and friend needed inside the struct
#define FLIGHT_REFLECT_BODY(TypeName) \
	using FlightReflectSelf = TypeName; \
	friend struct ::Flight::Reflection::TReflectionTraits<TypeName>

// Helper to create a field descriptor TYPE (not an instance!)
// This becomes a type alias that TFieldList can use
#define FLIGHT_FIELD(Type, Name) \
	::Flight::Reflection::TFieldDescriptor< \
		FlightReflectSelf, \
		Type, \
		&FlightReflectSelf::Name, \
		::Flight::Reflection::TStaticString(#Name) \
	>

// Specializes the traits OUTSIDE the struct
#define FLIGHT_REFLECT_FIELDS(TypeName, ...) \
	template<> \
	struct ::Flight::Reflection::TReflectionTraits<TypeName> \
	{ \
		using FlightReflectSelf = TypeName; \
		static constexpr bool IsReflectable = true; \
		using Type = TypeName; \
		using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
		static constexpr const char* Name = #TypeName; \
	};


// ============================================================================
// Type Registry - Runtime type information (opt-in)
// ============================================================================

class FTypeRegistry
{
public:
	struct FTypeInfo
	{
		FName TypeName;
		size_t Size;
		size_t Alignment;
		TArray<FName> FieldNames;
		TFunction<void(void*, FArchive&)> SerializeFn;
	};

	static FTypeRegistry& Get()
	{
		static FTypeRegistry Instance;
		return Instance;
	}

	template<CReflectable T>
	void Register()
	{
		FTypeInfo Info;
		Info.TypeName = TReflectTraits<T>::Name;
		Info.Size = sizeof(T);
		Info.Alignment = alignof(T);

		if constexpr (CHasFields<T>)
		{
			TReflectTraits<T>::Fields::ForEachDescriptor([&Info](auto Descriptor) {
				using DescType = decltype(Descriptor);
				Info.FieldNames.Add(FName(DescType::NameCStr));
			});
		}

		Types.Add(Info.TypeName, MoveTemp(Info));
	}

	const FTypeInfo* Find(FName TypeName) const
	{
		return Types.Find(TypeName);
	}

private:
	TMap<FName, FTypeInfo> Types;
};


// ============================================================================
// Serialization via Traits
// ============================================================================

// Check if a type is POD-like (trivially copyable)
template<typename T>
concept CPod = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

// Check if a type has UE archive operator<<
template<typename T>
concept CHasArchiveOperator = requires(T& t, FArchive& Ar) {
	{ Ar << t } -> std::same_as<FArchive&>;
};

namespace Policy
{
	template<typename Descriptor, typename Attribute>
	inline constexpr bool HasAttr = requires {
		{ Descriptor::template HasAttr<Attribute> } -> std::convertible_to<bool>;
	} && Descriptor::template HasAttr<Attribute>;

	template<typename Descriptor>
	inline constexpr bool IsSerializableField =
		!HasAttr<Descriptor, Attr::Transient> &&
		!HasAttr<Descriptor, Attr::DuplicateTransient> &&
		!HasAttr<Descriptor, Attr::SkipSerialization>;
}

template<CReflectable T>
void Serialize(T& Value, FArchive& Ar)
{
	if constexpr (CHasFields<T>)
	{
		TReflectTraits<T>::Fields::ForEachValue(Value, [&Ar](auto& FieldValue, auto Descriptor) {
			using DescType = decltype(Descriptor);
			using FieldType = std::decay_t<decltype(FieldValue)>;

			if constexpr (Policy::IsSerializableField<DescType>)
			{
				// Recursively serialize nested reflectable types
				if constexpr (CReflectable<FieldType>)
				{
					Serialize(FieldValue, Ar);
				}
				// Use archive operator if available
				else if constexpr (CHasArchiveOperator<FieldType>)
				{
					Ar << FieldValue;
				}
				// POD types: raw memory copy
				else if constexpr (CPod<FieldType>)
				{
					Ar.Serialize(&FieldValue, sizeof(FieldType));
				}
			}
			// else: skip unserializable fields (could add warning)
		});
	}
}


// ============================================================================
// Pattern Matching on Types
// ============================================================================

template<typename TOutput, typename TValue>
class TTypeMatcher
{
public:
	explicit TTypeMatcher(TValue& InValue) : Value(InValue) {}

	template<typename T, typename THandler>
	TTypeMatcher& Case(THandler&& Handler)
	{
		if (!Result.IsSet())
		{
			if constexpr (std::is_same_v<std::decay_t<TValue>, T>)
			{
				Result = Handler(static_cast<T&>(Value));
			}
		}
		return *this;
	}

	template<typename THandler>
	TOutput Default(THandler&& Handler)
	{
		if (Result.IsSet())
		{
			return MoveTemp(Result.GetValue());
		}
		return Handler(Value);
	}

private:
	TValue& Value;
	TOptional<TOutput> Result;
};


// ============================================================================
// Compile-Time Type Composition
// ============================================================================

template<typename... Components>
struct TTypeComposition;

template<typename TTuple>
struct TTupleToTypeComposition;

template<typename... Ts>
struct TTupleToTypeComposition<std::tuple<Ts...>>
{
	using Type = TTypeComposition<Ts...>;
};

template<typename... Components>
struct TTypeComposition
{
	static constexpr size_t Count = sizeof...(Components);

	template<typename T>
	static constexpr bool Has = (std::is_same_v<T, Components> || ...);

	template<typename... Ts>
	// Universal quantification: true when every Ts is present.
	// Note: HasAll<> is true (vacuous truth).
	static constexpr bool HasAll = (Has<Ts> && ...);

	template<typename... Others>
	struct Difference
	{
		template<typename T>
		static constexpr bool InOthers = (std::is_same_v<T, Others> || ...);

		template<typename T>
		using MaybeKeep = std::conditional_t<InOthers<T>, std::tuple<>, std::tuple<T>>;

		using Type = typename TTupleToTypeComposition<
			decltype(std::tuple_cat(std::declval<MaybeKeep<Components>>()...))
		>::Type;
	};

	template<typename... Others>
	using Union = TTypeComposition<Components..., Others...>;

	template<typename... Others>
	struct Intersection
	{
		template<typename T>
		static constexpr bool InOthers = (std::is_same_v<T, Others> || ...);

		template<typename T>
		using MaybeKeep = std::conditional_t<InOthers<T>, std::tuple<T>, std::tuple<>>;

		using Type = typename TTupleToTypeComposition<
			decltype(std::tuple_cat(std::declval<MaybeKeep<Components>>()...))
		>::Type;
	};

	template<typename F>
	static void ForEachType(F&& Func)
	{
		(Func.template operator()<Components>(), ...);
	}
};


// ============================================================================
// Validated Access Pattern (PhantomState integration)
// ============================================================================

template<typename TEntity, typename... AcquiredPermissions>
class TAccessContext
{
public:
	explicit TAccessContext(TEntity& InEntity) : Entity(InEntity) {}

	template<typename Permission, typename TAcquirer>
	auto Acquire(TAcquirer&& Acquirer) &&
	{
		[[maybe_unused]] auto _ = Acquirer(Entity);
		return TAccessContext<TEntity, AcquiredPermissions..., Permission>(Entity);
	}

	template<typename... Required>
	auto Require() &&
	{
		static_assert(
			((Detail::HasTag<Required, AcquiredPermissions...> ) && ...),
			"Missing required permission"
		);
		return MoveTemp(*this);
	}

	TEntity& Get() { return Entity; }

private:
	TEntity& Entity;
};

template<typename TEntity>
TAccessContext<TEntity> AccessContext(TEntity& Entity)
{
	return TAccessContext<TEntity>(Entity);
}


// ============================================================================
// Tree Trait for Reflectable Types
// ============================================================================

template<CReflectable T>
TArray<void*> GetReflectedChildren(T& Value)
{
	TArray<void*> Children;

	if constexpr (CHasFields<T>)
	{
		TReflectTraits<T>::Fields::ForEachValue(Value, [&Children](auto& FieldValue, auto) {
			using FieldType = std::decay_t<decltype(FieldValue)>;
			if constexpr (CReflectable<FieldType>)
			{
				Children.Add(&FieldValue);
			}
		});
	}

	return Children;
}

template<CReflectable T, typename TVisitor>
void ForEachField(T& Value, TVisitor&& Visitor)
{
	if constexpr (CHasFields<T>)
	{
		TReflectTraits<T>::Fields::ForEachValue(Value, [&Visitor](auto& FieldValue, auto Descriptor) {
			using DescType = decltype(Descriptor);
			Visitor(FieldValue, DescType::NameCStr);
		});
	}
}

template<CReflectable T, typename TVisitor>
void ForEachFieldDeep(T& Value, TVisitor&& Visitor)
{
	ForEachField(Value, [&Visitor](auto& FieldValue, const char* Name) {
		Visitor(FieldValue, Name);

		using FieldType = std::decay_t<decltype(FieldValue)>;
		if constexpr (CReflectable<FieldType>)
		{
			ForEachFieldDeep(FieldValue, Visitor);
		}
	});
}


// ============================================================================
// Attribute System - Compile-time field metadata
// ============================================================================

namespace Attr
{

// Access specifiers (editor visibility)
struct EditAnywhere {};
struct EditDefaultsOnly {};
struct EditInstanceOnly {};
struct VisibleAnywhere {};
struct VisibleDefaultsOnly {};
struct VisibleInstanceOnly {};

// Blueprint access
struct BlueprintReadOnly {};
struct BlueprintReadWrite {};
struct BlueprintCallable {};
struct BlueprintAssignable {};

// Serialization
struct Transient {};
struct DuplicateTransient {};
struct SaveGame {};
struct SkipSerialization {};

// Replication
struct Replicated {};
struct ReplicatedUsing {};
struct NotReplicated {};

// Lifetime
struct Instanced {};
struct Export {};
struct NoClear {};

// UI hints
struct SimpleDisplay {};
struct AdvancedDisplay {};

// Numeric constraints (parameterized)
template<int32 Min, int32 Max>
struct ClampedValue {};

template<int32 Min>
struct UIMin {};

template<int32 Max>
struct UIMax {};

// Category (parameterized with compile-time string)
template<TStaticString Str>
struct Category
{
	static constexpr std::string_view Value{Str};
};

template<TStaticString Str>
struct DisplayName
{
	static constexpr std::string_view Value{Str};
};

template<TStaticString Str>
struct ToolTip
{
	static constexpr std::string_view Value{Str};
};

// VEX/Scripting integration attributes
template<TStaticString Str>
struct VexSymbol
{
	static constexpr std::string_view Value{Str};
};

template<TStaticString Str>
struct HlslIdentifier
{
	static constexpr std::string_view Value{Str};
};

template<TStaticString Str>
struct VerseIdentifier
{
	static constexpr std::string_view Value{Str};
};

template<TStaticString Str>
struct VersePackage
{
	static constexpr std::string_view Value{Str};
};

template<EFlightVexSymbolResidency Residency>
struct VexResidency
{
	static constexpr EFlightVexSymbolResidency Value = Residency;
};

template<EFlightVexSymbolAffinity Affinity>
struct ThreadAffinity
{
	static constexpr EFlightVexSymbolAffinity Value = Affinity;
};

// SIMD/GPU Capability Attributes
template<bool bAllowed>
struct SimdReadAllowed { static constexpr bool Value = bAllowed; };

template<bool bAllowed>
struct SimdWriteAllowed { static constexpr bool Value = bAllowed; };

template<bool bAllowed>
struct GpuTier1Allowed { static constexpr bool Value = bAllowed; };

template<EFlightVexAlignmentRequirement Alignment>
struct VexAlignment { static constexpr EFlightVexAlignmentRequirement Value = Alignment; };

template<EFlightVexMathDeterminismProfile Profile>
struct MathDeterminism { static constexpr EFlightVexMathDeterminismProfile Value = Profile; };

// Meta specifiers
struct AllowPrivateAccess {};
struct ExposeOnSpawn {};
struct Interp {};

} // namespace Attr


// ============================================================================
// Template Matching Utilities - For parameterized attributes
// ============================================================================

// Check if a type is a specialization of a template
template<typename T, template<auto...> class Template>
struct TIsSpecializationOfNTTP : std::false_type {};

template<template<auto...> class Template, auto... Args>
struct TIsSpecializationOfNTTP<Template<Args...>, Template> : std::true_type {};

template<typename T, template<typename...> class Template>
struct TIsSpecializationOf : std::false_type {};

template<template<typename...> class Template, typename... Args>
struct TIsSpecializationOf<Template<Args...>, Template> : std::true_type {};

// Helper variable templates
template<typename T, template<auto...> class Template>
inline constexpr bool IsSpecializationOfNTTP = TIsSpecializationOfNTTP<T, Template>::value;

template<typename T, template<typename...> class Template>
inline constexpr bool IsSpecializationOf = TIsSpecializationOf<T, Template>::value;

namespace Detail
{
	// Helper to find a specialization of a template in a parameter pack
	template<template<auto...> class Template, typename... Ts>
	struct TGetTemplate;

	template<template<auto...> class Template>
	struct TGetTemplate<Template>
	{
		using Type = void;
	};

	template<template<auto...> class Template, typename Head, typename... Tail>
	struct TGetTemplate<Template, Head, Tail...>
	{
		using Type = std::conditional_t<
			IsSpecializationOfNTTP<Head, Template>,
			Head,
			typename TGetTemplate<Template, Tail...>::Type
		>;
	};

	template<template<typename...> class Template, typename... Ts>
	struct TGetTypeTemplate;

	template<template<typename...> class Template>
	struct TGetTypeTemplate<Template>
	{
		using Type = void;
	};

	template<template<typename...> class Template, typename Head, typename... Tail>
	struct TGetTypeTemplate<Template, Head, Tail...>
	{
		using Type = std::conditional_t<
			IsSpecializationOf<Head, Template>,
			Head,
			typename TGetTypeTemplate<Template, Tail...>::Type
		>;
	};

	// Required for TAccessContext
	template<typename Tag, typename... Tags>
	inline constexpr bool HasTag = (std::is_same_v<Tag, Tags> || ...);
}


// ============================================================================
// Attribute Queries - Check if field has specific attribute
// ============================================================================

template<typename... Attrs>
struct TAttributeSet
{
	static constexpr size_t Count = sizeof...(Attrs);

	// Exact match for simple attributes
	template<typename A>
	static constexpr bool Has = (std::is_same_v<A, Attrs> || ...);

	// Partial match for parameterized attributes (e.g., Category<"Foo">)
	template<template<auto...> class Template>
	static constexpr bool HasTemplate = (IsSpecializationOfNTTP<Attrs, Template> || ...);

	template<template<typename...> class Template>
	static constexpr bool HasTypeTemplate = (IsSpecializationOf<Attrs, Template> || ...);

	// Check for any attribute in a category
	static constexpr bool HasEditAccess =
		Has<Attr::EditAnywhere> ||
		Has<Attr::EditDefaultsOnly> ||
		Has<Attr::EditInstanceOnly>;

	static constexpr bool HasBlueprintAccess =
		Has<Attr::BlueprintReadOnly> ||
		Has<Attr::BlueprintReadWrite>;

	static constexpr bool IsTransient =
		Has<Attr::Transient> ||
		Has<Attr::DuplicateTransient>;

	static constexpr bool IsReplicated =
		Has<Attr::Replicated> ||
		Has<Attr::ReplicatedUsing>;

	static constexpr bool IsSaveGame = Has<Attr::SaveGame>;

	// Check for parameterized attributes
	static constexpr bool HasCategory = HasTemplate<Attr::Category>;
	static constexpr bool HasClampedValue = HasTemplate<Attr::ClampedValue>;

	// Get specific attribute type by template
	template<template<auto...> class Template>
	using GetTemplate = typename Detail::TGetTemplate<Template, Attrs...>::Type;

	template<template<typename...> class Template>
	using GetTypeTemplate = typename Detail::TGetTypeTemplate<Template, Attrs...>::Type;
};


// ============================================================================
// Attributed Field Descriptor
// ============================================================================

template<typename TOwner, typename TField, auto MemberPtr, TStaticString FieldName, typename TAttrs>
struct TAttributedFieldDescriptor
{
	using OwnerType = TOwner;
	using FieldType = TField;
	using Attributes = TAttrs;
	static constexpr auto Pointer = MemberPtr;

	// Static constexpr name - no instance data!
	static constexpr std::string_view Name{FieldName};
	static constexpr const char* NameCStr = FieldName.CStr();

	static TField& Get(TOwner& Owner) { return Owner.*MemberPtr; }
	static const TField& Get(const TOwner& Owner) { return Owner.*MemberPtr; }

	// Perfect forwarding for non-copyable types
	template<typename U>
		requires std::is_assignable_v<TField&, U&&>
	static void Set(TOwner& Owner, U&& Value)
	{
		Owner.*MemberPtr = Forward<U>(Value);
	}

	// Compile-time attribute queries
	template<typename A>
	static constexpr bool HasAttr = Attributes::template Has<A>;

	template<template<auto...> class Template>
	static constexpr bool HasAttrTemplate = Attributes::template HasTemplate<Template>;

	template<template<typename...> class Template>
	static constexpr bool HasAttrTypeTemplate = Attributes::template HasTypeTemplate<Template>;

	// Get specific attribute type
	template<template<auto...> class Template>
	using GetAttr = typename Attributes::template GetTemplate<Template>;

	template<template<typename...> class Template>
	using GetAttrType = typename Attributes::template GetTypeTemplate<Template>;

	static constexpr bool IsEditable = Attributes::HasEditAccess;
	static constexpr bool IsBlueprintAccessible = Attributes::HasBlueprintAccess;
	static constexpr bool IsTransient = Attributes::IsTransient;
	static constexpr bool IsReplicated = Attributes::IsReplicated;
	static constexpr bool IsSaveGame = Attributes::IsSaveGame;
	static constexpr bool HasCategory = Attributes::HasCategory;
};

// Attributed field macro - produces a TYPE, not a value
#define FLIGHT_FIELD_ATTR(Type, Name, ...) \
	::Flight::Reflection::TAttributedFieldDescriptor< \
		FlightReflectSelf, \
		Type, \
		&FlightReflectSelf::Name, \
		::Flight::Reflection::TStaticString(#Name), \
		::Flight::Reflection::TAttributeSet<__VA_ARGS__> \
	>

// Full reflection with attributes (outside struct)
#define FLIGHT_REFLECT_FIELDS_ATTR(TypeName, ...) \
	template<> \
	struct ::Flight::Reflection::TReflectionTraits<TypeName> \
	{ \
		using FlightReflectSelf = TypeName; \
		static constexpr bool IsReflectable = true; \
		using Type = TypeName; \
		using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
		static constexpr const char* Name = #TypeName; \
	};

// Reflection with struct-level attributes (e.g. for VersePackage)
#define FLIGHT_REFLECT_FIELDS_VEX(TypeName, AttrSet, ...) \
	template<> \
	struct ::Flight::Reflection::TReflectionTraits<TypeName> \
	{ \
		using FlightReflectSelf = TypeName; \
		static constexpr bool IsReflectable = true; \
		using Type = TypeName; \
		using Attributes = AttrSet; \
		using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
		static constexpr const char* Name = #TypeName; \
	};


// ============================================================================
// Property Change Notifications
// ============================================================================

template<typename T>
struct TChangeNotificationTraits
{
	static constexpr bool HasNotifications = false;
};

template<typename T>
concept CHasChangeNotifications = TChangeNotificationTraits<T>::HasNotifications;

template<typename T, typename TField>
void SetFieldNotify(T& Object, TField T::* MemberPtr, TField&& NewValue)
{
	TField OldValue = Object.*MemberPtr;
	Object.*MemberPtr = Forward<TField>(NewValue);

	if constexpr (CHasChangeNotifications<T>)
	{
		TChangeNotificationTraits<T>::OnFieldChanged(Object, MemberPtr, OldValue);
	}
}

template<typename TOwner, typename TField>
using TFieldChangeCallback = void(*)(TOwner&, const TField& /*OldValue*/, const TField& /*NewValue*/);


// ============================================================================
// Observable Field - Lightweight change tracking
// ============================================================================
//
// Uses function pointer instead of TFunction to avoid heap allocation.
// For ECS hot paths where thousands of instances exist.

template<typename T>
class TObservableField
{
public:
	// Function pointer - no heap allocation!
	using ChangeCallback = void(*)(const T& /*Old*/, const T& /*New*/, void* /*UserData*/);

	TObservableField() = default;
	explicit TObservableField(T InValue) : Value(MoveTemp(InValue)) {}

	// Assignment triggers notification
	TObservableField& operator=(T NewValue)
	{
		if (Callback && !(Value == NewValue))
		{
			T OldValue = MoveTemp(Value);
			Value = MoveTemp(NewValue);
			Callback(OldValue, Value, UserData);
		}
		else
		{
			Value = MoveTemp(NewValue);
		}
		return *this;
	}

	// Implicit conversion to underlying type
	operator const T&() const { return Value; }
	const T& Get() const { return Value; }
	T& GetMutable() { return Value; }  // Bypasses notification!

	// Bind change callback with optional user data
	void Bind(ChangeCallback InCallback, void* InUserData = nullptr)
	{
		Callback = InCallback;
		UserData = InUserData;
	}

	void Unbind()
	{
		Callback = nullptr;
		UserData = nullptr;
	}

	// Modify in-place with notification
	template<typename F>
	void Modify(F&& Modifier)
	{
		T OldValue = Value;
		Modifier(Value);
		if (Callback && !(OldValue == Value))
		{
			Callback(OldValue, Value, UserData);
		}
	}

private:
	T Value{};
	ChangeCallback Callback = nullptr;
	void* UserData = nullptr;
};


// ============================================================================
// Diff/Patch - Structural comparison and delta application
// ============================================================================

template<typename T>
struct TFieldDelta
{
	std::string_view FieldName;
	T OldValue;
	T NewValue;
};

// Type-erased field storage for patches
struct FFieldPatchData
{
	std::string_view FieldName;
	TArray<uint8> SerializedValue;  // For serializable types
	TFunction<void(void*)> Applier; // For non-serializable types
};

template<CReflectable T>
struct TStructPatch
{
	TArray<FFieldPatchData> ChangedFields;
};

// Generate patch from two instances
template<CReflectable T>
TStructPatch<T> Diff(const T& Old, const T& New)
{
	TStructPatch<T> Patch;

	if constexpr (CHasFields<T>)
	{
		TReflectTraits<T>::Fields::ForEachValue(Old, [&](const auto& OldField, auto Descriptor) {
			using DescType = decltype(Descriptor);
			using FieldType = std::decay_t<decltype(OldField)>;
			const auto& NewField = DescType::Get(New);

			if constexpr (Policy::IsSerializableField<DescType>)
			{
				// Check if field changed (requires operator==)
				if constexpr (requires { OldField == NewField; })
				{
					if (!(OldField == NewField))
					{
						FFieldPatchData PatchData;
						PatchData.FieldName = DescType::Name;

						// Nested reflectable types serialize recursively so field policies still apply.
						if constexpr (CReflectable<FieldType>)
						{
							FMemoryWriter Writer(PatchData.SerializedValue);
							FieldType Copy = NewField;
							Serialize(Copy, Writer);
						}
						// POD types: direct memory copy
						else if constexpr (CPod<FieldType>)
						{
							PatchData.SerializedValue.SetNumUninitialized(sizeof(FieldType));
							FMemory::Memcpy(PatchData.SerializedValue.GetData(), &NewField, sizeof(FieldType));
						}
						// Types with archive operator
						else if constexpr (CHasArchiveOperator<FieldType>)
						{
							FMemoryWriter Writer(PatchData.SerializedValue);
							FieldType Copy = NewField;
							Writer << Copy;
						}
						// Non-serializable: use type-erased applier
						else
						{
							// Capture the new value for later application
							PatchData.Applier = [NewField](void* Target) {
								*static_cast<FieldType*>(Target) = NewField;
							};
						}

						Patch.ChangedFields.Add(MoveTemp(PatchData));
					}
				}
			}
		});
	}

	return Patch;
}

// Apply patch to instance
template<CReflectable T>
void Apply(T& Target, const TStructPatch<T>& Patch)
{
	if constexpr (CHasFields<T>)
	{
		for (const FFieldPatchData& PatchData : Patch.ChangedFields)
		{
			TReflectTraits<T>::Fields::ForEachDescriptor([&](auto Descriptor) {
				using DescType = decltype(Descriptor);
				using FieldType = typename DescType::FieldType;

				if constexpr (Policy::IsSerializableField<DescType>)
				{
					if (DescType::Name == PatchData.FieldName)
					{
						// Use applier if available (non-serializable types)
						if (PatchData.Applier)
						{
							PatchData.Applier(&DescType::Get(Target));
						}
						// Nested reflectable types deserialize recursively so field policies still apply.
						else if constexpr (CReflectable<FieldType>)
						{
							FMemoryReader Reader(PatchData.SerializedValue);
							Serialize(DescType::Get(Target), Reader);
						}
						// POD types: direct memory copy
						else if constexpr (CPod<FieldType>)
						{
							if (PatchData.SerializedValue.Num() == sizeof(FieldType))
							{
								FMemory::Memcpy(&DescType::Get(Target), PatchData.SerializedValue.GetData(), sizeof(FieldType));
							}
						}
						// Types with archive operator
						else if constexpr (CHasArchiveOperator<FieldType>)
						{
							FMemoryReader Reader(PatchData.SerializedValue);
							FieldType Value;
							Reader << Value;
							DescType::Set(Target, MoveTemp(Value));
						}
					}
				}
			});
		}
	}
}


// ============================================================================
// Network Replication via Traits
// ============================================================================

template<typename T>
struct TReplicationTraits
{
	static constexpr bool IsReplicated = false;
};

template<typename T>
concept CReplicated = TReplicationTraits<T>::IsReplicated;

// Replicate only changed fields (delta compression)
// Uses TBitArray instead of uint32 to support > 32 fields
template<CReflectable T>
void ReplicateFields(T& Value, FArchive& Ar, TBitArray<>& DirtyFlags)
{
	if constexpr (CHasFields<T>)
	{
		int32 FieldIndex = 0;
		TReflectTraits<T>::Fields::ForEachValue(Value, [&](auto& FieldValue, auto Descriptor) {
			using DescType = decltype(Descriptor);

			// Only replicate fields marked as Replicated
			if constexpr (
				requires { { DescType::IsReplicated } -> std::convertible_to<bool>; } &&
				DescType::IsReplicated &&
				Policy::IsSerializableField<DescType>)
			{
				// Ensure DirtyFlags is large enough
				if (DirtyFlags.Num() <= FieldIndex)
				{
					DirtyFlags.SetNum(FieldIndex + 1, false);
				}

				if (DirtyFlags[FieldIndex])
				{
					using FieldType = std::decay_t<decltype(FieldValue)>;

					if constexpr (CHasArchiveOperator<FieldType>)
					{
						Ar << FieldValue;
					}
					else if constexpr (CPod<FieldType>)
					{
						Ar.Serialize(&FieldValue, sizeof(FieldType));
					}
				}
			}
			++FieldIndex;
		});
	}
}


// ============================================================================
// Compile-Time Field Filtering
// ============================================================================

template<typename TFields, template<typename> class Predicate>
struct TFilteredFieldList;

template<typename... Fields, template<typename> class Predicate>
struct TFilteredFieldList<TFieldList<Fields...>, Predicate>
{
	template<typename F>
	static void ForEach(F&& Func)
	{
		(ConditionalApply<Fields>(Forward<F>(Func)), ...);
	}

	// Count matching fields at compile time
	static constexpr size_t Count = ((Predicate<Fields>::value ? 1 : 0) + ...);

private:
	template<typename Field, typename F>
	static void ConditionalApply(F&& Func)
	{
		if constexpr (Predicate<Field>::value)
		{
			Func(Field{});
		}
	}
};

// Predicate: field is editable
template<typename Field>
struct TIsEditable
{
	static constexpr bool value = requires {
		{ Field::IsEditable } -> std::convertible_to<bool>;
	} && Field::IsEditable;
};

// Predicate: field is replicated
template<typename Field>
struct TIsReplicated
{
	static constexpr bool value = requires {
		{ Field::IsReplicated } -> std::convertible_to<bool>;
	} && Field::IsReplicated;
};

// Predicate: field is transient
template<typename Field>
struct TIsTransient
{
	static constexpr bool value = requires {
		{ Field::IsTransient } -> std::convertible_to<bool>;
	} && Field::IsTransient;
};

// Predicate: field has specific attribute
template<typename Attr>
struct THasAttribute
{
	template<typename Field>
	struct Predicate
	{
		static constexpr bool value = requires {
			{ Field::template HasAttr<Attr> } -> std::convertible_to<bool>;
		} && Field::template HasAttr<Attr>;
	};
};


// ============================================================================
// Usage Example
// ============================================================================
//
// struct FFlightPathData {
//     FGuid PathId;
//     float CurrentDistance = 0.f;
//     float DesiredSpeed = 1500.f;
//     bool bLooping = true;
//
//     FLIGHT_REFLECT_BODY(FFlightPathData)
// };
//
// FLIGHT_REFLECT_FIELDS(FFlightPathData,
//     FLIGHT_FIELD(FGuid, PathId),
//     FLIGHT_FIELD(float, CurrentDistance),
//     FLIGHT_FIELD(float, DesiredSpeed),
//     FLIGHT_FIELD(bool, bLooping)
// )
//
// // With attributes:
// struct FFlightPathDataAttr {
//     float CurrentDistance = 0.f;
//     float DesiredSpeed = 1500.f;
//     bool bLooping = true;
//
//     FLIGHT_REFLECT_BODY(FFlightPathDataAttr)
// };
//
// FLIGHT_REFLECT_FIELDS_ATTR(FFlightPathDataAttr,
//     FLIGHT_FIELD_ATTR(float, CurrentDistance,
//         Attr::VisibleAnywhere,
//         Attr::Transient
//     ),
//     FLIGHT_FIELD_ATTR(float, DesiredSpeed,
//         Attr::EditAnywhere,
//         Attr::BlueprintReadWrite,
//         Attr::Replicated,
//         Attr::ClampedValue<0, 10000>
//     ),
//     FLIGHT_FIELD_ATTR(bool, bLooping,
//         Attr::EditAnywhere,
//         Attr::SaveGame,
//         Attr::Category<"Path">
//     )
// )
//
// // Compile-time checks work:
// static_assert(CReflectable<FFlightPathData>);
// static_assert(TReflectionTraits<FFlightPathData>::Fields::Count == 4);
//
// // Attribute queries at compile time:
// using Fields = TReflectionTraits<FFlightPathDataAttr>::Fields;
// using SpeedField = Fields::At<1>;
// static_assert(SpeedField::IsEditable);
// static_assert(SpeedField::IsReplicated);
// static_assert(SpeedField::HasAttrTemplate<Attr::ClampedValue>);


// ============================================================================
// Future Directions
// ============================================================================
//
// 1. Constexpr string for field names - DONE via TStaticString
// 2. Attribute system - DONE with partial template matching
// 3. Property change notifications - DONE (lightweight version)
// 4. Editor integration - generate PropertyCustomization from traits
// 5. Network replication - DONE with TBitArray for >32 fields
// 6. Automatic diff/patch - DONE with POD detection
// 7. Blueprint binding generation without UHT
// 8. Hot-reload support via type versioning traits

} // namespace Flight::Reflection
