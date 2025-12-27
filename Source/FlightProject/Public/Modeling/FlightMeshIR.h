// FlightMeshIR.h
// Compact intermediate representation for procedural hard-surface mesh generation
// Uses bit-packed descriptors for efficient storage and hashing

#pragma once

#include "CoreMinimal.h"
#include "FlightMeshIR.generated.h"

// Forward declarations
class UDynamicMesh;

/**
 * Primitive types supported by the MeshIR system.
 * Ordered by complexity/common usage for efficient encoding.
 */
UENUM(BlueprintType)
enum class EFlightMeshPrimitive : uint8
{
	Box = 0,
	Sphere,
	Cylinder,
	Capsule,
	Cone,
	Torus,
	Disc,
	// Extended primitives
	RoundedBox,      // Box with rounded edges (future)
	Wedge,           // Triangular prism (future)
	// Keep under 16 for 4-bit encoding
	MAX UMETA(Hidden)
};

/**
 * Boolean/composition operations.
 * Maps to EGeometryScriptBooleanOperation but compact.
 */
UENUM(BlueprintType)
enum class EFlightMeshOp : uint8
{
	Base = 0,        // First primitive, no boolean
	Union,           // Add geometry
	Subtract,        // Remove geometry (A - B)
	Intersect,       // Keep only overlap
	TrimInside,      // Cut without fill, keep outside
	TrimOutside,     // Cut without fill, keep inside
	MAX UMETA(Hidden)
};

/**
 * Origin mode for primitive placement.
 */
UENUM(BlueprintType)
enum class EFlightMeshOrigin : uint8
{
	Center = 0,
	Base,            // Bottom-center
	MAX UMETA(Hidden)
};

/**
 * Flags for operation behavior.
 * Packed into a single byte for efficiency.
 */
USTRUCT(BlueprintType)
struct FFlightMeshOpFlags
{
	GENERATED_BODY()

	/** Fill holes created by boolean operations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	uint8 bFillHoles : 1;

	/** Simplify coplanar triangles after operation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	uint8 bSimplifyOutput : 1;

	/** Preserve UV coordinates during simplification */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	uint8 bPreserveUVs : 1;

	/** Reserved for future use */
	uint8 Reserved : 5;

	FFlightMeshOpFlags()
		: bFillHoles(1)
		, bSimplifyOutput(1)
		, bPreserveUVs(1)
		, Reserved(0)
	{}

	/** Pack flags into a single byte */
	uint8 Pack() const
	{
		return (bFillHoles) | (bSimplifyOutput << 1) | (bPreserveUVs << 2);
	}

	/** Unpack from a single byte */
	static FFlightMeshOpFlags Unpack(uint8 Packed)
	{
		FFlightMeshOpFlags Flags;
		Flags.bFillHoles = Packed & 0x01;
		Flags.bSimplifyOutput = (Packed >> 1) & 0x01;
		Flags.bPreserveUVs = (Packed >> 2) & 0x01;
		return Flags;
	}
};

/**
 * Primitive-specific parameters.
 * Union-like struct - interpretation depends on PrimitiveType.
 *
 * Layout by primitive:
 *   Box:      X=Width, Y=Depth, Z=Height
 *   Sphere:   X=Radius, Y=StepsPhi, Z=StepsTheta
 *   Cylinder: X=Radius, Y=Height, Z=RadialSteps
 *   Capsule:  X=Radius, Y=Length, Z=HemisphereSteps
 *   Cone:     X=BaseRadius, Y=TopRadius, Z=Height (W=RadialSteps via extra)
 *   Torus:    X=MajorRadius, Y=MinorRadius, Z=MajorSteps (W=MinorSteps via extra)
 *   Disc:     X=Radius, Y=AngleSteps, Z=HoleRadius
 */
