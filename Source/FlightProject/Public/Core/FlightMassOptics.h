// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightMassOptics - Reimagined ECS reflection with Pattern/Tree optics
//
// What if we designed Mass ECS reflection from scratch with:
// - Declarative pattern matching on fragment compositions
// - Compile-time query validation
// - Tree-like fragment introspection
// - Rewrite rules for archetype transformation
//
// This is an exploration/prototype - treating UE source as our own.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "Core/FlightFunctional.h"
#include "Core/FlightReflection.h"
#include <tuple>
#include <type_traits>

namespace Flight::MassOptics
{

using namespace Flight::Functional;
using namespace Flight::Reflection;
using namespace ::Flight::Reflection::Attr;

// ============================================================================
// Fragment Concepts (extending UE::Mass)
// ============================================================================

// Fragment category tags (phantom types for compile-time dispatch)
struct FragmentCategory {};
struct TagCategory {};
struct ChunkCategory {};
struct SharedCategory {};
struct ConstSharedCategory {};

template<typename T> struct TFragmentCategoryOf;
template<typename T> requires UE::Mass::CFragment<T>
struct TFragmentCategoryOf<T> { using Type = FragmentCategory; };
template<typename T> requires UE::Mass::CTag<T>
struct TFragmentCategoryOf<T> { using Type = TagCategory; };
template<typename T> requires UE::Mass::CChunkFragment<T>
struct TFragmentCategoryOf<T> { using Type = ChunkCategory; };
template<typename T> requires UE::Mass::CSharedFragment<T>
struct TFragmentCategoryOf<T> { using Type = SharedCategory; };
template<typename T> requires UE::Mass::CConstSharedFragment<T>
struct TFragmentCategoryOf<T> { using Type = ConstSharedCategory; };


// ============================================================================
// Declarative Query Patterns
// ============================================================================
//
// Instead of:
//   Query.AddRequirement<FFragmentA>(EMassFragmentAccess::ReadWrite);
//   Query.AddRequirement<FFragmentB>(EMassFragmentAccess::ReadOnly);
//   Query.AddTagRequirement<FTagC>(EMassFragmentPresence::All);
//
// Write:
//   using MyQuery = QueryPattern<
//       Read<FFragmentA>,
//       Write<FFragmentB>,
//       Has<FTagC>
//   >;

// Access mode wrappers (compile-time)
template<typename T> struct Read { using Fragment = T; static constexpr bool Mutable = false; };
template<typename T> struct Write { using Fragment = T; static constexpr bool Mutable = true; };
template<typename T> struct Has { using Fragment = T; };  // Presence only (tags)
template<typename T> struct Without { using Fragment = T; };  // Absence requirement
template<typename T> struct Maybe { using Fragment = T; };  // Optional

// Query pattern - compile-time validated fragment combination
template<typename... Requirements>
struct TQueryPattern
{
	// Extract fragment types
	template<typename R> using FragmentOf = typename R::Fragment;

	// Validate all fragments at compile time
	static_assert((UE::Mass::CElement<FragmentOf<Requirements>> && ...),
		"All query requirements must be valid Mass fragments/tags");

	// Count by category
	static constexpr size_t FragmentCount =
		((std::is_same_v<typename TFragmentCategoryOf<FragmentOf<Requirements>>::Type, FragmentCategory> ? 1 : 0) + ...);
	static constexpr size_t TagCount =
		((std::is_same_v<typename TFragmentCategoryOf<FragmentOf<Requirements>>::Type, TagCategory> ? 1 : 0) + ...);

