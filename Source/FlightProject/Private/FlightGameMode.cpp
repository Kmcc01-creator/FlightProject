#include "FlightGameMode.h"

#include "FlightGameState.h"
#include "FlightHUD.h"
#include "FlightPlayerController.h"
#include "FlightStartupProfile.h"
#include "FlightVehiclePawn.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "FlightScriptingLibrary.h"
#include "Modeling/FlightMeshIRLibrary.h"
#include "Modeling/FlightMeshIR.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/Material.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightGameMode, Log, All);

namespace
{

bool ShouldUseLegacyGauntletPath(const UWorld* World)
{
    if (!World)
    {
        return false;
    }

    const FString MapName = World->GetMapName();
    if (MapName.Contains(TEXT("PersistentFlightTest")))
    {
        return true;
    }

    if (const AWorldSettings* WorldSettings = World->GetWorldSettings())
    {
        return WorldSettings->Tags.Contains(TEXT("GauntletTest"));
    }

    return false;
}

} // namespace

AFlightGameMode::AFlightGameMode()
{
}

int32 AFlightGameMode::ResolveGauntletGpuSwarmEntityCount() const
{
    if (!StartupProfileAsset.IsNull())
    {
        if (const UFlightStartupProfile* StartupProfileObject = StartupProfileAsset.LoadSynchronous())
        {
            return FMath::Max(1, StartupProfileObject->GauntletGpuSwarmEntityCount);
        }
    }

    return FMath::Max(1, GauntletGpuSwarmEntityCount);
}

const TCHAR* AFlightGameMode::StartupProfileToString(const EFlightStartupProfile Profile)
{
    switch (Profile)
    {
    case EFlightStartupProfile::DefaultSandbox:
        return TEXT("DefaultSandbox");
    case EFlightStartupProfile::GauntletGpuSwarm:
        return TEXT("GauntletGpuSwarm");
    case EFlightStartupProfile::LegacyAuto:
        return TEXT("LegacyAuto");
    default:
        return TEXT("Unknown");
    }
}

EFlightStartupProfile AFlightGameMode::ResolveStartupProfile(const UWorld* World) const
{
    if (!StartupProfileAsset.IsNull())
    {
        if (const UFlightStartupProfile* StartupProfileObject = StartupProfileAsset.LoadSynchronous())
        {
            return StartupProfileObject->StartupProfile;
        }

        UE_LOG(
            LogFlightGameMode,
            Warning,
            TEXT("FlightGameMode: Failed to load StartupProfileAsset '%s'; falling back to config."),
            *StartupProfileAsset.ToSoftObjectPath().ToString());
    }

    if (StartupProfile != EFlightStartupProfile::LegacyAuto)
    {
        return StartupProfile;
    }

    const EFlightStartupProfile ResolvedProfile = ShouldUseLegacyGauntletPath(World)
        ? EFlightStartupProfile::GauntletGpuSwarm
        : EFlightStartupProfile::DefaultSandbox;

    UE_LOG(
        LogFlightGameMode,
        Warning,
        TEXT("FlightGameMode: StartupProfile is LegacyAuto; resolved '%s' from legacy map/tag inference. Prefer setting StartupProfile explicitly."),
        StartupProfileToString(ResolvedProfile));

    return ResolvedProfile;
}

void AFlightGameMode::StartPlay()
{
    Super::StartPlay();
    UE_LOG(LogFlightGameMode, Log, TEXT("=== FlightGameMode::StartPlay ==="));

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogFlightGameMode, Error, TEXT("StartPlay: No world available"));
        return;
    }

    const EFlightStartupProfile ActiveStartupProfile = ResolveStartupProfile(World);
    const int32 ActiveGauntletGpuSwarmEntityCount = ResolveGauntletGpuSwarmEntityCount();
    UE_LOG(
        LogFlightGameMode,
        Log,
        TEXT("FlightGameMode: StartupProfile=%s for map '%s' (Asset=%s)"),
        StartupProfileToString(ActiveStartupProfile),
        *World->GetMapName(),
        StartupProfileAsset.IsNull() ? TEXT("None") : *StartupProfileAsset.ToSoftObjectPath().ToString());

    if (ActiveStartupProfile == EFlightStartupProfile::GauntletGpuSwarm)
    {
        UE_LOG(
            LogFlightGameMode,
            Log,
            TEXT("FlightGameMode: Initializing Gauntlet GPU swarm path with %d entities."),
            ActiveGauntletGpuSwarmEntityCount);
        
        // Disable default pawn and HUD spawning for a clean slate
        DefaultPawnClass = nullptr;
        HUDClass = nullptr;

        // Initialize via scripting library for unified execution path
        UFlightScriptingLibrary::InitializeGpuSwarm(this, ActiveGauntletGpuSwarmEntityCount);
    }
    else
    {
        // Step 1: Run reusable world bootstrap.
        if (UFlightWorldBootstrapSubsystem* Bootstrap = World->GetSubsystem<UFlightWorldBootstrapSubsystem>())
        {
            UE_LOG(LogFlightGameMode, Log, TEXT("Running world bootstrap..."));
            Bootstrap->RunBootstrap();
            UE_LOG(LogFlightGameMode, Log, TEXT("World bootstrap complete"));
        }
        else
        {
            UE_LOG(LogFlightGameMode, Warning, TEXT("FlightWorldBootstrapSubsystem not available"));
        }

        // Step 2: Trigger spawn policy for the current startup mode.
        UE_LOG(LogFlightGameMode, Log, TEXT("Spawning initial swarm..."));
        int32 SpawnedCount = UFlightScriptingLibrary::SpawnInitialSwarm(this);
        UE_LOG(LogFlightGameMode, Log, TEXT("Spawned %d swarm entities"), SpawnedCount);

        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
                FString::Printf(TEXT("Flight initialized: %d swarm entities"), SpawnedCount));
        }
    }

    // Summary
    UE_LOG(LogFlightGameMode, Log, TEXT("=== FlightGameMode initialization complete ==="));
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
