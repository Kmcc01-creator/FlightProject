#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "FlightMovementComponent.generated.h"

USTRUCT(BlueprintType)
struct FFlightControlInput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight")
    float Throttle = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight")
    float Pitch = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight")
    float Yaw = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight")
    float Roll = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight")
    float Climb = 0.f;
};

UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class FLIGHTPROJECT_API UFlightMovementComponent : public UPawnMovementComponent
{
    GENERATED_BODY()

public:
    UFlightMovementComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void SetControlInput(const FFlightControlInput& NewInput);
    float GetCurrentAltitudeMeters() const;
    FVector GetVelocity() const { return CurrentVelocity; }

protected:
    UPROPERTY(EditAnywhere, Category = "Flight")
    float MaxSpeed = 3000.f;

    UPROPERTY(EditAnywhere, Category = "Flight")
    float Acceleration = 6000.f;

    UPROPERTY(EditAnywhere, Category = "Flight")
    float LiftCoefficient = 0.6f;

    UPROPERTY(EditAnywhere, Category = "Flight")
    float DragCoefficient = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Flight")
    float ClimbRate = 1500.f;

    UPROPERTY(EditAnywhere, Category = "Flight")
    float TurnRate = 45.f;

private:
    void UpdateFlightModel(float DeltaTime);
    void ApplySteering(float DeltaTime);
    void UpdateAltitudeCache();

    FFlightControlInput ControlInput;
    FVector CurrentVelocity = FVector::ZeroVector;
    float CachedAltitude = 0.f;
    float GroundTraceDistance = 50000.f;
};
