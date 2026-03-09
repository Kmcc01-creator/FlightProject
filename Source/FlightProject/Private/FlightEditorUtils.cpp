#include "FlightEditorUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"

void UFlightEditorUtils::GenerateDebugGrid(const UObject* WorldContextObject, int32 Rows, int32 Cols, float Spacing)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World) return;

    const FVector StartLoc = FVector::ZeroVector;

    for (int32 i = 0; i < Rows; ++i)
    {
        for (int32 j = 0; j < Cols; ++j)
        {
            FVector SpawnLoc = StartLoc + FVector(i * Spacing, j * Spacing, 500.0f);
            
            FActorSpawnParameters SpawnParams;
            SpawnParams.Name = *FString::Printf(TEXT("DebugActor_%d_%d"), i, j);
            
            // Spawn a generic empty actor for visualization
            AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLoc, FRotator::ZeroRotator, SpawnParams);
            if (NewActor)
            {
                NewActor->Tags.Add(FName("DebugGrid"));
                NewActor->SetActorLabel(SpawnParams.Name.ToString());
                
                // Make it selectable/visible
                // (In a real tool, we might add a billboard component or mesh here)
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Generated %d debug actors."), Rows * Cols);
}

void UFlightEditorUtils::ClearDebugGrid(const UObject* WorldContextObject)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World) return;

    TArray<AActor*> ActorsToDestroy;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor && Actor->ActorHasTag(FName("DebugGrid")))
        {
            ActorsToDestroy.Add(Actor);
        }
    }

    for (AActor* Actor : ActorsToDestroy)
    {
        World->DestroyActor(Actor);
    }

    UE_LOG(LogTemp, Log, TEXT("Cleared %d debug actors."), ActorsToDestroy.Num());
}

bool UFlightEditorUtils::IsInEditorMode()
{
#if WITH_EDITOR
    return GIsEditor && !GIsPlayInEditorWorld;
#else
    return false;
#endif
}
