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
#include <concepts>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Vex/FlightVexTypes.h"

enum class EFlightGpuResourceKind : uint8;
enum class EFlightGpuResourceLifetime : uint8;
enum class EFlightGpuExecutionDomain : uint8;
enum class EFlightGpuAccessClass : uint8;
class UScriptStruct;

namespace Flight::Vex
{
template<typename T>
struct TTypeVexRegistry
{
    static const void* GetTypeKey();
    static void Register();
};
}

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

template<size_t N>
TStaticString(const char (&)[N]) -> TStaticString<N>;


// ============================================================================
// Core Concepts - What makes a type reflectable?
// ============================================================================

template<typename T>
using TReflectBase = std::remove_cvref_t<T>;

enum class EVexCapability : uint8
{
    NotVexCapable,
    VexCapableAuto,
    VexCapableManual,
};

struct FVexSchemaProviderResult
{
    bool bSuccess = false;
    FString Diagnostic;

    static FVexSchemaProviderResult Success(const FString& InDiagnostic = FString())
    {
        FVexSchemaProviderResult Result;
        Result.bSuccess = true;
        Result.Diagnostic = InDiagnostic;
        return Result;
    }

    static FVexSchemaProviderResult Failure(const FString& InDiagnostic)
    {
        FVexSchemaProviderResult Result;
        Result.bSuccess = false;
        Result.Diagnostic = InDiagnostic;
        return Result;
    }
};

template<typename T>
struct TReflectionTraits
{
    static constexpr bool IsReflectable = false;
    static constexpr EVexCapability VexCapability = EVexCapability::NotVexCapable;
};

template<typename T>
using TReflectTraits = TReflectionTraits<TReflectBase<T>>;

template<typename T>
concept CReflectable = TReflectTraits<T>::IsReflectable;

template<typename T>
concept CHasFields = CReflectable<T> && requires {
    typename TReflectTraits<T>::Fields;
};

template<typename T>
concept CSerializable = CReflectable<T> && requires(T& t, FArchive& Ar) {
    { TReflectionTraits<TReflectBase<T>>::Serialize(t, Ar) };
};

namespace Detail
{
    template<typename T>
    struct TRuntimeTypeKey
    {
        static inline uint8 Marker = 0;
    };

    template<typename T>
    void EnsureVexSchemaRegistered();

    template<typename T>
    FVexSchemaProviderResult ProvideAutoVexSchema();

    template<typename T>
    FVexSchemaProviderResult ProvideManualVexSchema();
}

template<typename T>
const void* GetRuntimeTypeKey()
{
    return &Detail::TRuntimeTypeKey<TReflectBase<T>>::Marker;
}

namespace Attr
{
    struct Transient;
    struct DuplicateTransient;
    struct SkipSerialization;
    template<TStaticString Str>
    struct VexSymbol;
}

template<CReflectable T>
void Serialize(T& Value, FArchive& Ar);


// ============================================================================
// Field Descriptor - Compile-time field metadata (TYPE, not instance)
// ============================================================================

template<typename TOwner, typename TField, auto MemberPtr, TStaticString FieldName>
struct TFieldDescriptor
{
    using OwnerType = TOwner;
    using FieldType = TField;
    static constexpr auto Pointer = MemberPtr;
    static constexpr bool IsOffsetEligible = std::is_standard_layout_v<TOwner>;

    static constexpr std::string_view Name{FieldName};
    static constexpr const char* NameCStr = FieldName.CStr();
    static int32 GetOffset() { return static_cast<int32>(reinterpret_cast<intptr_t>(&(((TOwner*)0)->*MemberPtr))); }

    static TField& Get(TOwner& Owner)
    {
        return Owner.*MemberPtr;
    }

    static const TField& Get(const TOwner& Owner)
    {
        return Owner.*MemberPtr;
    }

    template<typename U>
        requires std::is_assignable_v<TField&, U&&>
    static void Set(TOwner& Owner, U&& Value)
    {
        Owner.*MemberPtr = Forward<U>(Value);
    }

    template<typename A>
    static constexpr bool HasAttr = false;

    template<template<auto...> class Template>
    static constexpr bool HasAttrTemplate = false;

    template<template<typename...> class Template>
    static constexpr bool HasAttrTypeTemplate = false;
};


