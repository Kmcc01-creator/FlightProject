#include "FlightGameMode.h"

#include "FlightGameState.h"
#include "FlightHUD.h"
#include "FlightPlayerController.h"
#include "FlightVehiclePawn.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "FlightScriptingLibrary.h"
#include "Orchestration/FlightStartupCoordinatorSubsystem.h"
#include "Modeling/FlightMeshIRLibrary.h"
#include "Modeling/FlightMeshIR.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/Material.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightGameMode, Log, All);

AFlightGameMode::AFlightGameMode()
{
}

void AFlightGameMode::StartPlay()
{
    Super::StartPlay();
    UE_LOG(LogFlightGameMode, Log, TEXT("=== FlightGameMode::StartPlay ==="));

    RunStartupSequence();

    // Summary
    UE_LOG(LogFlightGameMode, Log, TEXT("=== FlightGameMode initialization complete ==="));
}

void AFlightGameMode::RunStartupSequence()
{
    UE_LOG(LogFlightGameMode, Log, TEXT("=== FlightGameMode::RunStartupSequence ==="));

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("RunStartupSequence: No world available"));
        return;
    }

    UFlightStartupCoordinatorSubsystem* StartupCoordinator = World->GetSubsystem<UFlightStartupCoordinatorSubsystem>();
    if (!StartupCoordinator)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("RunStartupSequence: FlightStartupCoordinatorSubsystem not available"));
        return;
    }

    Flight::Startup::FFlightStartupRequest StartupRequest;
    StartupCoordinator->BuildStartupRequest(*this, StartupRequest);

    UE_LOG(
        LogFlightGameMode,
        Log,
        TEXT("FlightGameMode: StartupProfile=%s for map '%s' (Asset=%s, Source=%s)"),
        *StartupRequest.ActiveProfileName,
        *World->GetMapName(),
        StartupRequest.bProfileAssetConfigured ? *StartupRequest.ProfileAssetPath : TEXT("None"),
        *StartupRequest.ResolutionSource);

    if (StartupRequest.ActiveProfile == EFlightStartupProfile::GauntletGpuSwarm)
    {
        UE_LOG(
            LogFlightGameMode,
            Log,
            TEXT("FlightGameMode: Delegating Gauntlet GPU swarm path with %d entities to startup coordinator."),
            StartupRequest.GauntletGpuSwarmEntityCount);

        DefaultPawnClass = nullptr;
        HUDClass = nullptr;
    }

    const bool bStartupSucceeded = StartupCoordinator->RunStartup(StartupRequest);
    const Flight::Startup::FFlightStartupResult& StartupResult = StartupCoordinator->GetLastResult();
    UE_LOG(
        LogFlightGameMode,
        Log,
        TEXT("FlightGameMode: Startup coordinator completed=%s succeeded=%s summary=%s"),
        StartupResult.bCompleted ? TEXT("true") : TEXT("false"),
        bStartupSucceeded ? TEXT("true") : TEXT("false"),
        *StartupResult.Summary);

    if (GEngine && bStartupSucceeded && StartupRequest.ActiveProfile == EFlightStartupProfile::DefaultSandbox)
    {
        GEngine->AddOnScreenDebugMessage(
            -1,
            5.f,
            FColor::Green,
            FString::Printf(TEXT("Flight initialized: %d swarm entities"), StartupResult.SpawnedSwarmEntities));
    }
}

void AFlightGameMode::ResetSwarm()
{
    UE_LOG(LogTemp, Warning, TEXT("FlightGameMode: Resetting Swarm (Placeholder)"));
    
    // Example logic: Find all pawns of a certain type and destroy them, or reset a simulation subsystem.
    // For now, just logging to prove the Exec command works from the console.
    
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("Swarm Reset Triggered"));
    }
}

void AFlightGameMode::SpawnTestDrone(float Distance)
{
    UE_LOG(LogTemp, Display, TEXT("FlightGameMode: Spawning Test Drone at distance %f"), Distance);

    // Logic to spawn a drone would go here.
    // We would typically use GetWorld()->SpawnActor<...>(...) using the player's camera location.

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Spawn Drone Request: %.2f units"), Distance));
    }
}

