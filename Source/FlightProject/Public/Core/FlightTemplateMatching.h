// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightTemplateMatching - Deep dive into partial template matching
//
// This file explains the TIsSpecializationOfNTTP mechanism used for
// detecting parameterized attributes like Category<"Foo">.

#pragma once

#include <type_traits>
#include <concepts>

namespace Flight::TemplateMatching
{

// ============================================================================
// The Problem: Exact Matching Doesn't Work for Templates
// ============================================================================
//
// Given:
//   template<int N> struct Limit {};
//   using MyLimit = Limit<100>;
//
// We want to ask: "Is MyLimit some kind of Limit<>?"
//
// std::is_same_v<MyLimit, Limit<100>>  → true  (exact match)
// std::is_same_v<MyLimit, Limit<200>>  → false (different instantiation)
// std::is_same_v<MyLimit, Limit<???>>  → ??? (can't express "any Limit")
//
// We need a way to match the TEMPLATE, ignoring the arguments.


// ============================================================================
// Solution 1: Non-Type Template Parameters (NTTP)
// ============================================================================
//
// For templates with auto/non-type parameters like:
//   template<int N> struct Limit {};
//   template<auto... Args> struct Config {};
//   template<TStaticString S> struct Category {};

// Primary template: T is NOT a specialization of Template
template<typename T, template<auto...> class Template>
struct TIsSpecializationOfNTTP : std::false_type {};

// Partial specialization: T IS Template<Args...> for some Args
//
// This matches when:
//   T = Template<Args...>  (T is literally an instantiation of Template)
//
// The compiler tries to match TIsSpecializationOfNTTP<T, Template>
// against TIsSpecializationOfNTTP<Template<Args...>, Template>
//
// If T = Limit<100> and Template = Limit, then:
//   Template<Args...> = Limit<Args...>
//   We need Limit<Args...> = Limit<100>
//   Therefore Args... = {100}
//   Match succeeds! → true_type
template<template<auto...> class Template, auto... Args>
struct TIsSpecializationOfNTTP<Template<Args...>, Template> : std::true_type {};

// Helper variable template
template<typename T, template<auto...> class Template>
inline constexpr bool IsNTTPSpecialization = TIsSpecializationOfNTTP<T, Template>::value;


// ============================================================================
// Solution 2: Type Template Parameters
// ============================================================================
//
// For templates with type parameters like:
//   template<typename T> struct Container {};
//   template<typename K, typename V> struct Map {};

template<typename T, template<typename...> class Template>
struct TIsSpecializationOf : std::false_type {};

template<template<typename...> class Template, typename... Args>
struct TIsSpecializationOf<Template<Args...>, Template> : std::true_type {};

template<typename T, template<typename...> class Template>
inline constexpr bool IsTypeSpecialization = TIsSpecializationOf<T, Template>::value;


// ============================================================================
// Example: How the Matching Works Step by Step
// ============================================================================

template<int N>
struct Limit {};

template<int Min, int Max>
struct Range {};

// Test cases
using Limit100 = Limit<100>;
using Limit200 = Limit<200>;
using Range0_100 = Range<0, 100>;

// Step-by-step for TIsSpecializationOfNTTP<Limit100, Limit>:
//
// 1. Compiler sees: TIsSpecializationOfNTTP<Limit<100>, Limit>
//
// 2. Tries primary template:
//    TIsSpecializationOfNTTP<T, Template> with T=Limit<100>, Template=Limit
//    → Would match, gives false_type
//
// 3. Tries partial specialization:
//    TIsSpecializationOfNTTP<Template<Args...>, Template>
//    Need to unify: Template<Args...> = Limit<100>
//                   Template = Limit
//    So: Limit<Args...> = Limit<100>
//    Therefore: Args... = {100}
//    → Match succeeds! Uses this specialization → true_type
//
// 4. Partial specialization is MORE SPECIALIZED, so it wins

static_assert(IsNTTPSpecialization<Limit100, Limit>);   // true
static_assert(IsNTTPSpecialization<Limit200, Limit>);   // true
static_assert(!IsNTTPSpecialization<Range0_100, Limit>); // false (wrong template)
static_assert(IsNTTPSpecialization<Range0_100, Range>);  // true


// ============================================================================
// Application: Attribute Set Queries
// ============================================================================

namespace Attr
{
    // Simple (non-parameterized) attributes
    struct EditAnywhere {};
    struct Replicated {};

    // Parameterized attributes (NTTP)
    template<int Min, int Max>
    struct ClampedValue
    {
        static constexpr int MinValue = Min;
        static constexpr int MaxValue = Max;
    };

    template<int N>
    struct MaxLength {};

    // String-parameterized (requires C++20 NTTP with class types)
    template<size_t N>
    struct FixedString
    {
        char Data[N]{};
        constexpr FixedString(const char (&Str)[N])
        {
            for (size_t i = 0; i < N; ++i) Data[i] = Str[i];
        }
    };
    template<size_t N> FixedString(const char (&)[N]) -> FixedString<N>;

    template<FixedString S>
    struct Category
    {
        static constexpr auto Value = S;
    };
}

template<typename... Attrs>
struct TAttributeSet
{
    // Exact match for simple attributes
    template<typename A>
    static constexpr bool Has = (std::is_same_v<A, Attrs> || ...);

    // Partial match for parameterized attributes
    template<template<auto...> class Template>
    static constexpr bool HasTemplate = (IsNTTPSpecialization<Attrs, Template> || ...);