USTRUCT(BlueprintType)
struct FFlightMeshPrimitiveParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	float X = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	float Y = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	float Z = 100.0f;

	/** Extra parameter for primitives needing 4+ values */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	float W = 0.0f;

	FFlightMeshPrimitiveParams() = default;

	FFlightMeshPrimitiveParams(float InX, float InY, float InZ, float InW = 0.0f)
		: X(InX), Y(InY), Z(InZ), W(InW)
	{}

	/** Create box parameters */
	static FFlightMeshPrimitiveParams Box(float Width, float Depth, float Height)
	{
		return FFlightMeshPrimitiveParams(Width, Depth, Height);
	}

	/** Create sphere parameters */
	static FFlightMeshPrimitiveParams Sphere(float Radius, int32 StepsPhi = 16, int32 StepsTheta = 32)
	{
		return FFlightMeshPrimitiveParams(Radius, static_cast<float>(StepsPhi), static_cast<float>(StepsTheta));
	}

	/** Create cylinder parameters */
	static FFlightMeshPrimitiveParams Cylinder(float Radius, float Height, int32 RadialSteps = 24)
	{
		return FFlightMeshPrimitiveParams(Radius, Height, static_cast<float>(RadialSteps));
	}

	/** Create capsule parameters */
	static FFlightMeshPrimitiveParams Capsule(float Radius, float Length, int32 HemisphereSteps = 8)
	{
		return FFlightMeshPrimitiveParams(Radius, Length, static_cast<float>(HemisphereSteps));
	}

	/** Create cone parameters */
	static FFlightMeshPrimitiveParams Cone(float BaseRadius, float TopRadius, float Height, int32 RadialSteps = 24)
	{
		return FFlightMeshPrimitiveParams(BaseRadius, TopRadius, Height, static_cast<float>(RadialSteps));
	}

	/** Create torus parameters */
	static FFlightMeshPrimitiveParams Torus(float MajorRadius, float MinorRadius, int32 MajorSteps = 24, int32 MinorSteps = 12)
	{
		return FFlightMeshPrimitiveParams(MajorRadius, MinorRadius, static_cast<float>(MajorSteps), static_cast<float>(MinorSteps));
	}

	/** Create disc parameters */
	static FFlightMeshPrimitiveParams Disc(float Radius, int32 AngleSteps = 32, float HoleRadius = 0.0f)
	{
		return FFlightMeshPrimitiveParams(Radius, static_cast<float>(AngleSteps), HoleRadius);
	}
};

/**
 * Single operation descriptor in the IR.
 * Represents either a base primitive or a boolean operation with a tool primitive.
 */
USTRUCT(BlueprintType)
struct FFlightMeshOpDescriptor
{
	GENERATED_BODY()

	/** Type of primitive to create */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	EFlightMeshPrimitive Primitive = EFlightMeshPrimitive::Box;

	/** Boolean operation to perform (Base for first primitive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	EFlightMeshOp Operation = EFlightMeshOp::Base;

	/** Origin mode for primitive placement */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	EFlightMeshOrigin Origin = EFlightMeshOrigin::Center;

	/** Operation flags */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	FFlightMeshOpFlags Flags;

	/** Primitive dimensions/parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	FFlightMeshPrimitiveParams Params;

	/** Transform for this primitive (relative to mesh origin) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	FTransform Transform = FTransform::Identity;

	FFlightMeshOpDescriptor() = default;

	/** Convenience constructor for base primitive */
	static FFlightMeshOpDescriptor MakeBase(
		EFlightMeshPrimitive InPrimitive,
		const FFlightMeshPrimitiveParams& InParams,
		const FTransform& InTransform = FTransform::Identity,
		EFlightMeshOrigin InOrigin = EFlightMeshOrigin::Center)
	{
		FFlightMeshOpDescriptor Desc;
		Desc.Primitive = InPrimitive;
		Desc.Operation = EFlightMeshOp::Base;
		Desc.Origin = InOrigin;
		Desc.Params = InParams;
		Desc.Transform = InTransform;
		return Desc;
	}

	/** Convenience constructor for boolean operation */
	static FFlightMeshOpDescriptor MakeOp(
		EFlightMeshOp InOperation,
		EFlightMeshPrimitive InPrimitive,
		const FFlightMeshPrimitiveParams& InParams,
		const FTransform& InTransform,
		EFlightMeshOrigin InOrigin = EFlightMeshOrigin::Center,
		FFlightMeshOpFlags InFlags = FFlightMeshOpFlags())
	{
		FFlightMeshOpDescriptor Desc;
		Desc.Primitive = InPrimitive;
		Desc.Operation = InOperation;
		Desc.Origin = InOrigin;
		Desc.Flags = InFlags;
		Desc.Params = InParams;
		Desc.Transform = InTransform;
		return Desc;
	}
};

/**
 * Complete Mesh IR - a sequence of operations that define a mesh.
 *
 * The first operation must be EFlightMeshOp::Base.
 * Subsequent operations are boolean compositions onto the accumulated result.
 *
 * Example: A box with a cylindrical hole
 *   Ops[0]: Base, Box(100,100,50)
 *   Ops[1]: Subtract, Cylinder(20,60) at (0,0,0)
 */