void AFlightGameMode::TestMeshIR()
{
    UE_LOG(LogFlightGameMode, Log, TEXT("=== MeshIR System Test ==="));

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Green, TEXT("Running MeshIR Tests..."));
    }

    int32 PassCount = 0;
    int32 FailCount = 0;

    // Test 1: Basic Box Creation
    {
        FFlightMeshIR BoxIR = UFlightMeshIRLibrary::MakeBoxIR(100.f, 100.f, 100.f);
        if (BoxIR.IsValid() && BoxIR.Ops.Num() == 1)
        {
            UE_LOG(LogFlightGameMode, Log, TEXT("  [PASS] Basic box creation"));
            PassCount++;
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("  [FAIL] Basic box creation"));
            FailCount++;
        }
    }

    // Test 2: Box with Hole (Boolean Subtract)
    {
        FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(200.f, 200.f, 100.f);
        UFlightMeshIRLibrary::SubtractCylinder(IR, 50.f, 150.f, FTransform::Identity);

        if (IR.IsValid() && IR.Ops.Num() == 2)
        {
            UE_LOG(LogFlightGameMode, Log, TEXT("  [PASS] Box with hole (2 ops)"));
            PassCount++;
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("  [FAIL] Box with hole"));
            FailCount++;
        }
    }

    // Test 3: Hash Consistency
    {
        FFlightMeshIR IR1 = UFlightMeshIRLibrary::MakeBoxIR(100.f, 100.f, 100.f);
        FFlightMeshIR IR2 = UFlightMeshIRLibrary::MakeBoxIR(100.f, 100.f, 100.f);
        int64 Hash1 = UFlightMeshIRLibrary::GetIRHash(IR1);
        int64 Hash2 = UFlightMeshIRLibrary::GetIRHash(IR2);

        if (Hash1 == Hash2 && Hash1 != 0)
        {
            UE_LOG(LogFlightGameMode, Log, TEXT("  [PASS] Hash consistency (hash=%lld)"), Hash1);
            PassCount++;
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("  [FAIL] Hash consistency (%lld != %lld)"), Hash1, Hash2);
            FailCount++;
        }
    }

    // Test 4: Mesh Evaluation
    {
        FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(100.f, 100.f, 100.f);
        UDynamicMesh* Mesh = UFlightMeshIRLibrary::EvaluateIR(IR);

        if (Mesh != nullptr)
        {
            UE_LOG(LogFlightGameMode, Log, TEXT("  [PASS] Mesh evaluation succeeded"));
            PassCount++;
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("  [FAIL] Mesh evaluation returned null"));
            FailCount++;
        }
    }

    // Test 5: Cache System
    {
        UFlightMeshIRLibrary::ClearGlobalCache();

        FFlightMeshIR IR = UFlightMeshIRLibrary::MakeSphereIR(50.f, 16);

        // First call - cache miss
        UDynamicMesh* Mesh1 = UFlightMeshIRLibrary::GetOrCreateMesh(IR);

        // Second call - should be cache hit
        UDynamicMesh* Mesh2 = UFlightMeshIRLibrary::GetOrCreateMesh(IR);

        int32 EntryCount, HitCount, MissCount;
        UFlightMeshIRLibrary::GetCacheStats(EntryCount, HitCount, MissCount);

        if (Mesh1 != nullptr && Mesh2 == Mesh1 && HitCount == 1 && MissCount == 1)
        {
            UE_LOG(LogFlightGameMode, Log, TEXT("  [PASS] Cache system (entries=%d, hits=%d, misses=%d)"),
                EntryCount, HitCount, MissCount);
            PassCount++;
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("  [FAIL] Cache system (entries=%d, hits=%d, misses=%d)"),
                EntryCount, HitCount, MissCount);
            FailCount++;
        }
    }

    // Test 6: Presets
    {
        FFlightMeshIR HexNut = UFlightMeshIRPresets::MakeHexNut(50.f, 20.f, 30.f);
        if (HexNut.IsValid() && HexNut.Ops.Num() >= 2)
        {
            UE_LOG(LogFlightGameMode, Log, TEXT("  [PASS] HexNut preset (%d ops)"), HexNut.Ops.Num());
            PassCount++;
        }
        else
        {
            UE_LOG(LogFlightGameMode, Error, TEXT("  [FAIL] HexNut preset"));
            FailCount++;
        }
    }

    // Summary
    UE_LOG(LogFlightGameMode, Log, TEXT("=== MeshIR Test Results: %d passed, %d failed ==="), PassCount, FailCount);

    if (GEngine)
    {
        FColor ResultColor = (FailCount == 0) ? FColor::Green : FColor::Red;
        GEngine->AddOnScreenDebugMessage(-1, 10.f, ResultColor,
            FString::Printf(TEXT("MeshIR Tests: %d PASSED, %d FAILED"), PassCount, FailCount));
    }
}

