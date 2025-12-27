// FlightMeshIRLibrary.h
// Blueprint function library for MeshIR operations

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Modeling/FlightMeshIR.h"
#include "FlightMeshIRLibrary.generated.h"

class UDynamicMesh;
class UFlightMeshIRCache;

/**
 * Blueprint function library for creating and evaluating MeshIR.
 *
 * Provides a convenient API for procedural mesh generation using boolean composition.
 *
 * Example usage in Blueprint:
 *   1. Create an IR with MakeBoxIR
 *   2. Add operations with AddSubtractCylinder, AddUnionBox, etc.
 *   3. Evaluate with EvaluateIR or use cached evaluation with GetOrCreateMesh
 */
UCLASS()
class FLIGHTPROJECT_API UFlightMeshIRLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//-------------------------------------------------------------------------
	// IR Creation - Base Primitives
	//-------------------------------------------------------------------------

	/** Create a new MeshIR with a box as the base primitive */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Create", meta = (DisplayName = "Make Box IR"))
	static FFlightMeshIR MakeBoxIR(
		float Width = 100.0f,
		float Depth = 100.0f,
		float Height = 100.0f,
		FTransform Transform = FTransform());

	/** Create a new MeshIR with a sphere as the base primitive */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Create", meta = (DisplayName = "Make Sphere IR"))
	static FFlightMeshIR MakeSphereIR(
		float Radius = 50.0f,
		int32 Subdivisions = 16,
		FTransform Transform = FTransform());

	/** Create a new MeshIR with a cylinder as the base primitive */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Create", meta = (DisplayName = "Make Cylinder IR"))
	static FFlightMeshIR MakeCylinderIR(
		float Radius = 50.0f,
		float Height = 100.0f,
		int32 RadialSteps = 24,
		FTransform Transform = FTransform());

	/** Create a new MeshIR with a capsule as the base primitive */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Create", meta = (DisplayName = "Make Capsule IR"))
	static FFlightMeshIR MakeCapsuleIR(
		float Radius = 30.0f,
		float Length = 75.0f,
		int32 HemisphereSteps = 8,
		FTransform Transform = FTransform());

	//-------------------------------------------------------------------------
	// IR Modification - Add Operations
	//-------------------------------------------------------------------------

	/** Add a box subtraction to an existing IR */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify", meta = (DisplayName = "Subtract Box"))
	static FFlightMeshIR& SubtractBox(
		UPARAM(ref) FFlightMeshIR& IR,
		float Width,
		float Depth,
		float Height,
		FTransform Transform);

	/** Add a cylinder subtraction to an existing IR */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify", meta = (DisplayName = "Subtract Cylinder"))
	static FFlightMeshIR& SubtractCylinder(
		UPARAM(ref) FFlightMeshIR& IR,
		float Radius,
		float Height,
		FTransform Transform,
		int32 RadialSteps = 24);

	/** Add a sphere subtraction to an existing IR */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify", meta = (DisplayName = "Subtract Sphere"))
	static FFlightMeshIR& SubtractSphere(
		UPARAM(ref) FFlightMeshIR& IR,
		float Radius,
		FTransform Transform,
		int32 Subdivisions = 16);

	/** Add a box union to an existing IR */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify", meta = (DisplayName = "Union Box"))
	static FFlightMeshIR& UnionBox(
		UPARAM(ref) FFlightMeshIR& IR,
		float Width,
		float Depth,
		float Height,
		FTransform Transform);

	/** Add a cylinder union to an existing IR */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify", meta = (DisplayName = "Union Cylinder"))
	static FFlightMeshIR& UnionCylinder(
		UPARAM(ref) FFlightMeshIR& IR,
		float Radius,
		float Height,
		FTransform Transform,
		int32 RadialSteps = 24);

	/** Add a sphere union to an existing IR */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify", meta = (DisplayName = "Union Sphere"))
	static FFlightMeshIR& UnionSphere(
		UPARAM(ref) FFlightMeshIR& IR,
		float Radius,
		FTransform Transform,
		int32 Subdivisions = 16);

	/** Add an intersection with a box to an existing IR */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify", meta = (DisplayName = "Intersect Box"))
	static FFlightMeshIR& IntersectBox(
		UPARAM(ref) FFlightMeshIR& IR,
		float Width,
		float Depth,
		float Height,
		FTransform Transform);

	//-------------------------------------------------------------------------
	// Generic Operation Addition
	//-------------------------------------------------------------------------

	/** Add any primitive operation to an existing IR (advanced) */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Modify|Advanced", meta = (DisplayName = "Add Operation"))
	static FFlightMeshIR& AddOperation(
		UPARAM(ref) FFlightMeshIR& IR,
		EFlightMeshOp Operation,
		EFlightMeshPrimitive Primitive,
		FFlightMeshPrimitiveParams Params,
		FTransform Transform,
		EFlightMeshOrigin Origin = EFlightMeshOrigin::Center);

	//-------------------------------------------------------------------------
	// Evaluation
	//-------------------------------------------------------------------------

	/** Evaluate an IR to produce a dynamic mesh */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Evaluate", meta = (DisplayName = "Evaluate IR"))
	static UDynamicMesh* EvaluateIR(const FFlightMeshIR& IR);

	/** Evaluate an IR with full result details */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Evaluate", meta = (DisplayName = "Evaluate IR (Detailed)"))
	static bool EvaluateIRDetailed(const FFlightMeshIR& IR, FFlightMeshIRResult& OutResult);

	/** Get the hash of an IR (useful for caching) */
	UFUNCTION(BlueprintPure, Category = "MeshIR|Utility", meta = (DisplayName = "Get IR Hash"))
	static int64 GetIRHash(const FFlightMeshIR& IR);

	/** Check if an IR is valid */
	UFUNCTION(BlueprintPure, Category = "MeshIR|Utility", meta = (DisplayName = "Is IR Valid"))
	static bool IsIRValid(const FFlightMeshIR& IR);

	/** Get the number of operations in an IR */
	UFUNCTION(BlueprintPure, Category = "MeshIR|Utility", meta = (DisplayName = "Get Operation Count"))
	static int32 GetOperationCount(const FFlightMeshIR& IR);

	//-------------------------------------------------------------------------
	// Cache Access (uses global cache)
	//-------------------------------------------------------------------------

	/** Get or create a mesh from IR using the global cache */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache", meta = (DisplayName = "Get Or Create Mesh (Cached)"))
	static UDynamicMesh* GetOrCreateMesh(const FFlightMeshIR& IR);

	/** Clear the global MeshIR cache */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache", meta = (DisplayName = "Clear Cache"))
	static void ClearGlobalCache();

	/** Get global cache statistics */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Cache", meta = (DisplayName = "Get Cache Stats"))
	static void GetCacheStats(int32& OutEntryCount, int32& OutHitCount, int32& OutMissCount);

