#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "GameFramework/GameModeBase.h"
#include "FlightGameMode.generated.h"

class UFlightStartupProfile;

UENUM(BlueprintType, meta = (ScriptName = "FlightStartupProfileType"))
enum class EFlightStartupProfile : uint8
{
    DefaultSandbox UMETA(DisplayName = "Default Sandbox"),
    GauntletGpuSwarm UMETA(DisplayName = "Gauntlet GPU Swarm"),
    LegacyAuto UMETA(DisplayName = "Legacy Auto")
};

UCLASS(Config=Game)
class FLIGHTPROJECT_API AFlightGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AFlightGameMode();

    /**
     * FlightProject keeps GameMode thin on purpose.
     * This surface should choose startup policy and delegate reusable world prep
     * to world subsystems rather than owning environment bootstrap directly.
     */
    virtual void StartPlay() override;

protected:
    /** Optional asset-driven startup policy surface. Falls back to config values if unset or unloadable. */
    UPROPERTY(EditDefaultsOnly, Config, Category = "Flight|Startup")
    TSoftObjectPtr<UFlightStartupProfile> StartupProfileAsset;

    /** Explicit startup policy for this mode. Avoid LegacyAuto for new paths. */
    UPROPERTY(EditDefaultsOnly, Config, Category = "Flight|Startup")
    EFlightStartupProfile StartupProfile = EFlightStartupProfile::DefaultSandbox;

    /** Entity count used by the Gauntlet GPU swarm path. */
    UPROPERTY(EditDefaultsOnly, Config, Category = "Flight|Startup", meta = (ClampMin = "1", UIMin = "1"))
    int32 GauntletGpuSwarmEntityCount = 100000;

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

private:
    EFlightStartupProfile ResolveStartupProfile(const UWorld* World) const;
    int32 ResolveGauntletGpuSwarmEntityCount() const;
    static const TCHAR* StartupProfileToString(EFlightStartupProfile Profile);
};