// ============================================================================
// Field List - Compile-time list of field TYPES
// ============================================================================

template<typename... Fields>
struct TFieldList
{
    static constexpr size_t Count = sizeof...(Fields);

    using FieldTuple = std::tuple<Fields...>;

    template<typename F>
    static void ForEach(F&& Func)
    {
        (Func.template operator()<Fields>(), ...);
    }

    template<typename F>
    static void ForEachDescriptor(F&& Func)
    {
        (Func(Fields{}), ...);
    }

    template<typename TOwner, typename F>
    static void ForEachValue(TOwner& Owner, F&& Func)
    {
        (Func(Fields::Get(Owner), Fields{}), ...);
    }

    template<typename TOwner, typename F>
    static void ForEachValue(const TOwner& Owner, F&& Func)
    {
        (Func(Fields::Get(Owner), Fields{}), ...);
    }

    template<typename TAccum, typename F>
    static TAccum Fold(TAccum Initial, F&& Func)
    {
        TAccum Accum = MoveTemp(Initial);
        ((Accum = Func(MoveTemp(Accum), Fields{})), ...);
        return Accum;
    }

    template<size_t I>
    using At = std::tuple_element_t<I, FieldTuple>;
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
        const void* RuntimeKey = nullptr;
        EVexCapability VexCapability = EVexCapability::NotVexCapable;
        mutable const UScriptStruct* NativeStruct = nullptr;
        size_t Size;
        size_t Alignment;
        TArray<FName> FieldNames;
        TFunction<void(void*, FArchive&)> SerializeFn;
        const UScriptStruct* (*ResolveNativeStructFn)() = nullptr;
        void (*EnsureVexSchemaRegisteredFn)() = nullptr;
        FVexSchemaProviderResult (*ProvideVexSchemaFn)() = nullptr;

        const UScriptStruct* GetNativeStruct() const
        {
            if (!NativeStruct && ResolveNativeStructFn)
            {
                NativeStruct = ResolveNativeStructFn();
            }

            return NativeStruct;
        }
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
        Info.RuntimeKey = GetRuntimeTypeKey<T>();
        Info.VexCapability = TReflectTraits<T>::VexCapability;
        Info.Size = sizeof(T);
        Info.Alignment = alignof(T);

        if constexpr (requires { T::StaticStruct(); })
        {
            Info.ResolveNativeStructFn = []() -> const UScriptStruct*
            {
                return T::StaticStruct();
            };
        }

        if constexpr (CHasFields<T>)
        {
            TReflectTraits<T>::Fields::ForEachDescriptor([&Info](auto Descriptor) {
                using DescType = decltype(Descriptor);
                Info.FieldNames.Add(FName(DescType::NameCStr));
            });
        }

        Info.SerializeFn = [](void* Ptr, FArchive& Ar) {
            T& Instance = *static_cast<T*>(Ptr);
            Serialize(Instance, Ar);
        };

        if constexpr (TReflectTraits<T>::VexCapability == EVexCapability::VexCapableAuto)
        {
            Info.EnsureVexSchemaRegisteredFn = &Detail::EnsureVexSchemaRegistered<T>;
            Info.ProvideVexSchemaFn = &Detail::ProvideAutoVexSchema<T>;
        }
        else if constexpr (TReflectTraits<T>::VexCapability == EVexCapability::VexCapableManual)
        {
            Info.ProvideVexSchemaFn = &Detail::ProvideManualVexSchema<T>;
        }

        Types.Add(Info.TypeName, Info);
        if (Info.RuntimeKey)
        {
            RuntimeKeysToTypeNames.Add(Info.RuntimeKey, Info.TypeName);
        }
    }

    const FTypeInfo* Find(FName TypeName) const
    {
        return Types.Find(TypeName);
    }

    const FTypeInfo* FindByRuntimeKey(const void* RuntimeKey) const
    {
        if (const FName* TypeName = RuntimeKeysToTypeNames.Find(RuntimeKey))
        {
            return Types.Find(*TypeName);
        }

        return nullptr;
    }

    const FTypeInfo* FindByNativeStruct(const UScriptStruct* NativeStruct) const
    {
        for (const TPair<FName, FTypeInfo>& Pair : Types)
        {
            if (Pair.Value.GetNativeStruct() == NativeStruct)
            {
                return &Pair.Value;
            }
        }

        return nullptr;
    }

    void GetAllTypes(TArray<FName>& OutNames) const
    {
        Types.GetKeys(OutNames);
    }

