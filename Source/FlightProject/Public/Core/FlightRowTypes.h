// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightRowTypes - Extensible Record Types (Row Polymorphism)
//
// Row types treat records as sets of labeled fields that can be
// extended, restricted, and merged at the type level.
//
// This enables:
//   - Type-safe field access by compile-time label
//   - Row polymorphism: functions that work on any row with required fields
//   - Type-level record extension (Add) and restriction (Remove)
//   - Record merging with compile-time duplicate detection
//
// Inspired by:
//   - OCaml's polymorphic records
//   - TypeScript's mapped types
//   - PureScript's row types
//   - Haskell's vinyl library

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include <type_traits>
#include <concepts>
#include <string_view>

namespace Flight::RowTypes
{

using Flight::Reflection::TStaticString;

// ============================================================================
// Forward Declarations
// ============================================================================

template<typename... Fields>
struct TRow;

template<TStaticString Label, typename T>
struct TRowField;


// ============================================================================
// TRowField - A Labeled Field
// ============================================================================
//
// Each field has:
//   - A compile-time string label (NTTP)
//   - A type
//   - Storage for the value
//
// Fields are inherited by TRow, enabling EBO for empty types.

template<TStaticString Label, typename T>
struct TRowField
{
	using Type = T;
	static constexpr TStaticString LabelString = Label;
	static constexpr std::string_view Name{Label};

	T Value{};

	// Default constructors
	TRowField() = default;
	explicit TRowField(T InValue) : Value(MoveTemp(InValue)) {}

	// Allow aggregate-like initialization
	TRowField(const TRowField&) = default;
	TRowField(TRowField&&) = default;
	TRowField& operator=(const TRowField&) = default;
	TRowField& operator=(TRowField&&) = default;
};


// ============================================================================
// Type-Level Field Lookup
// ============================================================================

namespace Detail
{

// Check if a type is a TRowField
template<typename T>
struct TIsRowField : std::false_type {};

template<TStaticString Label, typename T>
struct TIsRowField<TRowField<Label, T>> : std::true_type {};

template<typename T>
inline constexpr bool IsRowField = TIsRowField<T>::value;


// Find a field by label in a pack
template<TStaticString Label, typename... Fields>
struct TFindField
{
	static constexpr bool Found = false;
	using Type = void;
	using FieldType = void;
};

template<TStaticString Label, typename Head, typename... Tail>
struct TFindField<Label, Head, Tail...>
{
private:
	static constexpr bool Match = (Head::Name == std::string_view{Label});
	using TailResult = TFindField<Label, Tail...>;

public:
	static constexpr bool Found = Match || TailResult::Found;

	using Type = std::conditional_t<
		Match,
		typename Head::Type,
		typename TailResult::Type
	>;

	using FieldType = std::conditional_t<
		Match,
		Head,
		typename TailResult::FieldType
	>;
};


// Check if label exists in pack
template<TStaticString Label, typename... Fields>
inline constexpr bool HasLabel = TFindField<Label, Fields...>::Found;


// ============================================================================
// Type-Level Field Filtering (for Remove)
// ============================================================================

// Prepend a type to a TRow
template<typename Field, typename Row>
struct TPrepend;

template<typename Field, typename... Fields>
struct TPrepend<Field, TRow<Fields...>>
{
	using Type = TRow<Field, Fields...>;
};

// Filter out a field by label
template<TStaticString Label, typename Row>
struct TRemoveField;

template<TStaticString Label>
struct TRemoveField<Label, TRow<>>
{
	using Type = TRow<>;
};

template<TStaticString Label, typename Head, typename... Tail>
struct TRemoveField<Label, TRow<Head, Tail...>>
{
private:
	static constexpr bool ShouldRemove = (Head::Name == std::string_view{Label});
	using FilteredTail = typename TRemoveField<Label, TRow<Tail...>>::Type;

public:
	using Type = std::conditional_t<
		ShouldRemove,
		FilteredTail,
		typename TPrepend<Head, FilteredTail>::Type
	>;
};


// ============================================================================
// Type-Level Row Merging
// ============================================================================

// Extract fields from a row
template<typename Row>
struct TRowFields;

template<typename... Fields>
struct TRowFields<TRow<Fields...>>
{
	using Type = TRow<Fields...>;

	template<typename OtherRow>
	using MergeWith = typename TRowFields<OtherRow>::template PrependTo<Fields...>;

