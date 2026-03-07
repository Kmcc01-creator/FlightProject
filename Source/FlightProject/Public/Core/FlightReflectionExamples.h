// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightReflectionExamples - Concrete examples of trait-based reflection
//
// This file demonstrates the Flight reflection system by re-implementing
// the same fragment structures from FlightMassFragments.h using our
// trait-based approach instead of USTRUCT/UPROPERTY macros.
//
// Key differences from UE reflection:
//   - No generated.h file needed
//   - No UHT preprocessing step
//   - Compile-time attribute queries
//   - Composable serialization and replication
//   - Zero-cost abstractions (no vtables, no RTTI)
//   - User controls struct closing brace

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Core/FlightMassOptics.h"

namespace Flight::ReflectionExamples
{

using namespace Flight::Reflection;
using namespace Flight::Reflection::Attr;

// ============================================================================
// Example 1: Path Following Data (equivalent to FFlightPathFollowFragment)
// ============================================================================
//
// Compare with the USTRUCT version:
//
//   USTRUCT()
//   struct FFlightPathFollowFragment : public FMassFragment {
//       GENERATED_BODY()
//       UPROPERTY() FGuid PathId;
//       UPROPERTY() float CurrentDistance = 0.f;
//       UPROPERTY() float DesiredSpeed = 1500.f;
//       UPROPERTY() bool bLooping = true;
//   };

struct FPathFollowData
{
	FGuid PathId;
	float CurrentDistance = 0.f;
	float DesiredSpeed = 1500.f;
	bool bLooping = true;

	// Two-part pattern: body inside struct, fields outside
	// User controls the closing brace!
	FLIGHT_REFLECT_BODY(FPathFollowData)
};

// Trait specialization OUTSIDE the struct
FLIGHT_REFLECT_FIELDS_ATTR(FPathFollowData,
	FLIGHT_FIELD_ATTR(FGuid, PathId,
		VisibleAnywhere,
		Transient  // Don't save path references
	),
	FLIGHT_FIELD_ATTR(float, CurrentDistance,
		VisibleAnywhere,
		Transient  // Runtime state, don't save
	),
	FLIGHT_FIELD_ATTR(float, DesiredSpeed,
		EditAnywhere,
		BlueprintReadWrite,
		Replicated,
		ClampedValue<0, 10000>
	),
	FLIGHT_FIELD_ATTR(bool, bLooping,
		EditAnywhere,
		BlueprintReadWrite,
		SaveGame
	)
)

// Compile-time validation
static_assert(CReflectable<FPathFollowData>);
static_assert(TReflectionTraits<FPathFollowData>::Fields::Count == 4);

// Field attribute queries at compile time
using PathFields = TReflectionTraits<FPathFollowData>::Fields;
using SpeedField = PathFields::At<2>;  // DesiredSpeed
static_assert(SpeedField::IsEditable);
static_assert(SpeedField::IsReplicated);
static_assert(!SpeedField::IsTransient);
static_assert(SpeedField::HasAttrTemplate<ClampedValue>);


// ============================================================================
// Example 2: Transform Data (equivalent to FFlightTransformFragment)
// ============================================================================

struct FTransformData
{
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	FVector Velocity = FVector::ZeroVector;

	// Helper methods - reflection doesn't require inheritance!
	FTransform AsTransform() const
	{
		return FTransform(Rotation, Location);
	}

	void SetFromTransform(const FTransform& T)
	{
		Location = T.GetLocation();
		Rotation = T.GetRotation();
	}

	FLIGHT_REFLECT_BODY(FTransformData)
};

FLIGHT_REFLECT_FIELDS_ATTR(FTransformData,
	FLIGHT_FIELD_ATTR(FVector, Location,
		EditAnywhere,
		BlueprintReadWrite,
		Replicated
	),
	FLIGHT_FIELD_ATTR(FQuat, Rotation,
		EditAnywhere,
		BlueprintReadWrite,
		Replicated
	),
	FLIGHT_FIELD_ATTR(FVector, Velocity,
		VisibleAnywhere,
		Replicated,
		Transient
	)
)

static_assert(CReflectable<FTransformData>);


// ============================================================================
// Example 3: Visual Properties with Observable Fields
// ============================================================================
//
// Uses TObservableField for automatic change notifications.
// Now uses function pointer (no heap allocation) for ECS-friendly usage.

struct FVisualData
{
	// Observable fields trigger callbacks on change
	// Note: These use function pointers, not TFunction (zero heap allocation)
	TObservableField<FLinearColor> LightColor{FLinearColor(0.588f, 0.784f, 1.0f, 1.0f)};
	TObservableField<float> LightIntensity{8000.f};
	TObservableField<float> LightRadius{1500.f};
	float LightHeightOffset = 250.f;
	bool bUseInverseSquaredFalloff = false;