private:
    TMap<FName, FTypeInfo> Types;
    TMap<const void*, FName> RuntimeKeysToTypeNames;
};


// ============================================================================
// Serialization via Traits
// ============================================================================

template<typename T>
concept CPod = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

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
                if constexpr (CReflectable<FieldType>)
                {
                    Serialize(FieldValue, Ar);
                }
                else if constexpr (CHasArchiveOperator<FieldType>)
                {
                    Ar << FieldValue;
                }
                else if constexpr (CPod<FieldType>)
                {
                    Ar.Serialize(&FieldValue, sizeof(FieldType));
                }
            }
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
            ((Functional::Detail::HasTag<Required, AcquiredPermissions...>) && ...),
            "Missing required permission");
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

struct EditAnywhere {};
struct EditDefaultsOnly {};
struct EditInstanceOnly {};
struct VisibleAnywhere {};
struct VisibleDefaultsOnly {};
struct VisibleInstanceOnly {};

struct BlueprintReadOnly {};
struct BlueprintReadWrite {};
struct BlueprintCallable {};
struct BlueprintAssignable {};

struct Transient {};
struct DuplicateTransient {};
struct SaveGame {};
struct SkipSerialization {};

struct Replicated {};
struct ReplicatedUsing {};
struct NotReplicated {};

struct Instanced {};
struct Export {};
struct NoClear {};

struct SimpleDisplay {};
struct AdvancedDisplay {};

template<int32 Min, int32 Max>
struct ClampedValue {};

template<int32 Min>
struct UIMin {};

template<int32 Max>
struct UIMax {};

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

template<TStaticString Str>
struct GpuResourceId
{
    static constexpr std::string_view Value{Str};
};

template<EFlightGpuResourceKind Kind>
struct GpuResourceKind
{
    static constexpr EFlightGpuResourceKind Value = Kind;
};

template<EFlightGpuResourceLifetime Lifetime>
struct GpuResourceLifetime
{
    static constexpr EFlightGpuResourceLifetime Value = Lifetime;
};

template<TStaticString Str>
struct GpuBindingName
{
    static constexpr std::string_view Value{Str};
};

template<bool bPreferred>
struct PreferUnrealRdg
{
    static constexpr bool Value = bPreferred;
};

template<bool bRequired>
struct RawVulkanInteropRequired
{
    static constexpr bool Value = bRequired;
};

template<EFlightGpuExecutionDomain Domain, EFlightGpuAccessClass Access>
struct GpuAccessRule
{
    static constexpr EFlightGpuExecutionDomain DomainValue = Domain;
    static constexpr EFlightGpuAccessClass AccessValue = Access;
};

struct AllowPrivateAccess {};
struct ExposeOnSpawn {};
struct Interp {};

} // namespace Attr


// ============================================================================
// Template Matching Utilities - For parameterized attributes
// ============================================================================

template<typename T, template<auto...> class Template>
struct TIsSpecializationOfNTTP : std::false_type {};

template<template<auto...> class Template, auto... Args>
struct TIsSpecializationOfNTTP<Template<Args...>, Template> : std::true_type {};

template<typename T, template<typename...> class Template>
struct TIsSpecializationOf : std::false_type {};

template<template<typename...> class Template, typename... Args>
struct TIsSpecializationOf<Template<Args...>, Template> : std::true_type {};

template<typename T, template<auto...> class Template>
inline constexpr bool IsSpecializationOfNTTP = TIsSpecializationOfNTTP<T, Template>::value;

template<typename T, template<typename...> class Template>
inline constexpr bool IsSpecializationOf = TIsSpecializationOf<T, Template>::value;

namespace Detail
{
    template<typename Fields>
    struct TFieldListHasVexSymbol;

