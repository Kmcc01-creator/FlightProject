#include "FlightPlayerController.h"

#include "Engine/Engine.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

AFlightPlayerController::AFlightPlayerController()
{
    bShowMouseCursor = false;
    bEnableClickEvents = false;
    bEnableTouchEvents = false;
}

void AFlightPlayerController::BeginPlay()
{
    Super::BeginPlay();

    SetInputMode(FInputModeGameOnly());

    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            if (PlayerMappingContext)
            {
                Subsystem->AddMappingContext(PlayerMappingContext, 0);
            }
        }
    }
}

void AFlightPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    InputComponent->BindAction(TEXT("Mass_Profiler_Snapshot"), IE_Pressed, this, &AFlightPlayerController::SnapshotMassProfiler);
    InputComponent->BindAction(TEXT("Mass_Debugger_Toggle"), IE_Pressed, this, &AFlightPlayerController::ToggleMassDebugger);
}

void AFlightPlayerController::ToggleMassDebugger()
{
    if (UWorld* World = GetWorld())
    {
        GEngine->Exec(World, TEXT("MassDebugger.Toggle"));
    }
}

void AFlightPlayerController::SnapshotMassProfiler()
{
    if (UWorld* World = GetWorld())
    {
        GEngine->Exec(World, TEXT("MassDebugger.TakeSnapshot"));
    }
}
