// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightPropertyOptics - Tree/Pattern optics for UE property system
//
// Bringing m2-style declarative traversal to Unreal's reflection system.
// Composable patterns replace callback hell and switch statements.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "UObject/Field.h"
#include "Core/FlightFunctional.h"
#include <type_traits>
#include <tuple>

namespace Flight::PropertyOptics
{

using namespace Flight::Functional;

// ============================================================================
// Property Pattern - Declarative matching on FProperty types
// ============================================================================
//
// Instead of:
//   if (Prop->IsA<FStructProperty>()) { ... }
//   else if (Prop->IsA<FArrayProperty>()) { ... }
//
// Write:
//   auto Result = Match(Prop)
//       .Case<FStructProperty>([](auto* S) { return HandleStruct(S); })
//       .Case<FArrayProperty>([](auto* A) { return HandleArray(A); })
//       .Default([](auto* P) { return HandleOther(P); });

/**
 * TPropertyPattern - A pattern that matches specific property types
 *
 * Pattern<FProperty*, Output>: apply returns Some(Output) on match, None on fail
 */
template<typename TProp, typename TOutput>
class TPropertyPattern
{
public:
	using Handler = TFunction<TOutput(TProp*)>;

	explicit TPropertyPattern(Handler InHandler)
		: HandlerFn(MoveTemp(InHandler))
	{
	}

	TOptional<TOutput> Apply(FProperty* Prop) const
	{
		if (TProp* Typed = CastField<TProp>(Prop))
		{
			return TOptional<TOutput>(HandlerFn(Typed));
		}
		return TOptional<TOutput>();
	}

	// Combinator: try this pattern, then other on failure
	template<typename TOther>
	auto Or(TOther&& Other) const
	{
		return TOrPattern<TPropertyPattern, std::decay_t<TOther>>(*this, Forward<TOther>(Other));
	}

private:
	Handler HandlerFn;
};

/**
 * TOrPattern - Try first pattern, then second on failure
 */
template<typename TFirst, typename TSecond>
class TOrPattern
{
public:
	TOrPattern(TFirst InFirst, TSecond InSecond)
		: First(MoveTemp(InFirst)), Second(MoveTemp(InSecond))
	{
	}

	template<typename T>
	auto Apply(T* Input) const -> decltype(First.Apply(Input))
	{
		if (auto Result = First.Apply(Input))
		{
			return Result;
		}
		return Second.Apply(Input);
	}

	template<typename TOther>
	auto Or(TOther&& Other) const
	{
		return TOrPattern<TOrPattern, std::decay_t<TOther>>(*this, Forward<TOther>(Other));
	}

private:
	TFirst First;
	TSecond Second;
};

/**
 * TPropertyMatcher - Fluent builder for property pattern matching
 *
 * Usage:
 *   int32 Size = Match(Prop)
 *       .Case<FStructProperty>([](auto* S) { return S->Struct->GetPropertiesSize(); })
 *       .Case<FArrayProperty>([](auto* A) { return A->Inner->GetSize(); })
 *       .Default([](auto* P) { return P->GetSize(); });
 */
template<typename TOutput>
class TPropertyMatcher
{
public:
	explicit TPropertyMatcher(FProperty* InProp)
		: Prop(InProp)
	{
	}

	// Add a case for a specific property type
	template<typename TProp, typename THandler>
	TPropertyMatcher& Case(THandler&& Handler)
	{
		static_assert(std::is_base_of_v<FProperty, TProp>,
			"Case type must derive from FProperty");

		if (!Result.IsSet())
		{
			if (TProp* Typed = CastField<TProp>(Prop))
			{
				Result = Handler(Typed);
			}
		}
		return *this;
	}

	// Terminal: provide default for unmatched cases
	template<typename THandler>
	TOutput Default(THandler&& Handler)
	{
		if (Result.IsSet())
		{
			return MoveTemp(Result.GetValue());
		}
		return Handler(Prop);
	}

