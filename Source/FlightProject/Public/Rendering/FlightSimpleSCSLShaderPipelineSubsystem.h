// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlightSimpleSCSLShaderPipelineSubsystem.generated.h"

namespace Flight::Rendering
{
class FSimpleSCSLSceneViewExtension;
}

USTRUCT(BlueprintType)
struct FLIGHTPROJECT_API FFlightSimpleSCSLShaderPipelineConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rendering")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rendering")
	FLinearColor Tint = FLinearColor(0.92f, 1.0f, 1.08f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rendering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlendWeight = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rendering", meta = (ClampMin = "1.0", ClampMax = "32.0"))
	float PosterizeSteps = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rendering", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float EdgeWeight = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rendering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScanlineWeight = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight|Rendering", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float TimeScale = 1.0f;
};

UCLASS()
class FLIGHTPROJECT_API UFlightSimpleSCSLShaderPipelineSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Flight|Rendering")
	bool IsEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Flight|Rendering")
	FFlightSimpleSCSLShaderPipelineConfig GetConfig() const;

	UFUNCTION(BlueprintCallable, Category = "Flight|Rendering")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Flight|Rendering")
	void SetConfig(const FFlightSimpleSCSLShaderPipelineConfig& InConfig);

	FString BuildStateJson() const;
	FString BuildServiceDetail() const;

private:
	void ApplyConfigToViewExtension();
	void NotifyOrchestrationChanged();

	FFlightSimpleSCSLShaderPipelineConfig Config;
	TSharedPtr<Flight::Rendering::FSimpleSCSLSceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