	// Apply to FMassEntityQuery
	static void Configure(FMassEntityQuery& Query)
	{
		(ConfigureOne<Requirements>(Query), ...);
	}

private:
	template<typename R>
	static void ConfigureOne(FMassEntityQuery& Query)
	{
		using F = typename R::Fragment;

		if constexpr (std::is_same_v<R, Read<F>>)
		{
			Query.AddRequirement<F>(EMassFragmentAccess::ReadOnly);
		}
		else if constexpr (std::is_same_v<R, Write<F>>)
		{
			Query.AddRequirement<F>(EMassFragmentAccess::ReadWrite);
		}
		else if constexpr (std::is_same_v<R, Has<F>>)
		{
			if constexpr (UE::Mass::CTag<F>)
			{
				Query.AddTagRequirement<F>(EMassFragmentPresence::All);
			}
			else
			{
				Query.AddRequirement<F>(EMassFragmentAccess::None, EMassFragmentPresence::All);
			}
		}
		else if constexpr (std::is_same_v<R, Without<F>>)
		{
			if constexpr (UE::Mass::CTag<F>)
			{
				Query.AddTagRequirement<F>(EMassFragmentPresence::None);
			}
			else
			{
				Query.AddRequirement<F>(EMassFragmentAccess::None, EMassFragmentPresence::None);
			}
		}
		else if constexpr (std::is_same_v<R, Maybe<F>>)
		{
			Query.AddRequirement<F>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
		}
	}
};


// ============================================================================
// Typed Execution Context
// ============================================================================
//
// Instead of runtime-checked fragment access:
//   auto Fragments = Context.GetMutableFragmentView<FFragmentA>();
//
// Get compile-time validated, type-safe access:
//   auto [A, B] = TypedContext<Read<FFragmentA>, Write<FFragmentB>>::Bind(Context);
//   for (int32 i : Context) {
//       const auto& a = A[i];  // Read-only
//       auto& b = B[i];        // Mutable
//   }

template<typename... Requirements>
class TTypedContext
{
public:
	// Tuple of views (const for Read, mutable for Write)
	using ViewTuple = std::tuple<
		std::conditional_t<
			Requirements::Mutable,
			TArrayView<typename Requirements::Fragment>,
			TConstArrayView<typename Requirements::Fragment>
		>...
	>;

	static ViewTuple Bind(FMassExecutionContext& Context)
	{
		return ViewTuple(GetView<Requirements>(Context)...);
	}

private:
	template<typename R>
	static auto GetView(FMassExecutionContext& Context)
	{
		using F = typename R::Fragment;
		if constexpr (R::Mutable)
		{
			return Context.GetMutableFragmentView<F>();
		}
		else
		{
			return Context.GetFragmentView<F>();
		}
	}
};


// ============================================================================
// Pattern Matching on Archetypes
// ============================================================================
//
// Match against archetype composition with Pattern<T> style:
//
//   auto Result = MatchArchetype(Archetype)
//       .When<Has<FFlightPathFollowFragment>, Has<FFlightSwarmMemberTag>>([](auto) {
//           return TEXT("Path-following swarm member");
//       })
//       .When<Has<FFlightPathFollowFragment>>([](auto) {
//           return TEXT("Path follower");
//       })
//       .Default([](auto) {
//           return TEXT("Unknown");
//       });

template<typename TOutput>
class TArchetypeMatcher
{
public:
	explicit TArchetypeMatcher(const FMassArchetypeCompositionDescriptor& InDesc)
		: Desc(InDesc)
	{
	}

