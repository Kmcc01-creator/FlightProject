// FlightMeshIRInterpreter.h
// Evaluates MeshIR using UE GeometryScripting functions

#pragma once

#include "CoreMinimal.h"
#include "Modeling/FlightMeshIR.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "FlightMeshIRInterpreter.generated.h"

class UDynamicMesh;
class UGeometryScriptDebug;

/**
 * Interpreter for MeshIR - converts IR sequences to UDynamicMesh.
 *
 * Uses UE's GeometryScripting library under the hood:
 * - MeshPrimitiveFunctions for primitive generation
 * - MeshBooleanFunctions for boolean operations
 *
 * This class is stateless - all state lives in the IR and output mesh.
 */
UCLASS(BlueprintType)
class FLIGHTPROJECT_API UFlightMeshIRInterpreter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Evaluate a MeshIR to produce a dynamic mesh.
	 *
	 * @param IR The intermediate representation to evaluate
	 * @param OutResult Result struct with mesh, timing, and status
	 * @return true if evaluation succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR")
	static bool Evaluate(const FFlightMeshIR& IR, FFlightMeshIRResult& OutResult);

	/**
	 * Evaluate a MeshIR and return just the mesh (simpler API).
	 *
	 * @param IR The intermediate representation to evaluate
	 * @return The generated mesh, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR")
	static UDynamicMesh* EvaluateToMesh(const FFlightMeshIR& IR);

	/**
	 * Evaluate a single operation descriptor to create a primitive mesh.
	 * Useful for testing or when you need just the primitive without booleans.
	 *
	 * @param Desc The operation descriptor (only primitive/params/transform used)
	 * @param OutMesh The mesh to append the primitive to
	 * @return true if primitive was created successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR")
	static bool CreatePrimitive(
		const FFlightMeshOpDescriptor& Desc,
		UDynamicMesh* OutMesh);

protected:
	/**
	 * Apply a boolean operation between accumulated result and a tool mesh.
	 *
	 * @param TargetMesh The accumulated result mesh (modified in place conceptually, new mesh returned)
	 * @param ToolMesh The tool mesh for the boolean
	 * @param Desc The operation descriptor with operation type and flags
	 * @param Debug Optional debug output
	 * @return The result mesh (may be same as target or new allocation)
	 */
	static UDynamicMesh* ApplyBoolean(
		UDynamicMesh* TargetMesh,
		UDynamicMesh* ToolMesh,
		const FFlightMeshOpDescriptor& Desc,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	 * Convert our operation enum to GeometryScript's enum.
	 */
	static EGeometryScriptBooleanOperation ToGeometryScriptOp(EFlightMeshOp Op);

	/**
	 * Convert our origin enum to GeometryScript's enum.
	 */
	static EGeometryScriptPrimitiveOriginMode ToGeometryScriptOrigin(EFlightMeshOrigin Origin);
};

/**
 * Cache for evaluated MeshIR results.
 *
 * Uses hash-based lookup to avoid re-evaluating identical IR sequences.
 * Thread-safe for concurrent reads, serializes writes.
 */
UCLASS(BlueprintType)
class FLIGHTPROJECT_API UFlightMeshIRCache : public UObject
{
	GENERATED_BODY()

public:
	UFlightMeshIRCache();

	/**
	 * Get or create a mesh for the given IR.
	 * Returns cached version if available, otherwise evaluates and caches.
	 *
	 * @param IR The intermediate representation
	 * @return The mesh (cached or newly created)
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache")
	UDynamicMesh* GetOrCreate(const FFlightMeshIR& IR);

	/**
	 * Check if a mesh for this IR is already cached.
	 *
	 * @param IR The intermediate representation
	 * @return true if cached mesh exists and is valid
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache")
	bool IsCached(const FFlightMeshIR& IR) const;

	/**
	 * Explicitly cache an evaluated result.
	 *
	 * @param IR The intermediate representation
	 * @param Mesh The mesh to cache
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache")
	void CacheResult(const FFlightMeshIR& IR, UDynamicMesh* Mesh);

	/**
	 * Clear all cached meshes.
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache")
	void ClearCache();

	/**
	 * Remove a specific entry from cache.
	 *
	 * @param IR The intermediate representation to remove
	 * @return true if entry was found and removed
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache")
	bool Invalidate(const FFlightMeshIR& IR);

	/**
	 * Get cache statistics.
	 *
	 * @param OutEntryCount Number of entries in cache
	 * @param OutHitCount Number of cache hits since creation
	 * @param OutMissCount Number of cache misses since creation
	 */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache")
	void GetStats(int32& OutEntryCount, int32& OutHitCount, int32& OutMissCount) const;

protected:
	/** Hash -> Cached mesh */
	UPROPERTY()
	TMap<int64, TObjectPtr<UDynamicMesh>> Cache;

	/** Statistics */
	mutable int32 HitCount = 0;
	mutable int32 MissCount = 0;

	/** Thread safety */
	mutable FCriticalSection CacheLock;
};