    template<typename... Fields>
    struct TFieldListHasVexSymbol<TFieldList<Fields...>>
    {
        static constexpr bool Value = (Fields::template HasAttrTemplate<Attr::VexSymbol> || ...);
    };

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
            typename TGetTemplate<Template, Tail...>::Type>;
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
            typename TGetTypeTemplate<Template, Tail...>::Type>;
    };

    template<typename Tag, typename... Tags>
    inline constexpr bool HasTag = (std::is_same_v<Tag, Tags> || ...);

    template<template<auto...> class Template, typename AttrType, typename F>
    void InvokeIfTemplate(F&& Func)
    {
        if constexpr (IsSpecializationOfNTTP<AttrType, Template>)
        {
            Func(AttrType{});
        }
    }

    template<template<typename...> class Template, typename AttrType, typename F>
    void InvokeIfTypeTemplate(F&& Func)
    {
        if constexpr (IsSpecializationOf<AttrType, Template>)
        {
            Func(AttrType{});
        }
    }
}


// ============================================================================
// Attribute Queries - Check if field has specific attribute
// ============================================================================

template<typename... Attrs>
struct TAttributeSet
{
    static constexpr size_t Count = sizeof...(Attrs);

    template<typename A>
    static constexpr bool Has = (std::is_same_v<A, Attrs> || ...);

    template<template<auto...> class Template>
    static constexpr bool HasTemplate = (IsSpecializationOfNTTP<Attrs, Template> || ...);

    template<template<typename...> class Template>
    static constexpr bool HasTypeTemplate = (IsSpecializationOf<Attrs, Template> || ...);

    template<template<auto...> class Template, typename F>
    static void ForEachTemplate(F&& Func)
    {
        (Detail::InvokeIfTemplate<Template, Attrs>(Func), ...);
    }

    template<template<typename...> class Template, typename F>
    static void ForEachTypeTemplate(F&& Func)
    {
        (Detail::InvokeIfTypeTemplate<Template, Attrs>(Func), ...);
    }

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
    static constexpr bool HasCategory = HasTemplate<Attr::Category>;
    static constexpr bool HasClampedValue = HasTemplate<Attr::ClampedValue>;

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
    static constexpr bool IsOffsetEligible = std::is_standard_layout_v<TOwner>;

    static int32 GetOffset() { return static_cast<int32>(reinterpret_cast<intptr_t>(&(((TOwner*)0)->*MemberPtr))); }
    static constexpr std::string_view Name{FieldName};
    static constexpr const char* NameCStr = FieldName.CStr();

    static TField& Get(TOwner& Owner) { return Owner.*MemberPtr; }
    static const TField& Get(const TOwner& Owner) { return Owner.*MemberPtr; }

    template<typename U>
        requires std::is_assignable_v<TField&, U&&>
    static void Set(TOwner& Owner, U&& Value)
    {
        Owner.*MemberPtr = Forward<U>(Value);
    }

    template<typename A>
    static constexpr bool HasAttr = Attributes::template Has<A>;

    template<template<auto...> class Template>
    static constexpr bool HasAttrTemplate = Attributes::template HasTemplate<Template>;

    template<template<typename...> class Template>
    static constexpr bool HasAttrTypeTemplate = Attributes::template HasTypeTemplate<Template>;

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

namespace Detail
{
    template<typename T>
    void EnsureVexSchemaRegistered()
    {
        ::Flight::Vex::TTypeVexRegistry<T>::Register();
    }

    template<typename T>
    FVexSchemaProviderResult ProvideAutoVexSchema()
    {
        ::Flight::Vex::TTypeVexRegistry<T>::Register();
        return FVexSchemaProviderResult::Success(TEXT("Auto-reflection VEX schema provider registered the schema."));
    }

    template<typename T>
    FVexSchemaProviderResult ProvideManualVexSchema()
    {
        if constexpr (requires { { ::Flight::Reflection::TReflectTraits<T>::ProvideVexSchema() } -> std::same_as<FVexSchemaProviderResult>; })
        {
            return ::Flight::Reflection::TReflectTraits<T>::ProvideVexSchema();
        }
        else
        {
            return FVexSchemaProviderResult::Failure(TEXT("Manual VEX-capable type is missing a schema provider."));
        }
    }

