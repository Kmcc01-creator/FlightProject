#include "FlightVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputSubsystems.h"
#include "FlightDataSubsystem.h"
#include "FlightDataTypes.h"
#include "FlightGameState.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputMappingContext.h"

AFlightVehiclePawn::AFlightVehiclePawn()
{
    PrimaryActorTick.bCanEverTick = true;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(Root);
    BodyMesh->SetCollisionProfileName(TEXT("Pawn"));

    FlightMovementComponent = CreateDefaultSubobject<UFlightMovementComponent>(TEXT("FlightMovement"));
    FlightMovementComponent->UpdatedComponent = Root;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(Root);
    CameraBoom->TargetArmLength = 800.f;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 8.f;

    ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
    ChaseCamera->SetupAttachment(CameraBoom);

    CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
    CockpitCamera->SetupAttachment(BodyMesh);
    CockpitCamera->SetRelativeLocation(FVector(120.f, 0.f, 80.f));

    NavigationLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("NavigationLight"));
    NavigationLight->SetupAttachment(BodyMesh);
    NavigationLight->SetRelativeLocation(FVector(0.f, 0.f, 250.f));
    NavigationLight->Intensity = 8000.f;
    NavigationLight->AttenuationRadius = 1500.f;
    NavigationLight->LightColor = FColor(150, 200, 255);
    NavigationLight->bUseInverseSquaredFalloff = false;
    NavigationLight->SetMobility(EComponentMobility::Movable);

    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = true;
    bUseControllerRotationRoll = false;
}

void AFlightVehiclePawn::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                if (DefaultInputContext)
                {
                    Subsystem->AddMappingContext(DefaultInputContext, 0);
                }
            }
        }
    }

    if (UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            if (const UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>())
            {
                if (const FFlightAutopilotConfigRow* AutopilotConfig = DataSubsystem->GetAutopilotConfig())
                {
                    ApplyNavigationLightConfig(
                        AutopilotConfig->NavigationLightIntensity,
                        AutopilotConfig->NavigationLightRadius,
                        AutopilotConfig->GetNavigationLightColor(),
                        AutopilotConfig->UseInverseSquaredFalloff(),
                        AutopilotConfig->NavigationLightHeightOffset);
                }
            }
        }
    }
}

void AFlightVehiclePawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ApplyUpdatedInput(DeltaSeconds);

    if (UWorld* World = GetWorld())
    {
        if (AFlightGameState* FlightState = World->GetGameState<AFlightGameState>())
        {
            FlightState->RecordAltitudeSample(GetHeightAboveGroundMeters());
        }
    }
}

void AFlightVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    PlayerInputComponent->BindAxis(TEXT("Fly_Throttle"), this, &AFlightVehiclePawn::ThrottleInput);
    PlayerInputComponent->BindAxis(TEXT("Fly_Pitch"), this, &AFlightVehiclePawn::PitchInput);
    PlayerInputComponent->BindAxis(TEXT("Fly_Yaw"), this, &AFlightVehiclePawn::YawInput);
    PlayerInputComponent->BindAxis(TEXT("Fly_Roll"), this, &AFlightVehiclePawn::RollInput);
    PlayerInputComponent->BindAxis(TEXT("Fly_Climb"), this, &AFlightVehiclePawn::ClimbInput);

    PlayerInputComponent->BindAction(TEXT("Toggle_AI_Override"), IE_Pressed, this, &AFlightVehiclePawn::ToggleAIOverride);
    PlayerInputComponent->BindAction(TEXT("Toggle_Autopilot"), IE_Pressed, this, &AFlightVehiclePawn::ToggleAutopilot);
    PlayerInputComponent->BindAction(TEXT("Emergency_Stop"), IE_Pressed, this, &AFlightVehiclePawn::EmergencyStop);
}

float AFlightVehiclePawn::GetHeightAboveGroundMeters() const
{
    return FlightMovementComponent ? FlightMovementComponent->GetCurrentAltitudeMeters() : 0.f;
}

void AFlightVehiclePawn::ThrottleInput(float Value)
{
    TargetInput.Throttle = FMath::Clamp(Value, -1.f, 1.f);
}

void AFlightVehiclePawn::PitchInput(float Value)
{
    TargetInput.Pitch = FMath::Clamp(Value, -1.f, 1.f);
}

void AFlightVehiclePawn::YawInput(float Value)
{
    TargetInput.Yaw = FMath::Clamp(Value, -1.f, 1.f);
}

void AFlightVehiclePawn::RollInput(float Value)
{
    TargetInput.Roll = FMath::Clamp(Value, -1.f, 1.f);
}

void AFlightVehiclePawn::ClimbInput(float Value)
{
    TargetInput.Climb = FMath::Clamp(Value, -1.f, 1.f);
}

void AFlightVehiclePawn::ToggleAutopilot()
{
    SetAutopilotEnabled(!IsAutopilotEnabled());
}

void AFlightVehiclePawn::ToggleAIOverride()
{
    bAIOverrideEnabled = !bAIOverrideEnabled;
}

void AFlightVehiclePawn::EmergencyStop()
{
    TargetInput = FFlightControlInput();
    AppliedInput = FFlightControlInput();
    if (FlightMovementComponent)
    {
        FlightMovementComponent->StopMovementImmediately();
    }

    SetAutopilotEnabled(false);
}

void AFlightVehiclePawn::ApplyUpdatedInput(float DeltaSeconds)
{
    if (!FlightMovementComponent || bAutopilotEnabled)
    {
        return;
    }

    const float InterpSpeed = FMath::Max(InputSmoothing, 1.f);
    AppliedInput.Throttle = FMath::FInterpTo(AppliedInput.Throttle, TargetInput.Throttle, DeltaSeconds, InterpSpeed);
    AppliedInput.Pitch = FMath::FInterpTo(AppliedInput.Pitch, TargetInput.Pitch, DeltaSeconds, InterpSpeed);
    AppliedInput.Yaw = FMath::FInterpTo(AppliedInput.Yaw, TargetInput.Yaw, DeltaSeconds, InterpSpeed);
    AppliedInput.Roll = FMath::FInterpTo(AppliedInput.Roll, TargetInput.Roll, DeltaSeconds, InterpSpeed);
    AppliedInput.Climb = FMath::FInterpTo(AppliedInput.Climb, TargetInput.Climb, DeltaSeconds, InterpSpeed);

    FlightMovementComponent->SetControlInput(AppliedInput);
}

void AFlightVehiclePawn::SetAutopilotEnabled(bool bEnabled)
{
    bAutopilotEnabled = bEnabled;
    if (bAutopilotEnabled)
    {
        TargetInput = FFlightControlInput();
        AppliedInput = FFlightControlInput();
        if (FlightMovementComponent)
        {
            FlightMovementComponent->StopMovementImmediately();
        }
    }
}

void AFlightVehiclePawn::ApplyNavigationLightConfig(float Intensity, float Radius, const FLinearColor& Color, bool bUseInverseSquared, float HeightOffset)
{
    if (!NavigationLight)
    {
        return;
    }

    NavigationLight->Intensity = Intensity;
    NavigationLight->AttenuationRadius = Radius;
    NavigationLight->SetLightColor(Color);
    NavigationLight->bUseInverseSquaredFalloff = bUseInverseSquared;
    NavigationLight->SetRelativeLocation(FVector(0.f, 0.f, HeightOffset));
}