	// Terminal: return optional (None if no case matched)
	TOptional<TOutput> Finish()
	{
		return MoveTemp(Result);
	}

private:
	FProperty* Prop;
	TOptional<TOutput> Result;
};

// Entry point for pattern matching
template<typename TOutput = void*, typename = std::enable_if_t<!std::is_void_v<TOutput>>>
TPropertyMatcher<TOutput> Match(FProperty* Prop)
{
	return TPropertyMatcher<TOutput>(Prop);
}


// ============================================================================
// Property Tree - Recursive traversal with composable operations
// ============================================================================
//
// FProperty forms a virtual tree:
//   - Struct properties → nested struct's properties
//   - Array properties → Inner property
//   - Map properties → Key + Value properties
//   - Set properties → Element property
//
// This wrapper provides Tree-like operations over that structure.

/**
 * Get logical children of a property (inner properties for containers)
 */
inline TArray<FProperty*> GetPropertyChildren(FProperty* Prop)
{
	TArray<FProperty*> Children;

	if (!Prop)
	{
		return Children;
	}

	// Container properties have inner properties
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
		if (ArrayProp->Inner)
		{
			Children.Add(ArrayProp->Inner);
		}
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
	{
		if (MapProp->KeyProp)
		{
			Children.Add(MapProp->KeyProp);
		}
		if (MapProp->ValueProp)
		{
			Children.Add(MapProp->ValueProp);
		}
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
	{
		if (SetProp->ElementProp)
		{
			Children.Add(SetProp->ElementProp);
		}
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		// Nested struct: iterate its properties
		if (StructProp->Struct)
		{
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				Children.Add(*It);
			}
		}
	}

	return Children;
}

/**
 * Check if property is a leaf (no logical children)
 */
inline bool IsPropertyLeaf(FProperty* Prop)
{
	return GetPropertyChildren(Prop).Num() == 0;
}

/**
 * Get property depth (max depth to any leaf)
 */
inline int32 GetPropertyDepth(FProperty* Prop)
{
	if (!Prop)
	{
		return 0;
	}

	int32 MaxChildDepth = 0;
	for (FProperty* Child : GetPropertyChildren(Prop))
	{
		MaxChildDepth = FMath::Max(MaxChildDepth, GetPropertyDepth(Child));
	}
	return 1 + MaxChildDepth;
}

/**
 * Count all properties in the tree (self + descendants)
 */
inline int32 GetPropertyTreeSize(FProperty* Prop)
{
	if (!Prop)
	{
		return 0;
	}

	int32 Size = 1;
	for (FProperty* Child : GetPropertyChildren(Prop))
	{
		Size += GetPropertyTreeSize(Child);
	}
	return Size;
}


// ============================================================================
// Tree Traversal - Transform, Rewrite, Collect
// ============================================================================

/**
 * Visit all properties in tree (depth-first, pre-order)
 *
 * @param Prop Root property
 * @param Visitor Called for each property, return false to stop
 * @return true if completed, false if stopped early
 */
template<typename TVisitor>
bool ForEachProperty(FProperty* Prop, TVisitor&& Visitor)
{
	if (!Prop)
	{
		return true;
	}

	if (!Visitor(Prop))
	{
		return false;
	}

	for (FProperty* Child : GetPropertyChildren(Prop))
	{
		if (!ForEachProperty(Child, Visitor))
		{
			return false;
		}
	}

	return true;
}

/**
 * Collect all properties in tree matching a predicate
 */
template<typename TPredicate>
TArray<FProperty*> FilterProperties(FProperty* Prop, TPredicate&& Pred)
{
	TArray<FProperty*> Result;
	ForEachProperty(Prop, [&](FProperty* P) {
		if (Pred(P))
		{
			Result.Add(P);
		}
		return true;  // Continue
	});
	return Result;
}

/**
 * Check if any property in tree matches predicate
 */
template<typename TPredicate>
bool AnyProperty(FProperty* Prop, TPredicate&& Pred)
{
	bool Found = false;
	ForEachProperty(Prop, [&](FProperty* P) {
		if (Pred(P))
		{
			Found = true;
			return false;  // Stop
		}
		return true;
	});
	return Found;
}

/**
 * Check if all properties in tree match predicate
 */
template<typename TPredicate>
bool AllProperties(FProperty* Prop, TPredicate&& Pred)
{
	bool AllMatch = true;
	ForEachProperty(Prop, [&](FProperty* P) {
		if (!Pred(P))
		{
			AllMatch = false;
			return false;  // Stop
		}
		return true;
	});
	return AllMatch;
}

/**
 * Fold over all properties (reduce pattern)
 */
template<typename TAccum, typename TFolder>
TAccum FoldProperties(FProperty* Prop, TAccum Initial, TFolder&& Folder)
{
	TAccum Accum = MoveTemp(Initial);
	ForEachProperty(Prop, [&](FProperty* P) {
		Accum = Folder(MoveTemp(Accum), P);
		return true;
	});
	return Accum;
}


// ============================================================================
// Struct Traversal - Iterate properties of a UStruct
// ============================================================================

/**
 * Visit all properties of a struct (not recursive into nested structs)
 */
template<typename TVisitor>
void ForEachStructProperty(const UStruct* Struct, TVisitor&& Visitor)
{
	if (!Struct)
	{
		return;
	}

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (!Visitor(*It))
		{
			break;
		}
	}
}

