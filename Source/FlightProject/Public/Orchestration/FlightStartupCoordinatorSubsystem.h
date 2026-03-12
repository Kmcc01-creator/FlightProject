// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FlightGameMode.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightStartupCoordinatorSubsystem.generated.h"

class AFlightGameMode;
namespace Flight::Orchestration
{
struct FFlightStartupProfileReport;
}

namespace Flight::Startup
{

struct FLIGHTPROJECT_API FFlightStartupRequest
{
	EFlightStartupProfile ActiveProfile;
	FString ActiveProfileName;
	FString ResolutionSource;
	FString ProfileAssetPath;
	bool bProfileAssetConfigured = false;
	bool bProfileAssetLoaded = false;
	bool bResolvedFromLegacyAuto = false;
	int32 GauntletGpuSwarmEntityCount = 0;
	FString Detail;
};

struct FLIGHTPROJECT_API FFlightStartupStageReport
{
	FName StageName = NAME_None;
	bool bSucceeded = false;
	bool bCommitted = false;
	FString Detail;
};

struct FLIGHTPROJECT_API FFlightStartupResult
{
	bool bCompleted = false;
	bool bSucceeded = false;
	FDateTime StartedAtUtc;
	FDateTime CompletedAtUtc;
	FName FailureStage = NAME_None;
	int32 SpawnedSwarmEntities = 0;
	FString Summary;
	TArray<FFlightStartupStageReport> Stages;
};

} // namespace Flight::Startup

/**
 * Coordinates startup transactions that span bootstrap, orchestration, spawn,
 * and post-spawn reconciliation without forcing that cross-system sequence into
 * GameMode policy code or bootstrap/orchestration internals.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightStartupCoordinatorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void BuildStartupRequest(const AFlightGameMode& GameMode, Flight::Startup::FFlightStartupRequest& OutRequest) const;
	void BuildStartupReport(Flight::Orchestration::FFlightStartupProfileReport& OutReport) const;
	bool RunStartup(const Flight::Startup::FFlightStartupRequest& Request);
	const Flight::Startup::FFlightStartupRequest& GetLastRequest() const { return LastRequest; }
	const Flight::Startup::FFlightStartupResult& GetLastResult() const { return LastResult; }
	bool HasCompletedStartup() const { return LastResult.bCompleted; }

private:
	static const TCHAR* StartupProfileToString(EFlightStartupProfile Profile);
	const AFlightGameMode* FindFlightGameMode() const;
	EFlightStartupProfile ResolveStartupProfile(const AFlightGameMode& GameMode, const UWorld* World) const;
	int32 ResolveGauntletGpuSwarmEntityCount(const AFlightGameMode& GameMode) const;
	bool RunDefaultSandboxStartup(const Flight::Startup::FFlightStartupRequest& Request);
	bool RunGauntletGpuSwarmStartup(const Flight::Startup::FFlightStartupRequest& Request);

	void ResetLastStartup(const Flight::Startup::FFlightStartupRequest& Request);
	void CompleteSuccess(const FString& Summary);
	void CompleteFailure(FName FailureStage, const FString& Summary);
	void RecordStage(FName StageName, bool bSucceeded, bool bCommitted, FString Detail);

	Flight::Startup::FFlightStartupRequest LastRequest;
	Flight::Startup::FFlightStartupResult LastResult;
};
