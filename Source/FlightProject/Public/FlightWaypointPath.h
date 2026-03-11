#pragma once

#include "CoreMinimal.h"
#include "Core/FlightActorAdapter.h"
#include "Orchestration/FlightParticipantAdapter.h"
#include "FlightWaypointPath.generated.h"

class USplineComponent;
struct FFlightAutopilotConfigRow;

/**
 * Authorable spline used to drive autonomous flight paths.
 */
UCLASS()
class FLIGHTPROJECT_API AFlightWaypointPath : public AFlightActorAdapterBase, public IFlightParticipantAdapter
{
    GENERATED_BODY()

public:
    AFlightWaypointPath();

    USplineComponent* GetSplineComponent() const { return FlightSpline; }

    FGuid GetPathId() const { return PathId; }
    FGuid EnsureRegisteredPath();
    FName GetNavNetworkId() const { return NavNetworkId; }
    FName GetNavSubNetworkId() const { return NavSubNetworkId; }

    float GetPathLength() const;
    FTransform GetTransformAtDistance(float Distance) const;
    FVector GetLocationAtNormalizedPosition(float Alpha) const;

    void EnsureDefaultLoop();
    void ConfigureFromAutopilotConfig(const FFlightAutopilotConfigRow& Config);
    void SetNavigationRoutingMetadata(FName InNavNetworkId, FName InNavSubNetworkId);
    virtual bool BuildParticipantRecord(Flight::Orchestration::FFlightParticipantRecord& OutRecord) const override;

private:
    UPROPERTY(VisibleAnywhere, Category = "Flight|Path")
    USplineComponent* FlightSpline;

    UPROPERTY(EditAnywhere, Category = "Flight|Path")
    float DefaultRadius = 3500.f;

    UPROPERTY(EditAnywhere, Category = "Flight|Path")
    float DefaultAltitude = 1200.f;

    UPROPERTY(EditAnywhere, Category = "Flight|Path|Navigation")
    FName NavNetworkId = NAME_None;

    UPROPERTY(EditAnywhere, Category = "Flight|Path|Navigation")
    FName NavSubNetworkId = NAME_None;

    UPROPERTY(Transient, VisibleAnywhere, Category = "Flight|Path")
    FGuid PathId;

    virtual void OnAdapterConstruction(const FTransform& Transform) override;
    virtual void OnAdapterRegistered() override;
    virtual void OnAdapterUnregistered(const EEndPlayReason::Type EndPlayReason) override;

    void BuildDefaultLoop();
    void BuildLoop(float Radius, float Altitude);
    void RefreshRegisteredPath();
};