    // Extract the matched attribute (if any)
    template<template<auto...> class Template>
    struct FindTemplate
    {
        // This is trickier - we need to find and return the matching type
        // For now, just detect presence
    };
};

// Example usage
using MyAttrs = TAttributeSet<
    Attr::EditAnywhere,
    Attr::ClampedValue<0, 100>,
    Attr::Category<"Movement">
>;

// Simple attribute check (exact match)
static_assert(MyAttrs::Has<Attr::EditAnywhere>);
static_assert(!MyAttrs::Has<Attr::Replicated>);

// Parameterized attribute check (template match)
static_assert(MyAttrs::HasTemplate<Attr::ClampedValue>);  // Has SOME ClampedValue
static_assert(MyAttrs::HasTemplate<Attr::Category>);       // Has SOME Category
static_assert(!MyAttrs::HasTemplate<Attr::MaxLength>);     // No MaxLength at all


// ============================================================================
// Advanced: Extracting Template Arguments
// ============================================================================
//
// Sometimes we want not just "has Category?" but "what's the Category value?"

template<typename T, template<auto...> class Template>
struct TExtractArgs;

// Specialization that captures the arguments
template<template<auto...> class Template, auto... Args>
struct TExtractArgs<Template<Args...>, Template>
{
    static constexpr bool IsMatch = true;

    // For single-argument templates
    static constexpr auto Value = []() {
        if constexpr (sizeof...(Args) == 1)
        {
            return (Args, ...);  // Fold to get single value
        }
    }();

    // For multi-argument, return tuple
    static constexpr auto Values = std::tuple{Args...};
};

// Primary template (no match)
template<typename T, template<auto...> class Template>
struct TExtractArgs
{
    static constexpr bool IsMatch = false;
};

// Example: Extract ClampedValue bounds
using MyClamped = Attr::ClampedValue<10, 200>;
using Extracted = TExtractArgs<MyClamped, Attr::ClampedValue>;

static_assert(Extracted::IsMatch);
static_assert(std::get<0>(Extracted::Values) == 10);   // Min
static_assert(std::get<1>(Extracted::Values) == 200);  // Max


// ============================================================================
// Finding a Template in an Attribute Pack
// ============================================================================

template<template<auto...> class Template, typename... Attrs>
struct TFindTemplateInPack;

// Base case: not found
template<template<auto...> class Template>
struct TFindTemplateInPack<Template>
{
    static constexpr bool Found = false;
    using Type = void;
};

// Recursive case: check head, recurse on tail
template<template<auto...> class Template, typename Head, typename... Tail>
struct TFindTemplateInPack<Template, Head, Tail...>
{
private:
    static constexpr bool HeadMatches = IsNTTPSpecialization<Head, Template>;
    using TailResult = TFindTemplateInPack<Template, Tail...>;

public:
    static constexpr bool Found = HeadMatches || TailResult::Found;

    // Return the first match
    using Type = std::conditional_t<
        HeadMatches,
        Head,
        typename TailResult::Type
    >;
};

// Helper
template<template<auto...> class Template, typename... Attrs>
using FindTemplate = typename TFindTemplateInPack<Template, Attrs...>::Type;

// Example: Find the Category attribute
using FoundCategory = FindTemplate<Attr::Category,
    Attr::EditAnywhere,
    Attr::ClampedValue<0,100>,
    Attr::Category<"Movement">
>;

static_assert(std::is_same_v<FoundCategory, Attr::Category<"Movement">>);


// ============================================================================
// Concept-Based Alternative (C++20)
// ============================================================================

template<typename T, template<auto...> class Template>
concept CIsSpecializationOf = IsNTTPSpecialization<T, Template>;

// Can use in requires clauses
template<typename Field>
concept CHasClampedValue = requires {
    typename Field::Attributes;
} && Field::Attributes::template HasTemplate<Attr::ClampedValue>;

// Or directly constrain functions
template<CIsSpecializationOf<Attr::ClampedValue> T>
constexpr auto GetClampedMin(T)
{
    return T::MinValue;
}

static_assert(GetClampedMin(Attr::ClampedValue<5, 50>{}) == 5);


// ============================================================================
// Why This Matters for Reflection
// ============================================================================
//
// In FlightReflection, we use this to:
//
// 1. Detect if a field has ANY Category (regardless of which string):
//    static_assert(SpeedField::HasAttrTemplate<Attr::Category>);
//
// 2. Detect if a field has ANY ClampedValue (regardless of bounds):
//    static_assert(SpeedField::HasAttrTemplate<Attr::ClampedValue>);
//
// 3. Filter fields by template attribute:
//    using ClampedFields = TFilteredFieldList<Fields, HasClampedValuePredicate>;
//
// 4. Extract attribute values at compile time:
//    constexpr int MaxSpeed = ExtractClampedMax<SpeedField>();
//
// This enables UE-style UPROPERTY metadata but at compile time!


// ============================================================================
// Comparison: Type vs NTTP Specialization Detection
// ============================================================================
//
// For TYPE parameters (template<typename...>):
//   template<typename T> struct Optional {};
//   Use: TIsSpecializationOf<T, Optional>
//
// For NTTP parameters (template<auto...>):
//   template<int N> struct Limit {};
//   Use: TIsSpecializationOfNTTP<T, Limit>
//
// For MIXED parameters (template<typename T, int N>):
//   Need a custom trait - neither works directly!
//
// Mixed example:
//   template<typename T, int N>
//   struct FixedArray {};
//
//   template<typename T, template<typename, auto> class Template>
//   struct TIsMixedSpecialization : std::false_type {};
//
//   template<template<typename, auto> class Template, typename T, auto N>
//   struct TIsMixedSpecialization<Template<T, N>, Template> : std::true_type {};

} // namespace Flight::TemplateMatching