USTRUCT(BlueprintType)
struct FFlightMeshIR
{
	GENERATED_BODY()

	/** Sequence of operations defining the mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	TArray<FFlightMeshOpDescriptor> Ops;

	/** Optional name for debugging/identification */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshIR")
	FName Name;

	/** Version for cache invalidation on schema changes */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshIR")
	int32 Version = 1;

	FFlightMeshIR() = default;

	/** Check if IR is valid (has at least one base operation) */
	bool IsValid() const
	{
		return Ops.Num() > 0 && Ops[0].Operation == EFlightMeshOp::Base;
	}

	/** Compute a hash for cache lookup (returns as int64 for Blueprint compat, internally uint64) */
	FLIGHTPROJECT_API int64 ComputeHash() const;

	/** Add a base primitive (must be first operation) */
	FFlightMeshIR& AddBase(
		EFlightMeshPrimitive Primitive,
		const FFlightMeshPrimitiveParams& Params,
		const FTransform& Transform = FTransform::Identity,
		EFlightMeshOrigin Origin = EFlightMeshOrigin::Center)
	{
		check(Ops.Num() == 0); // Base must be first
		Ops.Add(FFlightMeshOpDescriptor::MakeBase(Primitive, Params, Transform, Origin));
		return *this;
	}

	/** Add a boolean operation */
	FFlightMeshIR& AddOp(
		EFlightMeshOp Operation,
		EFlightMeshPrimitive Primitive,
		const FFlightMeshPrimitiveParams& Params,
		const FTransform& Transform,
		EFlightMeshOrigin Origin = EFlightMeshOrigin::Center,
		FFlightMeshOpFlags Flags = FFlightMeshOpFlags())
	{
		check(Ops.Num() > 0); // Must have base first
		check(Operation != EFlightMeshOp::Base); // Can't add another base
		Ops.Add(FFlightMeshOpDescriptor::MakeOp(Operation, Primitive, Params, Transform, Origin, Flags));
		return *this;
	}

	// Convenience methods for common operations

	FFlightMeshIR& Union(
		EFlightMeshPrimitive Primitive,
		const FFlightMeshPrimitiveParams& Params,
		const FTransform& Transform,
		EFlightMeshOrigin Origin = EFlightMeshOrigin::Center)
	{
		return AddOp(EFlightMeshOp::Union, Primitive, Params, Transform, Origin);
	}

	FFlightMeshIR& Subtract(
		EFlightMeshPrimitive Primitive,
		const FFlightMeshPrimitiveParams& Params,
		const FTransform& Transform,
		EFlightMeshOrigin Origin = EFlightMeshOrigin::Center)
	{
		return AddOp(EFlightMeshOp::Subtract, Primitive, Params, Transform, Origin);
	}

	FFlightMeshIR& Intersect(
		EFlightMeshPrimitive Primitive,
		const FFlightMeshPrimitiveParams& Params,
		const FTransform& Transform,
		EFlightMeshOrigin Origin = EFlightMeshOrigin::Center)
	{
		return AddOp(EFlightMeshOp::Intersect, Primitive, Params, Transform, Origin);
	}
};

/**
 * Result of IR evaluation, includes the mesh and metadata.
 */
USTRUCT(BlueprintType)
struct FFlightMeshIRResult
{
	GENERATED_BODY()

	/** The generated mesh (may be null on failure) */
	UPROPERTY(BlueprintReadOnly, Category = "MeshIR")
	TObjectPtr<UDynamicMesh> Mesh = nullptr;

	/** Hash of the IR that generated this mesh */
	UPROPERTY(BlueprintReadOnly, Category = "MeshIR")
	int64 SourceHash = 0;

	/** Number of operations evaluated */
	UPROPERTY(BlueprintReadOnly, Category = "MeshIR")
	int32 OpsEvaluated = 0;

	/** Evaluation time in milliseconds */
	UPROPERTY(BlueprintReadOnly, Category = "MeshIR")
	float EvaluationTimeMs = 0.0f;

	/** Whether evaluation succeeded */
	UPROPERTY(BlueprintReadOnly, Category = "MeshIR")
	bool bSuccess = false;

	/** Error message if evaluation failed */
	UPROPERTY(BlueprintReadOnly, Category = "MeshIR")
	FString ErrorMessage;
};
