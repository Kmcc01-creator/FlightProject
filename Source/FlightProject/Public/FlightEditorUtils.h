#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FlightEditorUtils.generated.h"

/**
 * Utility library for Editor tools and scripts.
 * These functions are exposed to Editor Utility Widgets (EUW) and Blutility.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightEditorUtils : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Example tool function: Generates a debug grid of actors in the world.
     * Useful for testing swarm spatial queries.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|EditorTools", meta = (WorldContext = "WorldContextObject"))
    static void GenerateDebugGrid(const UObject* WorldContextObject, int32 Rows, int32 Cols, float Spacing);

    /**
     * Clears all actors with the 'DebugGrid' tag.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight|EditorTools", meta = (WorldContext = "WorldContextObject"))
    static void ClearDebugGrid(const UObject* WorldContextObject);
    
    /**
     * Returns true if we are currently in an Editor session (not PIE, not Standalone Game).
     */
    UFUNCTION(BlueprintPure, Category = "Flight|EditorTools")
    static bool IsInEditorMode();
};
