#include "FlightMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"

UFlightMovementComponent::UFlightMovementComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bAutoActivate = true;
}

void UFlightMovementComponent::BeginPlay()
{
    Super::BeginPlay();
    UpdateAltitudeCache();
}

void UFlightMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!PawnOwner || !UpdatedComponent)
    {
        return;
    }

    UpdateFlightModel(DeltaTime);
    ApplySteering(DeltaTime);
    UpdateAltitudeCache();

    const FVector MoveDelta = CurrentVelocity * DeltaTime;
    FHitResult Hit;
    SafeMoveUpdatedComponent(MoveDelta, UpdatedComponent->GetComponentQuat(), true, Hit);

    if (Hit.IsValidBlockingHit())
    {
        SlideAlongSurface(MoveDelta, 1.f - Hit.Time, Hit.Normal, Hit);
    }
}

void UFlightMovementComponent::SetControlInput(const FFlightControlInput& NewInput)
{
    ControlInput = NewInput;
}

float UFlightMovementComponent::GetCurrentAltitudeMeters() const
{
    return CachedAltitude * 0.01f; // convert from cm to meters
}

void UFlightMovementComponent::UpdateFlightModel(float DeltaTime)
{
    const FVector Forward = UpdatedComponent ? UpdatedComponent->GetForwardVector() : FVector::ForwardVector;
    const FVector Up = UpdatedComponent ? UpdatedComponent->GetUpVector() : FVector::UpVector;

    const float TargetSpeed = FMath::Clamp(ControlInput.Throttle, -1.f, 1.f) * MaxSpeed;
    const float CurrentSpeed = FVector::DotProduct(CurrentVelocity, Forward);
    const float SpeedDifference = TargetSpeed - CurrentSpeed;

    const float Accel = Acceleration * DeltaTime;
    const float AppliedAccel = FMath::Clamp(SpeedDifference, -Accel, Accel);
    CurrentVelocity += Forward * AppliedAccel;

    const float SpeedSq = CurrentVelocity.SizeSquared();
    const float LiftForce = LiftCoefficient * SpeedSq;
    CurrentVelocity += Up * (LiftForce * DeltaTime);

    const FVector DragForce = -CurrentVelocity.GetSafeNormal() * (CurrentVelocity.SizeSquared() * DragCoefficient * DeltaTime);
    CurrentVelocity += DragForce;

    const float ClimbInput = FMath::Clamp(ControlInput.Climb, -1.f, 1.f);
    CurrentVelocity += Up * (ClimbRate * ClimbInput * DeltaTime);

    CurrentVelocity = CurrentVelocity.GetClampedToMaxSize(MaxSpeed);
}

void UFlightMovementComponent::ApplySteering(float DeltaTime)
{
    if (!UpdatedComponent)
    {
        return;
    }

    const float PitchInput = FMath::Clamp(ControlInput.Pitch, -1.f, 1.f);
    const float YawInput = FMath::Clamp(ControlInput.Yaw, -1.f, 1.f);
    const float RollInput = FMath::Clamp(ControlInput.Roll, -1.f, 1.f);

    const float DeltaPitch = PitchInput * TurnRate * DeltaTime;
    const float DeltaYaw = YawInput * TurnRate * DeltaTime;
    const float DeltaRoll = RollInput * TurnRate * DeltaTime;

    const FRotator DeltaRot(DeltaPitch, DeltaYaw, DeltaRoll);
    UpdatedComponent->AddLocalRotation(DeltaRot.Quaternion());
}

void UFlightMovementComponent::UpdateAltitudeCache()
{
    CachedAltitude = 0.f;

    if (!PawnOwner)
    {
        return;
    }

    const FVector Start = PawnOwner->GetActorLocation();
    const FVector End = Start - FVector::UpVector * GroundTraceDistance;

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FlightAltitudeTrace), true, PawnOwner);

    if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
    {
        CachedAltitude = (Start - Hit.Location).Size();
    }
    else
    {
        CachedAltitude = Start.Z;
    }
}