	template<typename... OtherFields>
	using PrependTo = TRow<OtherFields..., Fields...>;
};

// Merge two rows (simple concatenation - duplicates cause compile error on access)
template<typename Row1, typename Row2>
struct TMergeRows;

template<typename... Fields1, typename... Fields2>
struct TMergeRows<TRow<Fields1...>, TRow<Fields2...>>
{
	using Type = TRow<Fields1..., Fields2...>;
};


// ============================================================================
// Type-Level Row Mapping
// ============================================================================

// Apply a metafunction to each field's type
template<typename Row, template<typename> class Transform>
struct TMapRow;

template<typename... Fields, template<typename> class Transform>
struct TMapRow<TRow<Fields...>, Transform>
{
	using Type = TRow<
		TRowField<Fields::LabelString, typename Transform<typename Fields::Type>::Type>...
	>;
};


// ============================================================================
// Duplicate Detection
// ============================================================================

// Check for duplicate labels (compile-time error if found)
template<typename... Fields>
struct THasDuplicateLabels
{
private:
	template<typename Checked, typename... Remaining>
	struct TCheckDuplicates
	{
		static constexpr bool Value = false;
	};

	template<typename... Checked, typename Head, typename... Tail>
	struct TCheckDuplicates<TRow<Checked...>, Head, Tail...>
	{
		static constexpr bool HeadIsDuplicate =
			((Head::Name == Checked::Name) || ...);

		static constexpr bool Value =
			HeadIsDuplicate ||
			TCheckDuplicates<TRow<Checked..., Head>, Tail...>::Value;
	};

	template<typename... Checked>
	struct TCheckDuplicates<TRow<Checked...>>
	{
		static constexpr bool Value = false;
	};

public:
	static constexpr bool Value = TCheckDuplicates<TRow<>, Fields...>::Value;
};

} // namespace Detail


// ============================================================================
// TRow - Extensible Record Type
// ============================================================================
//
// A row is a compile-time collection of labeled fields.
// Inherits from all fields for storage (enables EBO).
//
// Type-level operations:
//   - Has<"label">     : Check if field exists
//   - FieldType<"label">: Get type of field
//   - Add<"label", T>  : Extend with new field
//   - Remove<"label">  : Remove field by label
//   - Merge<OtherRow>  : Combine with another row
//   - Map<Transform>   : Transform all field types
//
// Value-level operations:
//   - Get<"label">()   : Get field value
//   - Set<"label">(v)  : Set field value
//   - GetOr<"label">(d): Get with default

template<typename... Fields>
struct TRow : Fields...
{
	// Verify all types are TRowField
	static_assert((Detail::IsRowField<Fields> && ...),
		"TRow can only contain TRowField types");

	// Warn about duplicates (will cause ambiguous access)
	static_assert(!Detail::THasDuplicateLabels<Fields...>::Value,
		"TRow contains duplicate field labels");

	static constexpr size_t Count = sizeof...(Fields);

	// ─────────────────────────────────────────────────────────────
	// Type-Level Queries
	// ─────────────────────────────────────────────────────────────

	// Check if row has a field with given label
	template<TStaticString Label>
	static constexpr bool Has = Detail::HasLabel<Label, Fields...>;

	// Get the type of a field by label
	template<TStaticString Label>
	using FieldType = typename Detail::TFindField<Label, Fields...>::Type;

	// Get the full field descriptor
	template<TStaticString Label>
	using Field = typename Detail::TFindField<Label, Fields...>::FieldType;

	// ─────────────────────────────────────────────────────────────
	// Type-Level Operations
	// ─────────────────────────────────────────────────────────────

	// Add a new field (compile error if label already exists)
	template<TStaticString Label, typename T>
	using Add = std::conditional_t<
		Has<Label>,
		void,  // Will cause error if used - label already exists
		TRow<Fields..., TRowField<Label, T>>
	>;

	// Remove a field by label
	template<TStaticString Label>
	using Remove = typename Detail::TRemoveField<Label, TRow>::Type;

	// Merge with another row
	template<typename OtherRow>
	using Merge = typename Detail::TMergeRows<TRow, OtherRow>::Type;

	// Transform all field types
	template<template<typename> class Transform>
	using Map = typename Detail::TMapRow<TRow, Transform>::Type;

	// ─────────────────────────────────────────────────────────────
	// Constructors
	// ─────────────────────────────────────────────────────────────

	TRow() = default;

	// Construct from field values (in order)
	template<typename... Args>
		requires (sizeof...(Args) == sizeof...(Fields))
			&& (std::is_constructible_v<typename Fields::Type, Args> && ...)
	explicit TRow(Args&&... InValues)
		: Fields{Forward<Args>(InValues)}...
	{}

