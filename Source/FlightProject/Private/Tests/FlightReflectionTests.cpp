// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

// Reflection and Optics system
#include "Core/FlightReflection.h"
#include "Core/FlightRowTypes.h"
#include "Core/FlightMassOptics.h"
#include "Core/FlightReflectionMocks.h"

#if !WITH_DEV_AUTOMATION_TESTS
#error "WITH_DEV_AUTOMATION_TESTS is not defined or 0!"
#endif

#if WITH_AUTOMATION_TESTS

using namespace Flight::Reflection;
using namespace Flight::Reflection::Attr;
using namespace Flight::RowTypes;
using namespace Flight::Mocks;

/**
 * Mock Asset Structure for Testing
 * Demonstrates composition of reflection, row types, and attributes.
 */
namespace Flight::Reflection::Test
{
    // 1. Define a base "AssetMetadata" that every asset should have.
    struct FAssetMetadata
    {
        FGuid Guid;
        FString DisplayName;
        int32 Version = 1;
        bool bIsInternal = false;

        FLIGHT_REFLECT_BODY(FAssetMetadata);
    };

    // 2. Define a specific "FlightModel" data structure.
    struct FFlightModelData
    {
        float MaxThrust = 5000.f;
        float LiftCoefficient = 0.85f;
        float DragCoefficient = 0.02f;
        FVector CenterOfMassOffset = FVector::ZeroVector;

        FLIGHT_REFLECT_BODY(FFlightModelData);
    };

    // 3. Composed "FlightAsset" using Row Types for extensible records.
    using FFlightAssetRow = TRow<
        TRowField<"Meta", FAssetMetadata>,
        TRowField<"Model", FFlightModelData>
    >;

    // 4. A more complex asset that adds "Visuals"
    struct FVisualConfig
    {
        FLinearColor BaseColor = FLinearColor::White;
        float Roughness = 0.5f;
        
        FLIGHT_REFLECT_BODY(FVisualConfig);
    };

    using FVisualFlightAssetRow = FFlightAssetRow::Add<"Visuals", FVisualConfig>;
}

// Specializations for the internal test types
namespace Flight::Reflection
{
    using namespace Flight::Reflection::Test;
    using namespace Flight::Reflection::Attr;

    FLIGHT_REFLECT_FIELDS_ATTR(FAssetMetadata,
        FLIGHT_FIELD_ATTR(FGuid, Guid, VisibleAnywhere, Transient),
        FLIGHT_FIELD_ATTR(FString, DisplayName, EditAnywhere, Category<"Identity">),
        FLIGHT_FIELD_ATTR(int32, Version, VisibleAnywhere, Category<"Version">),
        FLIGHT_FIELD_ATTR(bool, bIsInternal, EditAnywhere, Category<"State">)
    )

    FLIGHT_REFLECT_FIELDS_ATTR(FFlightModelData,
        FLIGHT_FIELD_ATTR(float, MaxThrust, EditAnywhere, ClampedValue<0, 100000>, Category<"Propulsion">),
        FLIGHT_FIELD_ATTR(float, LiftCoefficient, EditAnywhere, ClampedValue<0, 2>, Category<"Aerodynamics">),
        FLIGHT_FIELD_ATTR(float, DragCoefficient, EditAnywhere, ClampedValue<0, 1>, Category<"Aerodynamics">),
        FLIGHT_FIELD_ATTR(FVector, CenterOfMassOffset, EditAnywhere, Category<"Balance">)
    )

    FLIGHT_REFLECT_FIELDS(FVisualConfig, 
        FLIGHT_FIELD(FLinearColor, BaseColor),
        FLIGHT_FIELD(float, Roughness)
    )
}