	// Match against a pattern of fragment requirements
	template<typename... Pattern, typename THandler>
	TArchetypeMatcher& When(THandler&& Handler)
	{
		if (!Result.IsSet())
		{
			if (Matches<Pattern...>())
			{
				Result = Handler(Desc);
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
		return Handler(Desc);
	}

private:
	template<typename... Pattern>
	bool Matches() const
	{
		return (MatchOne<Pattern>() && ...);
	}

	template<typename P>
	bool MatchOne() const
	{
		using F = typename P::Fragment;

		if constexpr (std::is_same_v<P, Has<F>>)
		{
			if constexpr (UE::Mass::CTag<F>)
			{
				return Desc.GetTags().Contains<F>();
			}
			else if constexpr (UE::Mass::CFragment<F>)
			{
				return Desc.GetFragments().Contains<F>();
			}
			// ... other categories
		}
		else if constexpr (std::is_same_v<P, Without<F>>)
		{
			if constexpr (UE::Mass::CTag<F>)
			{
				return !Desc.GetTags().Contains<F>();
			}
			else if constexpr (UE::Mass::CFragment<F>)
			{
				return !Desc.GetFragments().Contains<F>();
			}
		}
		return false;
	}

	const FMassArchetypeCompositionDescriptor& Desc;
	TOptional<TOutput> Result;
};

template<typename TOutput = FString>
TArchetypeMatcher<TOutput> MatchArchetype(const FMassArchetypeCompositionDescriptor& Desc)
{
	return TArchetypeMatcher<TOutput>(Desc);
}


// ============================================================================
// Archetype Rewrite Rules
// ============================================================================
//
// Declarative transformation of archetype compositions.
// Think of it as Pattern<Archetype> with replacement.
//
//   auto Rule = RewriteRule<
//       Has<FOldFragment>,      // Match: has this
//       Without<FNewFragment>   // Match: doesn't have this
//   >::Into<
//       Without<FOldFragment>,  // Result: remove this
//       Has<FNewFragment>       // Result: add this
//   >;
//
//   if (Rule.Matches(Archetype)) {
//       auto NewArchetype = Rule.Apply(Archetype);
//   }

template<typename... MatchPattern>
struct TRewriteMatch
{
	template<typename... ResultPattern>
	struct Into
	{
		static bool Matches(const FMassArchetypeCompositionDescriptor& Desc)
		{
			return (MatchOne<MatchPattern>(Desc) && ...);
		}

		static FMassArchetypeCompositionDescriptor Apply(FMassArchetypeCompositionDescriptor Desc)
		{
			(ApplyOne<ResultPattern>(Desc), ...);
			return Desc;
		}

	private:
		template<typename P>
		static bool MatchOne(const FMassArchetypeCompositionDescriptor& Desc)
		{
			using F = typename P::Fragment;
			if constexpr (std::is_same_v<P, Has<F>>)
			{
				if constexpr (UE::Mass::CFragment<F>)
					return Desc.GetFragments().Contains<F>();
				else if constexpr (UE::Mass::CTag<F>)
					return Desc.GetTags().Contains<F>();
			}
			else if constexpr (std::is_same_v<P, Without<F>>)
			{
				if constexpr (UE::Mass::CFragment<F>)
					return !Desc.GetFragments().Contains<F>();
				else if constexpr (UE::Mass::CTag<F>)
					return !Desc.GetTags().Contains<F>();
			}
			return true;
		}

		template<typename P>
		static void ApplyOne(FMassArchetypeCompositionDescriptor& Desc)
		{
			using F = typename P::Fragment;
			if constexpr (std::is_same_v<P, Has<F>>)
			{
				// Add fragment to composition
				if constexpr (UE::Mass::CFragment<F>)
					Desc.GetFragments().Add<F>();
				else if constexpr (UE::Mass::CTag<F>)
					Desc.GetTags().Add<F>();
			}
			else if constexpr (std::is_same_v<P, Without<F>>)
			{
				// Remove fragment from composition
				if constexpr (UE::Mass::CFragment<F>)
					Desc.GetFragments().Remove<F>();
				else if constexpr (UE::Mass::CTag<F>)
					Desc.GetTags().Remove<F>();
			}
		}
	};
};

template<typename... MatchPattern>
using RewriteRule = TRewriteMatch<MatchPattern...>;


// ============================================================================
// Fragment Tree (for nested struct fragments)
// ============================================================================
//
// Fragments containing other fragments (via UScriptStruct) form a tree.
// This enables recursive traversal similar to FlightPropertyOptics.

/**
 * Get nested fragment types from a struct fragment
 */
inline TArray<const UScriptStruct*> GetNestedFragmentTypes(const UScriptStruct* FragmentStruct)
{
	TArray<const UScriptStruct*> Nested;

	if (!FragmentStruct)
	{
		return Nested;
	}

	for (TFieldIterator<FStructProperty> It(FragmentStruct); It; ++It)
	{
		const UScriptStruct* InnerStruct = It->Struct;
		if (InnerStruct && InnerStruct->IsChildOf(FMassFragment::StaticStruct()))
		{
			Nested.Add(InnerStruct);
		}
	}

	return Nested;
}

/**
 * Fold over all fragment types in an archetype (including nested)
 */
template<typename TAccum, typename TFolder>
TAccum FoldArchetypeFragments(
	const FMassArchetypeCompositionDescriptor& Desc,
	TAccum Initial,
	TFolder&& Folder)
{
	TAccum Accum = MoveTemp(Initial);

	// This would require access to the fragment type registry to iterate
	// For now, this is a sketch of the API

	return Accum;
}


// ============================================================================
// Query Combinators (Or, And, Not)
// ============================================================================
//
// Compose queries with set operations:
//
//   using QueryA = QueryPattern<Read<FragA>>;
//   using QueryB = QueryPattern<Read<FragB>>;
//   using Combined = QueryOr<QueryA, QueryB>;  // Matches either

template<typename... Queries>
struct TQueryOr
{
	// Matches if ANY of the queries would match
	static bool Matches(const FMassArchetypeCompositionDescriptor& Desc)
	{
		return (QueryMatches<Queries>(Desc) || ...);
	}

private:
	template<typename Q>
	static bool QueryMatches(const FMassArchetypeCompositionDescriptor& Desc)
	{
		// Would need to build a temporary query and check matching
		// This is a sketch - actual implementation would use bitset ops
		return false;
	}
};

template<typename... Queries>
struct TQueryAnd
{
	// Matches if ALL queries would match
	static bool Matches(const FMassArchetypeCompositionDescriptor& Desc)
	{
		return (QueryMatches<Queries>(Desc) && ...);
	}

private:
	template<typename Q>
	static bool QueryMatches(const FMassArchetypeCompositionDescriptor&) { return true; }
};


// ============================================================================
// Usage Examples
// ============================================================================
//
// 1. Declarative query definition:
//
//    using FlightPathQuery = QueryPattern<
//        Write<FFlightPathFollowFragment>,
//        Read<FFlightTransformFragment>,
//        Has<FFlightSwarmMemberTag>
//    >;
//
//    void ConfigureQueries() {
//        FlightPathQuery::Configure(EntityQuery);
//    }
//
// 2. Type-safe fragment access:
//
//    void Execute(FMassExecutionContext& Context) {
//        auto [PathFrags, TransformFrags] = TTypedContext<
//            Write<FFlightPathFollowFragment>,
//            Read<FFlightTransformFragment>
//        >::Bind(Context);
//
//        for (int32 i : Context) {
//            PathFrags[i].CurrentDistance += DeltaTime * PathFrags[i].DesiredSpeed;
//            const FVector& Pos = TransformFrags[i].Position;
//        }
//    }
//
// 3. Archetype pattern matching:
//
//    FString Classify(const FMassArchetypeCompositionDescriptor& Desc) {
//        return MatchArchetype<FString>(Desc)
//            .When<Has<FFlightPathFollowFragment>, Has<FFlightSwarmMemberTag>>([](auto) {
//                return TEXT("Swarm Member");
//            })
//            .When<Has<FFlightPathFollowFragment>>([](auto) {
//                return TEXT("Path Follower");
//            })
//            .Default([](auto) {
//                return TEXT("Unknown");
//            });
//    }
//
// 4. Archetype rewrite rules (for migration/upgrades):
//
//    using MigrateToV2 = RewriteRule<
//        Has<FLegacyFragment>,
//        Without<FNewFragment>
//    >::Into<
//        Without<FLegacyFragment>,
//        Has<FNewFragment>
//    >;
//
//    if (MigrateToV2::Matches(Archetype)) {
//        auto NewArchetype = MigrateToV2::Apply(Archetype);
//    }

} // namespace Flight::MassOptics
