#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FlightGameMode.generated.h"

UCLASS()
class FLIGHTPROJECT_API AFlightGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AFlightGameMode();

    virtual void StartPlay() override;

    /**
     * Exec command accessible from the in-game console (e.g., press `~`).
     * Usage: ResetSwarm
     */
    UFUNCTION(Exec)
    void ResetSwarm();

    /**
     * Spawns a drone at the player's view location.
     * Usage: SpawnTestDrone 50 (spawns 50 units ahead)
     */
    UFUNCTION(Exec)
    void SpawnTestDrone(float Distance = 1000.f);

    /**
     * Run MeshIR system tests.
     * Usage: TestMeshIR
     */
    UFUNCTION(Exec)
    void TestMeshIR();

    /**
     * Spawn a visible demo mesh using MeshIR boolean operations.
     * Usage: SpawnMeshIRDemo [type]
     * Types: 0=BoxWithHole, 1=MultiHole, 2=LBracket, 3=HexNut, 4=TJunction
     */
    UFUNCTION(Exec)
    void SpawnMeshIRDemo(int32 DemoType = 0);
};