private:
	/** Lazy-init global cache */
	static UFlightMeshIRCache* GetGlobalCache();
};

//-----------------------------------------------------------------------------
// Example Presets - Common Shapes
//-----------------------------------------------------------------------------

/**
 * Library of preset MeshIR shapes for common use cases.
 */
UCLASS()
class FLIGHTPROJECT_API UFlightMeshIRPresets : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Create a box with rounded edges (beveled) - uses subtract operations on corners */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Presets", meta = (DisplayName = "Rounded Box"))
	static FFlightMeshIR MakeRoundedBox(
		float Width = 100.0f,
		float Depth = 100.0f,
		float Height = 100.0f,
		float BevelRadius = 10.0f);

	/** Create a box with a cylindrical hole through it */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Presets", meta = (DisplayName = "Box With Hole"))
	static FFlightMeshIR MakeBoxWithHole(
		float BoxWidth = 100.0f,
		float BoxDepth = 100.0f,
		float BoxHeight = 50.0f,
		float HoleRadius = 20.0f);

	/** Create an L-shaped bracket */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Presets", meta = (DisplayName = "L Bracket"))
	static FFlightMeshIR MakeLBracket(
		float Length = 100.0f,
		float Width = 30.0f,
		float Thickness = 10.0f);

	/** Create a T-shaped pipe junction */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Presets", meta = (DisplayName = "T Junction"))
	static FFlightMeshIR MakeTJunction(
		float MainRadius = 25.0f,
		float MainLength = 100.0f,
		float BranchRadius = 20.0f,
		float BranchLength = 50.0f);

	/** Create a hex nut shape */
	UFUNCTION(BlueprintCallable, Category = "MeshIR|Presets", meta = (DisplayName = "Hex Nut"))
	static FFlightMeshIR MakeHexNut(
		float OuterRadius = 30.0f,
		float InnerRadius = 15.0f,
		float Thickness = 10.0f);
};