/**
 * Visit all properties of a struct, recursing into nested structs
 */
template<typename TVisitor>
void ForEachStructPropertyDeep(const UStruct* Struct, TVisitor&& Visitor)
{
	ForEachStructProperty(Struct, [&](FProperty* Prop) {
		return ForEachProperty(Prop, Visitor);
	});
}


// ============================================================================
// Pattern Constructors - Reusable property patterns
// ============================================================================

/**
 * Pattern: Match numeric properties
 */
inline auto IsNumeric()
{
	return [](FProperty* P) { return P && P->IsA<FNumericProperty>(); };
}

/**
 * Pattern: Match object reference properties
 */
inline auto IsObjectRef()
{
	return [](FProperty* P) { return P && P->IsA<FObjectPropertyBase>(); };
}

/**
 * Pattern: Match container properties (array, map, set)
 */
inline auto IsContainer()
{
	return [](FProperty* P) {
		return P && (P->IsA<FArrayProperty>() ||
		             P->IsA<FMapProperty>() ||
		             P->IsA<FSetProperty>());
	};
}

/**
 * Pattern: Match struct properties
 */
inline auto IsStruct()
{
	return [](FProperty* P) { return P && P->IsA<FStructProperty>(); };
}

/**
 * Pattern: Match properties with specific metadata
 */
inline auto HasMeta(const TCHAR* Key)
{
	return [Key](FProperty* P) { return P && P->HasMetaData(Key); };
}

/**
 * Pattern: Match properties of a specific struct type
 */
template<typename TStruct>
auto IsStructType()
{
	return [](FProperty* P) {
		if (FStructProperty* StructProp = CastField<FStructProperty>(P))
		{
			return StructProp->Struct == TStruct::StaticStruct();
		}
		return false;
	};
}


// ============================================================================
// Example Usage Patterns
// ============================================================================
//
// 1. Count object references in a struct:
//
//    int32 RefCount = FoldProperties(Prop, 0, [](int32 Acc, FProperty* P) {
//        return Acc + (IsObjectRef()(P) ? 1 : 0);
//    });
//
// 2. Find all FVector properties:
//
//    auto Vectors = FilterProperties(Prop, IsStructType<FVector>());
//
// 3. Pattern match on property type:
//
//    FString TypeName = Match<FString>(Prop)
//        .Case<FStructProperty>([](auto* S) { return S->Struct->GetName(); })
//        .Case<FArrayProperty>([](auto* A) { return FString::Printf(TEXT("Array<%s>"), *A->Inner->GetCPPType()); })
//        .Default([](auto* P) { return P->GetCPPType(); });
//
// 4. Calculate total property size (recursive):
//
//    int32 TotalSize = FoldProperties(RootProp, 0, [](int32 Acc, FProperty* P) {
//        return Acc + P->GetSize();
//    });
//
// 5. Check if struct has any BlueprintVisible properties:
//
//    bool HasBPVisible = AnyProperty(Prop, HasMeta(TEXT("BlueprintVisible")));

} // namespace Flight::PropertyOptics