	// Setup change notifications with function pointer
	static void OnColorChanged(const FLinearColor& Old, const FLinearColor& New, void* UserData)
	{
		UE_LOG(LogTemp, Verbose, TEXT("LightColor changed"));
	}

	static void OnIntensityChanged(const float& Old, const float& New, void* UserData)
	{
		UE_LOG(LogTemp, Verbose, TEXT("LightIntensity: %.1f -> %.1f"), Old, New);
	}

	void BindNotifications()
	{
		LightColor.Bind(&OnColorChanged, this);
		LightIntensity.Bind(&OnIntensityChanged, this);
	}
};


// ============================================================================
// Example 4: Nested Reflectable Types
// ============================================================================
//
// Types can contain other reflectable types for deep traversal.

struct FEntityState
{
	FPathFollowData PathData;
	FTransformData Transform;
	float Health = 100.f;
	bool bActive = true;

	FLIGHT_REFLECT_BODY(FEntityState)
};

FLIGHT_REFLECT_FIELDS_ATTR(FEntityState,
	FLIGHT_FIELD_ATTR(FPathFollowData, PathData,
		EditAnywhere,
		BlueprintReadWrite
	),
	FLIGHT_FIELD_ATTR(FTransformData, Transform,
		EditAnywhere,
		BlueprintReadWrite
	),
	FLIGHT_FIELD_ATTR(float, Health,
		EditAnywhere,
		BlueprintReadWrite,
		ClampedValue<0, 100>,
		Replicated
	),
	FLIGHT_FIELD_ATTR(bool, bActive,
		EditAnywhere,
		Replicated
	)
)

static_assert(CReflectable<FEntityState>);
static_assert(CHasFields<FEntityState>);


// ============================================================================
// Example 5: Simple reflection without attributes
// ============================================================================

struct FSimpleData
{
	int32 Id = 0;
	FString Name;
	float Value = 0.f;

	FLIGHT_REFLECT_BODY(FSimpleData)
};

// Use the non-attributed version for simpler cases
FLIGHT_REFLECT_FIELDS(FSimpleData,
	FLIGHT_FIELD(int32, Id),
	FLIGHT_FIELD(FString, Name),
	FLIGHT_FIELD(float, Value)
)

static_assert(CReflectable<FSimpleData>);
static_assert(TReflectionTraits<FSimpleData>::Fields::Count == 3);


// ============================================================================
// Example 6: Diff/Patch for Undo/Redo
// ============================================================================

inline void DemonstrateDiffPatch()
{
	FPathFollowData Before;
	Before.DesiredSpeed = 1500.f;
	Before.bLooping = true;

	FPathFollowData After = Before;
	After.DesiredSpeed = 2000.f;
	After.bLooping = false;

	// Generate patch (uses POD detection for efficiency)
	auto Patch = Diff(Before, After);

	// Patch contains only changed fields (DesiredSpeed, bLooping)
	check(Patch.ChangedFields.Num() == 2);

	// Apply patch to another instance
	FPathFollowData Target;
	Apply(Target, Patch);

	// Target now has DesiredSpeed=2000, bLooping=false
	check(Target.DesiredSpeed == 2000.f);
	check(Target.bLooping == false);
}


// ============================================================================
// Example 7: Serialization without UPROPERTY
// ============================================================================

inline void DemonstrateSerialization()
{
	FSimpleData Data;
	Data.Id = 42;
	Data.Name = TEXT("TestEntity");
	Data.Value = 3.14f;

	// Serialize to buffer (uses POD detection + archive operators)
	TArray<uint8> Buffer;
	{
		FMemoryWriter Writer(Buffer);
		Serialize(Data, Writer);
	}

	// Deserialize
	FSimpleData Loaded;
	{
		FMemoryReader Reader(Buffer);
		Serialize(Loaded, Reader);
	}

	// Loaded now matches Data
	check(Loaded.Id == 42);
	check(Loaded.Name == TEXT("TestEntity"));
	check(FMath::IsNearlyEqual(Loaded.Value, 3.14f));
}


// ============================================================================
// Example 8: Field Iteration for Debug/Editor
// ============================================================================

inline void DemonstrateFieldIteration()
{
	FPathFollowData Data;
	Data.DesiredSpeed = 3000.f;

	// Iterate all fields (Name is now static constexpr, always valid!)
	ForEachField(Data, [](auto& Value, const char* Name) {
		UE_LOG(LogTemp, Log, TEXT("Field: %hs"), Name);
	});

	// For nested types, iterate deep
	FEntityState Entity;
	ForEachFieldDeep(Entity, [](auto& Value, const char* Name) {
		UE_LOG(LogTemp, Log, TEXT("Deep field: %hs"), Name);
	});
}


// ============================================================================
// Example 9: Attribute-Based Filtering
// ============================================================================

inline void DemonstrateFiltering()
{
	using Fields = TReflectionTraits<FPathFollowData>::Fields;

	// Iterate only editable fields
	using EditableFilter = TFilteredFieldList<Fields, TIsEditable>;
	EditableFilter::ForEach([](auto Descriptor) {
		using DescType = decltype(Descriptor);
		UE_LOG(LogTemp, Log, TEXT("Editable: %hs"), DescType::NameCStr);
	});

	// Iterate only replicated fields
	using ReplicatedFilter = TFilteredFieldList<Fields, TIsReplicated>;
	ReplicatedFilter::ForEach([](auto Descriptor) {
		using DescType = decltype(Descriptor);
		UE_LOG(LogTemp, Log, TEXT("Replicated: %hs"), DescType::NameCStr);
	});

	// Compile-time count of filtered fields
	static_assert(EditableFilter::Count == 2);  // DesiredSpeed, bLooping
	static_assert(ReplicatedFilter::Count == 1);  // DesiredSpeed
}


// ============================================================================
// Example 10: Type Registry for Runtime Discovery
// ============================================================================

inline void DemonstrateRegistry()
{
	auto& Registry = FTypeRegistry::Get();

	// Register our types
	Registry.Register<FPathFollowData>();
	Registry.Register<FTransformData>();
	Registry.Register<FEntityState>();
	Registry.Register<FSimpleData>();

	// Runtime lookup
	if (const auto* Info = Registry.Find(TEXT("FPathFollowData")))
	{
		UE_LOG(LogTemp, Log, TEXT("Found type: %s, Size: %zu, Fields: %d"),
			*Info->TypeName.ToString(),
			Info->Size,
			Info->FieldNames.Num());
	}
}


// ============================================================================
// Example 11: Category Attribute with Compile-Time String
// ============================================================================

struct FCategorizedData
{
	float Speed = 0.f;
	float MaxSpeed = 1000.f;
	FVector Position = FVector::ZeroVector;
	bool bEnabled = true;

