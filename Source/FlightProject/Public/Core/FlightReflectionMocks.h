// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/FlightReflection.h"
#include "Core/FlightRowTypes.h"
#include "Core/FlightFunctional.h"
#include "MassEntityTypes.h"

namespace Flight::Mocks
{

using namespace Flight::Reflection;
using namespace Flight::Reflection::Attr;
using namespace Flight::RowTypes;
using namespace Flight::Functional;

/**
 * Metadata for a drone asset.
 */
struct FDroneMetadata
{
    FGuid AssetId;
    FString Manufacturer;
    FString ModelName;
    int32 ProductionYear = 2026;
    bool bExperimental = false;

    FLIGHT_REFLECT_BODY(FDroneMetadata);
};

/**
 * Physics parameters for flight simulation.
 */
struct FDronePhysics
{
    float MassKG = 1.5f;
    float MaxThrustNewtons = 45.0f;
    float DragCoefficient = 0.05f;
    FVector MomentOfInertia = FVector(0.01f, 0.01f, 0.02f);
    
    // Observable field for UI/System sync
    TObservableField<float> BatteryLevel{100.0f};

    FLIGHT_REFLECT_BODY(FDronePhysics);
};

/**
 * Perception sensor configuration.
 */
struct FDronePerception
{
    float CameraFieldOfView = 90.0f;
    float LidarRangeMeters = 50.0f;
    bool bHasUltrasonic = true;
    int32 MaxDetectedTargets = 32;

    FLIGHT_REFLECT_BODY(FDronePerception);
};

/**
 * Visual/Rendering configuration.
 */
struct FDroneVisuals
{
    FLinearColor PrimaryColor = FLinearColor::White;
    float EmissiveIntensity = 1.0f;
    bool bCastShadows = true;

    FLIGHT_REFLECT_BODY(FDroneVisuals);
};

/**
 * Composed Drone Asset using Row Types.
 * This is our "Master Asset" prototype.
 */
using FDroneAsset = TRow<
    TRowField<"Meta", FDroneMetadata>,
    TRowField<"Physics", FDronePhysics>,
    TRowField<"Perception", FDronePerception>,
    TRowField<"Visuals", FDroneVisuals>
>;

/**
 * Example of a specialized drone (Stealth variant).
 */
struct FStealthModule
{
    float RadarCrossSection = 0.01f;
    bool bActiveCamouflage = false;

    FLIGHT_REFLECT_BODY(FStealthModule);
};

using FStealthDroneAsset = FDroneAsset::Add<"Stealth", FStealthModule>;

/**
 * Example of a Reactive Mass Fragment.
 * Demonstrates how TObservableField can be used in ECS data.
 */
struct FReactiveFlightFragment : public FMassFragment
{
	TObservableField<float> Altitude{0.0f};
	TObservableField<float> Airspeed{0.0f};
	TObservableField<bool> bLandingGearDown{false};

	FLIGHT_REFLECT_BODY(FReactiveFlightFragment);
};

/**
 * Validation logic using TResult.
 */
inline TResult<bool, FString> ValidateDrone(const FDroneAsset& Drone)
{
    const auto& Physics = Drone.Get<"Physics">();
    if (Physics.MassKG <= 0.0f)
    {
        return Err<bool>(FString(TEXT("Drone mass must be positive")));
    }

    const auto& Meta = Drone.Get<"Meta">();
    if (Meta.Manufacturer.IsEmpty())
    {
        return Err<bool>(FString(TEXT("Manufacturer cannot be empty")));
    }

    return Ok(true);
}

/**
 * Generic Inspector Logic (Prototype)
 * Demonstrates how a single function can inspect ANY row-based asset.
 */
template<CRow R>
void InspectAsset(const R& Row)
{
    UE_LOG(LogTemp, Log, TEXT("Inspecting Asset with %zu components"), R::Count);
    
    Row.ForEach([](const auto& Component, std::string_view ComponentName) {
        using ComponentType = std::decay_t<decltype(Component)>;
        UE_LOG(LogTemp, Log, TEXT("  Component: %hs"), ComponentName.data());

        if constexpr (CReflectable<ComponentType>)
        {
            ForEachField(Component, [](const auto& FieldValue, const char* FieldName) {
                // In a real UI, this would create widgets based on attributes
                UE_LOG(LogTemp, Log, TEXT("    Field: %hs"), FieldName);
            });
        }
    });
}

} // namespace Flight::Mocks