	// Copy/move
	TRow(const TRow&) = default;
	TRow(TRow&&) = default;
	TRow& operator=(const TRow&) = default;
	TRow& operator=(TRow&&) = default;

	// ─────────────────────────────────────────────────────────────
	// Value-Level Access
	// ─────────────────────────────────────────────────────────────

	// Get field value by label
	template<TStaticString Label>
		requires Has<Label>
	auto& Get()
	{
		using FieldT = Field<Label>;
		return static_cast<FieldT&>(*this).Value;
	}

	template<TStaticString Label>
		requires Has<Label>
	const auto& Get() const
	{
		using FieldT = Field<Label>;
		return static_cast<const FieldT&>(*this).Value;
	}

	// Set field value by label
	template<TStaticString Label, typename U>
		requires Has<Label> && std::is_assignable_v<FieldType<Label>&, U&&>
	void Set(U&& Value)
	{
		Get<Label>() = Forward<U>(Value);
	}

	// Get with default (for optional fields in generic code)
	template<TStaticString Label, typename Default>
	decltype(auto) GetOr(Default&& DefaultValue) const
	{
		if constexpr (Has<Label>)
		{
			return Get<Label>();
		}
		else
		{
			return Forward<Default>(DefaultValue);
		}
	}

	// ─────────────────────────────────────────────────────────────
	// Iteration
	// ─────────────────────────────────────────────────────────────

	// Apply function to each field (receives value and name)
	template<typename F>
	void ForEach(F&& Func)
	{
		(Func(static_cast<Fields&>(*this).Value, Fields::Name), ...);
	}

	template<typename F>
	void ForEach(F&& Func) const
	{
		(Func(static_cast<const Fields&>(*this).Value, Fields::Name), ...);
	}

	// Apply function to each field descriptor
	template<typename F>
	static void ForEachField(F&& Func)
	{
		(Func.template operator()<Fields>(), ...);
	}

	// Fold over fields
	template<typename TAccum, typename F>
	TAccum Fold(TAccum Initial, F&& Func) const
	{
		TAccum Accum = MoveTemp(Initial);
		((Accum = Func(MoveTemp(Accum), static_cast<const Fields&>(*this).Value, Fields::Name)), ...);
		return Accum;
	}

	// ─────────────────────────────────────────────────────────────
	// Equality
	// ─────────────────────────────────────────────────────────────

	bool operator==(const TRow& Other) const
		requires (requires(const typename Fields::Type& a, const typename Fields::Type& b) { a == b; } && ...)
	{
		return ((static_cast<const Fields&>(*this).Value ==
			static_cast<const Fields&>(Other).Value) && ...);
	}

	bool operator!=(const TRow& Other) const
		requires (requires(const typename Fields::Type& a, const typename Fields::Type& b) { a == b; } && ...)
	{
		return !(*this == Other);
	}
};

// Empty row specialization
template<>
struct TRow<>
{
	static constexpr size_t Count = 0;

	template<TStaticString Label>
	static constexpr bool Has = false;

	template<TStaticString Label, typename T>
	using Add = TRow<TRowField<Label, T>>;

	template<TStaticString Label>
	using Remove = TRow<>;

	template<typename OtherRow>
	using Merge = OtherRow;

	template<typename F>
	void ForEach(F&&) const {}

	template<typename F>
	static void ForEachField(F&&) {}

	template<typename TAccum, typename F>
	TAccum Fold(TAccum Initial, F&&) const { return Initial; }