    template<typename T>
    struct TTypeAutoReg
    {
        static inline const bool bRegistered = []() {
            ::Flight::Reflection::FTypeRegistry::Get().Register<T>();
            return true;
        }();
    };
}

#define FLIGHT_PP_JOIN_IMPL(A, B) A##B
#define FLIGHT_PP_JOIN(A, B) FLIGHT_PP_JOIN_IMPL(A, B)


// ============================================================================
// Reflection Macros - Two-part pattern (user controls struct brace)
// ============================================================================

#define FLIGHT_REFLECT_BODY(TypeName) \
    using FlightReflectSelf = TypeName; \
    friend struct ::Flight::Reflection::TReflectionTraits<TypeName>;

#define FLIGHT_FIELD(Type, Name) \
    ::Flight::Reflection::TFieldDescriptor< \
        FlightReflectSelf, \
        Type, \
        &FlightReflectSelf::Name, \
        ::Flight::Reflection::TStaticString(#Name) \
    >

#define FLIGHT_REFLECT_FIELDS(TypeName, ...) \
    template<> \
    struct ::Flight::Reflection::TReflectionTraits<TypeName> \
    { \
        using FlightReflectSelf = TypeName; \
        static constexpr bool IsReflectable = true; \
        using Type = TypeName; \
        using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
        static constexpr ::Flight::Reflection::EVexCapability VexCapability = ::Flight::Reflection::EVexCapability::NotVexCapable; \
        static constexpr const char* Name = #TypeName; \
    }; \
    namespace { \
        [[maybe_unused]] const bool FLIGHT_PP_JOIN(kFlightAutoReg_, __COUNTER__) = \
            ::Flight::Reflection::Detail::TTypeAutoReg<TypeName>::bRegistered; \
    }

#define FLIGHT_FIELD_ATTR(Type, Name, ...) \
    ::Flight::Reflection::TAttributedFieldDescriptor< \
        FlightReflectSelf, \
        Type, \
        &FlightReflectSelf::Name, \
        ::Flight::Reflection::TStaticString(#Name), \
        ::Flight::Reflection::TAttributeSet<__VA_ARGS__> \
    >

#define FLIGHT_REFLECT_FIELDS_ATTR(TypeName, ...) \
    template<> \
    struct ::Flight::Reflection::TReflectionTraits<TypeName> \
    { \
        using FlightReflectSelf = TypeName; \
        static constexpr bool IsReflectable = true; \
        using Type = TypeName; \
        using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
        static constexpr ::Flight::Reflection::EVexCapability VexCapability = \
            ::Flight::Reflection::Detail::TFieldListHasVexSymbol<Fields>::Value \
                ? ::Flight::Reflection::EVexCapability::VexCapableAuto \
                : ::Flight::Reflection::EVexCapability::NotVexCapable; \
        static constexpr const char* Name = #TypeName; \
    }; \
    namespace { \
        [[maybe_unused]] const bool FLIGHT_PP_JOIN(kFlightAutoReg_, __COUNTER__) = \
            ::Flight::Reflection::Detail::TTypeAutoReg<TypeName>::bRegistered; \
    }

#define FLIGHT_REFLECT_FIELDS_VEX(TypeName, AttrSet, ...) \
    template<> \
    struct ::Flight::Reflection::TReflectionTraits<TypeName> \
    { \
        using FlightReflectSelf = TypeName; \
        static constexpr bool IsReflectable = true; \
        using Type = TypeName; \
        using Attributes = AttrSet; \
        using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
        static constexpr ::Flight::Reflection::EVexCapability VexCapability = ::Flight::Reflection::EVexCapability::VexCapableAuto; \
        static constexpr const char* Name = #TypeName; \
    }; \
    namespace { \
        [[maybe_unused]] const bool FLIGHT_PP_JOIN(kFlightAutoReg_, __COUNTER__) = \
            ::Flight::Reflection::Detail::TTypeAutoReg<TypeName>::bRegistered; \
    }

#define FLIGHT_REFLECT_FIELDS_VEX_MANUAL(TypeName, AttrSet, ProviderFn, ...) \
    template<> \
    struct ::Flight::Reflection::TReflectionTraits<TypeName> \
    { \
        using FlightReflectSelf = TypeName; \
        static constexpr bool IsReflectable = true; \
        using Type = TypeName; \
        using Attributes = AttrSet; \
        using Fields = ::Flight::Reflection::TFieldList<__VA_ARGS__>; \
        static constexpr ::Flight::Reflection::EVexCapability VexCapability = ::Flight::Reflection::EVexCapability::VexCapableManual; \
        static constexpr const char* Name = #TypeName; \
        static ::Flight::Reflection::FVexSchemaProviderResult ProvideVexSchema() \
        { \
            return ProviderFn(); \
        } \
    }; \
    namespace { \
        [[maybe_unused]] const bool FLIGHT_PP_JOIN(kFlightAutoReg_, __COUNTER__) = \
            ::Flight::Reflection::Detail::TTypeAutoReg<TypeName>::bRegistered; \
    }


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
using TFieldChangeCallback = void(*)(TOwner&, const TField&, const TField&);


// ============================================================================
// Observable Field - Lightweight change tracking
// ============================================================================

template<typename T>
class TObservableField
{
public:
    using ChangeCallback = void(*)(const T&, const T&, void*);

    TObservableField() = default;
    explicit TObservableField(T InValue) : Value(MoveTemp(InValue)) {}

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

    operator const T&() const { return Value; }
    const T& Get() const { return Value; }
    T& GetMutable() { return Value; }

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

struct FFieldPatchData
{
    std::string_view FieldName;
    TArray<uint8> SerializedValue;
    TFunction<void(void*)> Applier;
};

template<CReflectable T>
struct TStructPatch
{
    TArray<FFieldPatchData> ChangedFields;
};

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
                if constexpr (requires { OldField == NewField; })
                {
                    if (!(OldField == NewField))
                    {
                        FFieldPatchData PatchData;
                        PatchData.FieldName = DescType::Name;

                        if constexpr (CReflectable<FieldType>)
                        {
                            FMemoryWriter Writer(PatchData.SerializedValue);
                            FieldType Copy = NewField;
                            Serialize(Copy, Writer);
                        }
                        else if constexpr (CPod<FieldType>)
                        {
                            PatchData.SerializedValue.SetNumUninitialized(sizeof(FieldType));
                            FMemory::Memcpy(PatchData.SerializedValue.GetData(), &NewField, sizeof(FieldType));
                        }
                        else if constexpr (CHasArchiveOperator<FieldType>)
                        {
                            FMemoryWriter Writer(PatchData.SerializedValue);
                            FieldType Copy = NewField;
                            Writer << Copy;
                        }
                        else
                        {
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
                        if (PatchData.Applier)
                        {
                            PatchData.Applier(&DescType::Get(Target));
                        }
                        else if constexpr (CReflectable<FieldType>)
                        {
                            FMemoryReader Reader(PatchData.SerializedValue);
                            Serialize(DescType::Get(Target), Reader);
                        }
                        else if constexpr (CPod<FieldType>)
                        {
                            if (PatchData.SerializedValue.Num() == sizeof(FieldType))
                            {
                                FMemory::Memcpy(&DescType::Get(Target), PatchData.SerializedValue.GetData(), sizeof(FieldType));
                            }
                        }
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

template<CReflectable T>
void ReplicateFields(T& Value, FArchive& Ar, TBitArray<>& DirtyFlags)
{
    if constexpr (CHasFields<T>)
    {
        int32 FieldIndex = 0;
        TReflectTraits<T>::Fields::ForEachValue(Value, [&](auto& FieldValue, auto Descriptor) {
            using DescType = decltype(Descriptor);

            if constexpr (
                requires { { DescType::IsReplicated } -> std::convertible_to<bool>; } &&
                DescType::IsReplicated &&
                Policy::IsSerializableField<DescType>)
            {
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

template<typename Field>
struct TIsEditable
{
    static constexpr bool value = requires {
        { Field::IsEditable } -> std::convertible_to<bool>;
    } && Field::IsEditable;
};

template<typename Field>
struct TIsReplicated
{
    static constexpr bool value = requires {
        { Field::IsReplicated } -> std::convertible_to<bool>;
    } && Field::IsReplicated;
};

template<typename Field>
struct TIsTransient
{
    static constexpr bool value = requires {
        { Field::IsTransient } -> std::convertible_to<bool>;
    } && Field::IsTransient;
};

template<typename AttrType>
struct THasAttribute
{
    template<typename Field>
    struct Predicate
    {
        static constexpr bool value = requires {
            { Field::template HasAttr<AttrType> } -> std::convertible_to<bool>;
        } && Field::template HasAttr<AttrType>;
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
// static_assert(CReflectable<FFlightPathData>);
// static_assert(TReflectionTraits<FFlightPathData>::Fields::Count == 4);
//
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
