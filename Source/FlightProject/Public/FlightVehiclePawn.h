#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FlightMovementComponent.h"
#include "FlightVehiclePawn.generated.h"

class UPointLightComponent;
class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UInputMappingContext;

UCLASS(Blueprintable)
class FLIGHTPROJECT_API AFlightVehiclePawn : public APawn
{
    GENERATED_BODY()

public:
    AFlightVehiclePawn();

    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    UFlightMovementComponent* GetFlightMovementComponent() const { return FlightMovementComponent; }
    float GetHeightAboveGroundMeters() const;

protected:
    virtual void BeginPlay() override;

    void ThrottleInput(float Value);
    void PitchInput(float Value);
    void YawInput(float Value);
    void RollInput(float Value);
    void ClimbInput(float Value);

    void ToggleAutopilot();
    void ToggleAIOverride();
    void EmergencyStop();

    void ApplyUpdatedInput(float DeltaSeconds);
    void SetAutopilotEnabled(bool bEnabled);
    bool IsAutopilotEnabled() const { return bAutopilotEnabled; }
    void ApplyNavigationLightConfig(float Intensity, float Radius, const FLinearColor& Color, bool bUseInverseSquared, float HeightOffset);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight")
    USceneComponent* Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight")
    UStaticMeshComponent* BodyMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight")
    USpringArmComponent* CameraBoom;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight")
    UCameraComponent* CockpitCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight")
    UCameraComponent* ChaseCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flight")
    UPointLightComponent* NavigationLight;

    UPROPERTY(EditDefaultsOnly, Category = "Flight|Input")
    TObjectPtr<UInputMappingContext> DefaultInputContext;

    UPROPERTY(EditAnywhere, Category = "Flight|Input")
    float InputSmoothing = 5.f;

private:
    UPROPERTY(VisibleAnywhere, Category = "Flight")
    UFlightMovementComponent* FlightMovementComponent;

    FFlightControlInput TargetInput;
    FFlightControlInput AppliedInput;
    bool bAutopilotEnabled = false;
    bool bAIOverrideEnabled = false;
};