	FLIGHT_REFLECT_BODY(FCategorizedData)
};

FLIGHT_REFLECT_FIELDS_ATTR(FCategorizedData,
	FLIGHT_FIELD_ATTR(float, Speed,
		EditAnywhere,
		Category<"Movement">
	),
	FLIGHT_FIELD_ATTR(float, MaxSpeed,
		EditAnywhere,
		Category<"Movement">,
		ClampedValue<0, 10000>
	),
	FLIGHT_FIELD_ATTR(FVector, Position,
		EditAnywhere,
		Category<"Transform">
	),
	FLIGHT_FIELD_ATTR(bool, bEnabled,
		EditAnywhere,
		Category<"State">
	)
)

// Verify parameterized attribute detection works
using CatFields = TReflectionTraits<FCategorizedData>::Fields;
using SpeedFieldCat = CatFields::At<0>;
static_assert(SpeedFieldCat::HasCategory);
static_assert(SpeedFieldCat::HasAttrTemplate<Category>);


// ============================================================================
// Comparison: UE Reflection vs Flight Reflection
// ============================================================================
//
// UE Traditional:
//   - Requires USTRUCT()/GENERATED_BODY() macros
//   - Requires UPROPERTY() on each field
//   - Requires UnrealHeaderTool (UHT) preprocessing
//   - Generates .generated.h files
//   - Runtime RTTI via UScriptStruct
//   - Metadata stored in UStruct at runtime
//
// Flight Trait-Based:
//   - Just traits and concepts (standard C++20)
//   - No code generation step
//   - Compile-time attribute queries via template metaprogramming
//   - Zero runtime overhead for reflection queries
//   - Composable with functional patterns (TResult, TValidateChain)
//   - User controls struct brace (two-part macro pattern)
//   - Static constexpr field names (no nullptr issues)
//   - Perfect forwarding for non-copyable types
//   - POD detection for efficient serialization
//   - Partial template matching for parameterized attributes
//
// Trade-offs:
//   - UE: Better Blueprint/Editor integration (built-in)
//   - Flight: Better compile-time guarantees, no UHT dependency
//   - UE: Works with existing engine systems
//   - Flight: Better for pure C++ data processing, Mass ECS hot paths
//
// Recommended Usage:
//   - Use UE reflection for Actor/Component properties exposed to Editor
//   - Use Flight reflection for internal data structures, ECS fragments
//   - The two can coexist - a type can have both USTRUCT and FLIGHT_REFLECT

} // namespace Flight::ReflectionExamples