void AFlightGameMode::SpawnMeshIRDemo(int32 DemoType)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("SpawnMeshIRDemo: No world"));
        return;
    }

    // Get spawn location in front of the player
    FVector SpawnLocation = FVector::ZeroVector;
    FRotator SpawnRotation = FRotator::ZeroRotator;

    APlayerController* PC = World->GetFirstPlayerController();
    if (PC && PC->PlayerCameraManager)
    {
        FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
        FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
        SpawnLocation = CamLoc + CamRot.Vector() * 500.f; // 500 units in front
    }
    else
    {
        SpawnLocation = FVector(0, 0, 200);
    }

    // Create IR based on demo type
    FFlightMeshIR IR;
    FString DemoName;

    switch (DemoType)
    {
    case 0: // Box with center hole
        DemoName = TEXT("BoxWithHole");
        IR = UFlightMeshIRLibrary::MakeBoxIR(200.f, 200.f, 100.f);
        UFlightMeshIRLibrary::SubtractCylinder(IR, 50.f, 150.f, FTransform::Identity);
        break;

    case 1: // Box with 4 corner holes
        DemoName = TEXT("MultiHole");
        IR = UFlightMeshIRLibrary::MakeBoxIR(300.f, 300.f, 80.f);
        {
            float Offset = 100.f;
            TArray<FVector> Corners = {
                FVector(Offset, Offset, 0),
                FVector(-Offset, Offset, 0),
                FVector(-Offset, -Offset, 0),
                FVector(Offset, -Offset, 0)
            };
            for (const FVector& Corner : Corners)
            {
                UFlightMeshIRLibrary::SubtractCylinder(IR, 30.f, 120.f, FTransform(Corner));
            }
        }
        break;

    case 2: // L-Bracket
        DemoName = TEXT("LBracket");
        IR = UFlightMeshIRPresets::MakeLBracket(200.f, 80.f, 20.f);
        break;

    case 3: // Hex Nut
        DemoName = TEXT("HexNut");
        IR = UFlightMeshIRPresets::MakeHexNut(100.f, 40.f, 50.f);
        break;

    case 4: // T-Junction pipe
        DemoName = TEXT("TJunction");
        IR = UFlightMeshIRPresets::MakeTJunction(30.f, 200.f, 25.f, 120.f);
        break;

    default:
        DemoName = TEXT("DefaultBox");
        IR = UFlightMeshIRLibrary::MakeBoxIR(100.f, 100.f, 100.f);
        break;
    }

    // Evaluate IR to mesh
    UDynamicMesh* DynMesh = UFlightMeshIRLibrary::GetOrCreateMesh(IR);
    if (!DynMesh)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("SpawnMeshIRDemo: Failed to evaluate mesh"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("MeshIR evaluation failed"));
        }
        return;
    }

    // Spawn an actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* MeshActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
    if (!MeshActor)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("SpawnMeshIRDemo: Failed to spawn actor"));
        return;
    }

    MeshActor->SetActorLabel(FString::Printf(TEXT("MeshIR_%s"), *DemoName));

    // Add root component
    USceneComponent* RootComp = NewObject<USceneComponent>(MeshActor, TEXT("Root"));
    RootComp->SetMobility(EComponentMobility::Movable);
    MeshActor->SetRootComponent(RootComp);
    RootComp->RegisterComponent();

    // Add DynamicMeshComponent
    UDynamicMeshComponent* MeshComp = NewObject<UDynamicMeshComponent>(MeshActor, TEXT("MeshComponent"));
    MeshComp->SetupAttachment(RootComp);
    MeshComp->SetMobility(EComponentMobility::Movable);

    // Copy mesh data
    MeshComp->SetDynamicMesh(DynMesh);

    // Set a basic material
    UMaterial* DefaultMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    if (DefaultMat)
    {
        MeshComp->SetMaterial(0, DefaultMat);
    }

    MeshComp->RegisterComponent();

    UE_LOG(LogFlightGameMode, Log, TEXT("Spawned MeshIR demo: %s at %s (%d ops)"),
        *DemoName, *SpawnLocation.ToString(), IR.Ops.Num());

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan,
            FString::Printf(TEXT("Spawned: %s (%d boolean ops)"), *DemoName, IR.Ops.Num()));
    }
}