// Specializations MUST be in the Flight::Reflection namespace or global namespace with full qualification
namespace Flight::Reflection
{
    using namespace Flight::Mocks;
    using namespace Flight::Reflection::Attr;

    FLIGHT_REFLECT_FIELDS_ATTR(FDroneMetadata,
        FLIGHT_FIELD_ATTR(FGuid, AssetId, VisibleAnywhere, Transient),
        FLIGHT_FIELD_ATTR(FString, Manufacturer, EditAnywhere, Category<"Identity">),
        FLIGHT_FIELD_ATTR(FString, ModelName, EditAnywhere, Category<"Identity">),
        FLIGHT_FIELD_ATTR(int32, ProductionYear, VisibleAnywhere, Category<"History">),
        FLIGHT_FIELD_ATTR(bool, bExperimental, EditAnywhere, Category<"Status">)
    )

    FLIGHT_REFLECT_FIELDS_ATTR(FDronePhysics,
        FLIGHT_FIELD_ATTR(float, MassKG, EditAnywhere, ClampedValue<0, 100>, Category<"Physics">),
        FLIGHT_FIELD_ATTR(float, MaxThrustNewtons, EditAnywhere, ClampedValue<0, 1000>, Category<"Physics">),
        FLIGHT_FIELD_ATTR(float, DragCoefficient, EditAnywhere, ClampedValue<0, 1>, Category<"Physics">),
        FLIGHT_FIELD_ATTR(FVector, MomentOfInertia, EditAnywhere, Category<"Physics">),
        FLIGHT_FIELD_ATTR(TObservableField<float>, BatteryLevel, VisibleAnywhere, Category<"Status">, Transient)
    )

    FLIGHT_REFLECT_FIELDS_ATTR(FDronePerception,
        FLIGHT_FIELD_ATTR(float, CameraFieldOfView, EditAnywhere, ClampedValue<30, 150>, Category<"Sensors">),
        FLIGHT_FIELD_ATTR(float, LidarRangeMeters, EditAnywhere, ClampedValue<1, 500>, Category<"Sensors">),
        FLIGHT_FIELD_ATTR(bool, bHasUltrasonic, EditAnywhere, Category<"Sensors">),
        FLIGHT_FIELD_ATTR(int32, MaxDetectedTargets, EditAnywhere, ClampedValue<1, 256>, Category<"Sensors">)
    )

    FLIGHT_REFLECT_FIELDS_ATTR(FDroneVisuals,
        FLIGHT_FIELD_ATTR(FLinearColor, PrimaryColor, EditAnywhere, Category<"Rendering">),
        FLIGHT_FIELD_ATTR(float, EmissiveIntensity, EditAnywhere, ClampedValue<0, 100>, Category<"Rendering">),
        FLIGHT_FIELD_ATTR(bool, bCastShadows, EditAnywhere, Category<"Rendering">)
    )

    FLIGHT_REFLECT_FIELDS_ATTR(FStealthModule,
        FLIGHT_FIELD_ATTR(float, RadarCrossSection, EditAnywhere, ClampedValue<0, 1>, Category<"Stealth">),
        FLIGHT_FIELD_ATTR(bool, bActiveCamouflage, EditAnywhere, Category<"Stealth">)
    )

    FLIGHT_REFLECT_FIELDS_ATTR(FReactiveFlightFragment,
        FLIGHT_FIELD_ATTR(TObservableField<float>, Altitude, VisibleAnywhere, Category<"Flight">),
        FLIGHT_FIELD_ATTR(TObservableField<float>, Airspeed, VisibleAnywhere, Category<"Flight">),
        FLIGHT_FIELD_ATTR(TObservableField<bool>, bLandingGearDown, EditAnywhere, Category<"Status">)
    )
}