	bool operator==(const TRow&) const { return true; }
	bool operator!=(const TRow&) const { return false; }
};


// ============================================================================
// Row Polymorphism Concepts
// ============================================================================

// Concept: Row has a specific field
template<typename R, TStaticString Label>
concept CHasField = requires {
	{ R::template Has<Label> } -> std::same_as<const bool&>;
} && R::template Has<Label>;

// Concept: Row has multiple fields
template<typename R, TStaticString... Labels>
concept CHasFields = (CHasField<R, Labels> && ...);

// Concept: Row has a field of specific type
template<typename R, TStaticString Label, typename T>
concept CHasFieldOfType = CHasField<R, Label> &&
	std::same_as<typename R::template FieldType<Label>, T>;

// Concept: Type is a TRow
template<typename T>
concept CRow = requires {
	{ T::Count } -> std::same_as<const size_t&>;
};


// ============================================================================
// Helper: Create Row from pairs
// ============================================================================

// Make a single field
template<TStaticString Label, typename T>
TRowField<Label, T> MakeField(T&& Value)
{
	return TRowField<Label, T>{Forward<T>(Value)};
}


// ============================================================================
// Row Operations (Free Functions)
// ============================================================================

// Project: extract subset of fields
template<TStaticString... Labels, CRow R>
	requires (CHasField<R, Labels> && ...)
auto Project(const R& Row)
{
	return TRow<TRowField<Labels, typename R::template FieldType<Labels>>...>{
		Row.template Get<Labels>()...
	};
}

// Extend: add a field to existing row
template<TStaticString Label, typename T, CRow R>
	requires (!CHasField<R, Label>)
auto Extend(const R& Row, T&& Value)
{
	using NewRow = typename R::template Add<Label, std::decay_t<T>>;

	NewRow Result;

	// Copy existing fields
	Row.ForEach([&Result](const auto& Val, std::string_view Name) {
		Result.ForEach([&Val, Name](auto& DestVal, std::string_view DestName) {
			if (Name == DestName)
			{
				if constexpr (std::is_assignable_v<decltype(DestVal), decltype(Val)>)
				{
					DestVal = Val;
				}
			}
		});
	});

	// Set new field
	Result.template Set<Label>(Forward<T>(Value));

	return Result;
}

// Rename: change a field's label
template<TStaticString OldLabel, TStaticString NewLabel, CRow R>
	requires CHasField<R, OldLabel> && (!CHasField<R, NewLabel>)
auto Rename(const R& Row)
{
	using OldType = typename R::template FieldType<OldLabel>;
	using Removed = typename R::template Remove<OldLabel>;
	using Result = typename Removed::template Add<NewLabel, OldType>;

	Result NewRow;

	// Copy all fields except the renamed one
	Row.ForEach([&NewRow](const auto& Val, std::string_view Name) {
		if (Name != std::string_view{OldLabel})
		{
			NewRow.ForEach([&Val, Name](auto& DestVal, std::string_view DestName) {
				if (Name == DestName)
				{
					if constexpr (std::is_assignable_v<decltype(DestVal), decltype(Val)>)
					{
						DestVal = Val;
					}
				}
			});
		}
	});

	// Copy the renamed field's value
	NewRow.template Set<NewLabel>(Row.template Get<OldLabel>());

	return NewRow;
}


// ============================================================================
// Serialization Support
// ============================================================================

template<CRow R>
void SerializeRow(R& Row, FArchive& Ar)
{
	Row.ForEach([&Ar](auto& Value, std::string_view) {
		using ValueType = std::decay_t<decltype(Value)>;

		if constexpr (requires { Ar << Value; })
		{
			Ar << Value;
		}
		else if constexpr (std::is_trivially_copyable_v<ValueType>)
		{
			Ar.Serialize(&Value, sizeof(ValueType));
		}
	});
}


// ============================================================================
// Debug/Logging Support
// ============================================================================

template<CRow R>
FString RowToString(const R& Row)
{
	FString Result = TEXT("{");
	bool bFirst = true;

	Row.ForEach([&Result, &bFirst](const auto& Value, std::string_view Name) {
		if (!bFirst) Result += TEXT(", ");
		bFirst = false;

		Result += FString::Printf(TEXT("%hs: "), Name.data());

		using ValueType = std::decay_t<decltype(Value)>;
		if constexpr (std::is_same_v<ValueType, FString>)
		{
			Result += Value;
		}
		else if constexpr (std::is_same_v<ValueType, FVector>)
		{
			Result += Value.ToString();
		}
		else if constexpr (std::is_arithmetic_v<ValueType>)
		{
			if constexpr (std::is_floating_point_v<ValueType>)
			{
				Result += FString::Printf(TEXT("%f"), static_cast<double>(Value));
			}
			else
			{
				Result += FString::Printf(TEXT("%lld"), static_cast<long long>(Value));
			}
		}
		else if constexpr (std::is_same_v<ValueType, bool>)
		{
			Result += Value ? TEXT("true") : TEXT("false");
		}
		else
		{
			Result += TEXT("<...>");
		}
	});

	Result += TEXT("}");
	return Result;
}


// ============================================================================
// Examples and Tests
// ============================================================================

namespace Examples
{

// Basic field creation
using PositionField = TRowField<"Position", FVector>;
using VelocityField = TRowField<"Velocity", FVector>;
using HealthField = TRowField<"Health", float>;
using NameField = TRowField<"Name", FString>;

// Row type composition
using Point2D = TRow<
	TRowField<"x", float>,
	TRowField<"y", float>
>;

using Point3D = Point2D::Add<"z", float>;

using Entity = TRow<
	TRowField<"Position", FVector>,
	TRowField<"Rotation", FQuat>,
	TRowField<"Name", FString>
>;

using MovingEntity = Entity::Add<"Velocity", FVector>;

// Static assertions
static_assert(Point2D::Count == 2);
static_assert(Point3D::Count == 3);
static_assert(Entity::Count == 3);
static_assert(MovingEntity::Count == 4);

static_assert(Point2D::Has<"x">);
static_assert(Point2D::Has<"y">);
static_assert(!Point2D::Has<"z">);

static_assert(Point3D::Has<"x">);
static_assert(Point3D::Has<"y">);
static_assert(Point3D::Has<"z">);

static_assert(std::is_same_v<Point2D::FieldType<"x">, float>);
static_assert(std::is_same_v<Entity::FieldType<"Position">, FVector>);
static_assert(std::is_same_v<Entity::FieldType<"Name">, FString>);

// Remove operation
using Point2DFromPoint3D = Point3D::Remove<"z">;
static_assert(Point2DFromPoint3D::Count == 2);
static_assert(Point2DFromPoint3D::Has<"x">);
static_assert(Point2DFromPoint3D::Has<"y">);
static_assert(!Point2DFromPoint3D::Has<"z">);

// Merge operation
using ColorRow = TRow<
	TRowField<"r", uint8>,
	TRowField<"g", uint8>,
	TRowField<"b", uint8>
>;

using ColoredPoint = Point3D::Merge<ColorRow>;
static_assert(ColoredPoint::Count == 6);
static_assert(ColoredPoint::Has<"x">);
static_assert(ColoredPoint::Has<"r">);

// Row concepts
static_assert(CRow<Point2D>);
static_assert(CRow<Entity>);
static_assert(CHasField<Point2D, "x">);
static_assert(CHasField<Entity, "Position">);
static_assert(!CHasField<Point2D, "z">);
static_assert(CHasFields<Entity, "Position", "Rotation", "Name">);
static_assert(CHasFieldOfType<Entity, "Position", FVector>);


// Row polymorphism example: works on ANY row with x and y
template<typename R>
	requires CHasFields<R, "x", "y">
float DistanceFromOrigin(const R& Point)
{
	const float X = Point.template Get<"x">();
	const float Y = Point.template Get<"y">();
	return FMath::Sqrt(X * X + Y * Y);
}

// Works on ANY row with Position and Velocity
template<typename R>
	requires CHasFields<R, "Position", "Velocity">
void IntegrateMotion(R& Entity, float DeltaTime)
{
	Entity.template Get<"Position">() +=
		Entity.template Get<"Velocity">() * DeltaTime;
}

// Works on ANY row with Health
template<typename R>
	requires CHasField<R, "Health">
void TakeDamage(R& Entity, float Damage)
{
	float& Health = Entity.template Get<"Health">();
	Health = FMath::Max(0.0f, Health - Damage);
}


inline void DemonstrateRowTypes()
{
	// Create a Point2D
	Point2D P2D;
	P2D.Set<"x">(10.0f);
	P2D.Set<"y">(20.0f);

	float Dist = DistanceFromOrigin(P2D);
	UE_LOG(LogTemp, Log, TEXT("Point2D distance: %f"), Dist);

	// Create a Point3D (works with same function!)
	Point3D P3D;
	P3D.Set<"x">(3.0f);
	P3D.Set<"y">(4.0f);
	P3D.Set<"z">(0.0f);

	Dist = DistanceFromOrigin(P3D);  // Still works!
	UE_LOG(LogTemp, Log, TEXT("Point3D distance: %f"), Dist);

	// Create entity
	MovingEntity Ent;
	Ent.Set<"Position">(FVector(0, 0, 0));
	Ent.Set<"Velocity">(FVector(100, 50, 0));
	Ent.Set<"Rotation">(FQuat::Identity);
	Ent.Set<"Name">(TEXT("TestEntity"));

	// Integrate motion
	IntegrateMotion(Ent, 0.016f);  // ~60fps
	UE_LOG(LogTemp, Log, TEXT("New position: %s"), *Ent.Get<"Position">().ToString());

	// Iterate all fields
	Ent.ForEach([](const auto& Value, std::string_view Name) {
		UE_LOG(LogTemp, Log, TEXT("Field: %hs"), Name.data());
	});

	// Print row
	UE_LOG(LogTemp, Log, TEXT("Entity: %s"), *RowToString(Ent));
}

} // namespace Examples

} // namespace Flight::RowTypes