/**
 * Test: Basic Reflection Validity
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReflectionValidityTest, "FlightProject.Reflection.Core.Validity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReflectionValidityTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Reflection::Test;

    // Verify CReflectable concept
    TestTrue("FAssetMetadata should be reflectable", CReflectable<FAssetMetadata>);
    TestTrue("FFlightModelData should be reflectable", CReflectable<FFlightModelData>);

    // Verify field counts
    using MetaTraits = TReflectionTraits<FAssetMetadata>;
    TestEqual("FAssetMetadata should have 4 fields", MetaTraits::Fields::Count, 4);

    using ModelTraits = TReflectionTraits<FFlightModelData>;
    TestEqual("FFlightModelData should have 4 fields", ModelTraits::Fields::Count, 4);

    // Verify field names
    using Field0 = MetaTraits::Fields::At<0>;
    TestEqual("Field 0 name should be Guid", FString(Field0::NameCStr), TEXT("Guid"));

    return true;
}

/**
 * Test: Attribute Queries
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReflectionAttributeTest, "FlightProject.Reflection.Core.Attributes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReflectionAttributeTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Reflection::Test;

    using ModelTraits = TReflectionTraits<FFlightModelData>;
    
    using ThrustField = ModelTraits::Fields::At<0>; // MaxThrust
    TestTrue("MaxThrust should be editable", ThrustField::IsEditable);
    TestTrue("MaxThrust should have a Category", ThrustField::HasCategory);
    
    using ThrustCategory = ThrustField::GetAttr<Category>;
    TestEqual("MaxThrust category should be Propulsion", FString(ThrustCategory::Value.data()), TEXT("Propulsion"));

    return true;
}

/**
 * Test: Field Iteration and Access
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReflectionIterationTest, "FlightProject.Reflection.Core.Iteration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReflectionIterationTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Reflection::Test;

    FFlightModelData Model;
    Model.MaxThrust = 12345.f;

    TMap<FString, float> Values;
    ForEachField(Model, [&Values](auto& Value, const char* Name) {
        if constexpr (std::is_same_v<std::decay_t<decltype(Value)>, float>)
        {
            Values.Add(FString(Name), Value);
        }
    });

    TestTrue("Should have found MaxThrust", Values.Contains(TEXT("MaxThrust")));
    TestEqual("MaxThrust value should match", Values[TEXT("MaxThrust")], 12345.f);

    return true;
}

/**
 * Test: Row Type Composition
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReflectionRowCompositionTest, "FlightProject.Reflection.RowTypes.Composition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReflectionRowCompositionTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Reflection::Test;

    FVisualFlightAssetRow Asset;
    Asset.Get<"Meta">().DisplayName = TEXT("StealthDrone_v2");
    Asset.Get<"Model">().MaxThrust = 8000.f;

    TestEqual("Display name should be set", Asset.Get<"Meta">().DisplayName, TEXT("StealthDrone_v2"));

    return true;
}

/**
 * Test: Serialization
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReflectionSerializationTest, "FlightProject.Reflection.Core.Serialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReflectionSerializationTest::RunTest(const FString& Parameters)
{
    using namespace Flight::Reflection::Test;

    FAssetMetadata Original;
    Original.DisplayName = TEXT("SerializedAsset");
    Original.Version = 5;

    TArray<uint8> Buffer;
    FMemoryWriter Writer(Buffer);
    Serialize(Original, Writer);

    FAssetMetadata Loaded;
    FMemoryReader Reader(Buffer);
    Serialize(Loaded, Reader);
    
    TestEqual("DisplayName should match", Loaded.DisplayName, Original.DisplayName);
    TestEqual("Version should match", Loaded.Version, Original.Version);

    return true;
}

/**
 * Test: Drone Asset Mock Composition
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReflectionDroneMockTest, "FlightProject.Reflection.Mocks.DroneAsset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FReflectionDroneMockTest::RunTest(const FString& Parameters)
{
    FDroneAsset Drone;
    Drone.Get<"Meta">().Manufacturer = TEXT("AeroDynamics");
    Drone.Get<"Physics">().MassKG = 2.4f;

    TestEqual("Manufacturer should be set", Drone.Get<"Meta">().Manufacturer, TEXT("AeroDynamics"));
    TestEqual("Mass should be set", Drone.Get<"Physics">().MassKG, 2.4f);

    auto ValidationResult = ValidateDrone(Drone);
    TestTrue("Drone should be valid", ValidationResult.IsOk());

    int32 TotalFields = 0;
    Drone.ForEach([&TotalFields](auto& Component, std::string_view ComponentName) {
        using ComponentType = std::decay_t<decltype(Component)>;
        if constexpr (CReflectable<ComponentType>)
        {
            ForEachField(Component, [&TotalFields](auto& FieldValue, const char* FieldName) {
                TotalFields++;
            });
        }
    });

    TestEqual("Total recursive fields should be 17", TotalFields, 17);

    FStealthDroneAsset StealthDrone;
    TestTrue("Stealth drone should have Stealth", StealthDrone.Has<"Stealth">);
    TestEqual("Stealth drone should have 5 components", FStealthDroneAsset::Count, 5);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
