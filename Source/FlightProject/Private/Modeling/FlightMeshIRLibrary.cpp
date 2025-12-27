// FlightMeshIRLibrary.cpp

#include "Modeling/FlightMeshIRLibrary.h"
#include "Modeling/FlightMeshIRInterpreter.h"
#include "UDynamicMesh.h"

//-----------------------------------------------------------------------------
// Global Cache Singleton
//-----------------------------------------------------------------------------

static TWeakObjectPtr<UFlightMeshIRCache> GGlobalMeshIRCache;

UFlightMeshIRCache* UFlightMeshIRLibrary::GetGlobalCache()
{
	if (!GGlobalMeshIRCache.IsValid())
	{
		// Create as a root object to prevent GC
		GGlobalMeshIRCache = NewObject<UFlightMeshIRCache>();
		GGlobalMeshIRCache->AddToRoot();
	}
	return GGlobalMeshIRCache.Get();
}

//-----------------------------------------------------------------------------
// IR Creation - Base Primitives
//-----------------------------------------------------------------------------

FFlightMeshIR UFlightMeshIRLibrary::MakeBoxIR(
	float Width,
	float Depth,
	float Height,
	FTransform Transform)
{
	FFlightMeshIR IR;
	IR.AddBase(
		EFlightMeshPrimitive::Box,
		FFlightMeshPrimitiveParams::Box(Width, Depth, Height),
		Transform,
		EFlightMeshOrigin::Center);
	return IR;
}

FFlightMeshIR UFlightMeshIRLibrary::MakeSphereIR(
	float Radius,
	int32 Subdivisions,
	FTransform Transform)
{
	FFlightMeshIR IR;
	IR.AddBase(
		EFlightMeshPrimitive::Sphere,
		FFlightMeshPrimitiveParams::Sphere(Radius, Subdivisions, Subdivisions * 2),
		Transform,
		EFlightMeshOrigin::Center);
	return IR;
}

FFlightMeshIR UFlightMeshIRLibrary::MakeCylinderIR(
	float Radius,
	float Height,
	int32 RadialSteps,
	FTransform Transform)
{
	FFlightMeshIR IR;
	IR.AddBase(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(Radius, Height, RadialSteps),
		Transform,
		EFlightMeshOrigin::Center);
	return IR;
}

FFlightMeshIR UFlightMeshIRLibrary::MakeCapsuleIR(
	float Radius,
	float Length,
	int32 HemisphereSteps,
	FTransform Transform)
{
	FFlightMeshIR IR;
	IR.AddBase(
		EFlightMeshPrimitive::Capsule,
		FFlightMeshPrimitiveParams::Capsule(Radius, Length, HemisphereSteps),
		Transform,
		EFlightMeshOrigin::Center);
	return IR;
}

//-----------------------------------------------------------------------------
// IR Modification - Subtract Operations
//-----------------------------------------------------------------------------

FFlightMeshIR& UFlightMeshIRLibrary::SubtractBox(
	FFlightMeshIR& IR,
	float Width,
	float Depth,
	float Height,
	FTransform Transform)
{
	IR.Subtract(
		EFlightMeshPrimitive::Box,
		FFlightMeshPrimitiveParams::Box(Width, Depth, Height),
		Transform);
	return IR;
}

FFlightMeshIR& UFlightMeshIRLibrary::SubtractCylinder(
	FFlightMeshIR& IR,
	float Radius,
	float Height,
	FTransform Transform,
	int32 RadialSteps)
{
	IR.Subtract(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(Radius, Height, RadialSteps),
		Transform);
	return IR;
}

FFlightMeshIR& UFlightMeshIRLibrary::SubtractSphere(
	FFlightMeshIR& IR,
	float Radius,
	FTransform Transform,
	int32 Subdivisions)
{
	IR.Subtract(
		EFlightMeshPrimitive::Sphere,
		FFlightMeshPrimitiveParams::Sphere(Radius, Subdivisions, Subdivisions * 2),
		Transform);
	return IR;
}

//-----------------------------------------------------------------------------
// IR Modification - Union Operations
//-----------------------------------------------------------------------------

FFlightMeshIR& UFlightMeshIRLibrary::UnionBox(
	FFlightMeshIR& IR,
	float Width,
	float Depth,
	float Height,
	FTransform Transform)
{
	IR.Union(
		EFlightMeshPrimitive::Box,
		FFlightMeshPrimitiveParams::Box(Width, Depth, Height),
		Transform);
	return IR;
}

FFlightMeshIR& UFlightMeshIRLibrary::UnionCylinder(
	FFlightMeshIR& IR,
	float Radius,
	float Height,
	FTransform Transform,
	int32 RadialSteps)
{
	IR.Union(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(Radius, Height, RadialSteps),
		Transform);
	return IR;
}

FFlightMeshIR& UFlightMeshIRLibrary::UnionSphere(
	FFlightMeshIR& IR,
	float Radius,
	FTransform Transform,
	int32 Subdivisions)
{
	IR.Union(
		EFlightMeshPrimitive::Sphere,
		FFlightMeshPrimitiveParams::Sphere(Radius, Subdivisions, Subdivisions * 2),
		Transform);
	return IR;
}

//-----------------------------------------------------------------------------
// IR Modification - Intersect Operations
//-----------------------------------------------------------------------------

FFlightMeshIR& UFlightMeshIRLibrary::IntersectBox(
	FFlightMeshIR& IR,
	float Width,
	float Depth,
	float Height,
	FTransform Transform)
{
	IR.Intersect(
		EFlightMeshPrimitive::Box,
		FFlightMeshPrimitiveParams::Box(Width, Depth, Height),
		Transform);
	return IR;
}

//-----------------------------------------------------------------------------
// Generic Operation Addition
//-----------------------------------------------------------------------------

FFlightMeshIR& UFlightMeshIRLibrary::AddOperation(
	FFlightMeshIR& IR,
	EFlightMeshOp Operation,
	EFlightMeshPrimitive Primitive,
	FFlightMeshPrimitiveParams Params,
	FTransform Transform,
	EFlightMeshOrigin Origin)
{
	if (Operation == EFlightMeshOp::Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("AddOperation called with Base - use MakeXxxIR instead"));
		return IR;
	}

	IR.AddOp(Operation, Primitive, Params, Transform, Origin);
	return IR;
}

//-----------------------------------------------------------------------------
// Evaluation
//-----------------------------------------------------------------------------

UDynamicMesh* UFlightMeshIRLibrary::EvaluateIR(const FFlightMeshIR& IR)
{
	return UFlightMeshIRInterpreter::EvaluateToMesh(IR);
}

bool UFlightMeshIRLibrary::EvaluateIRDetailed(const FFlightMeshIR& IR, FFlightMeshIRResult& OutResult)
{
	return UFlightMeshIRInterpreter::Evaluate(IR, OutResult);
}

int64 UFlightMeshIRLibrary::GetIRHash(const FFlightMeshIR& IR)
{
	return static_cast<int64>(IR.ComputeHash());
}

bool UFlightMeshIRLibrary::IsIRValid(const FFlightMeshIR& IR)
{
	return IR.IsValid();
}

int32 UFlightMeshIRLibrary::GetOperationCount(const FFlightMeshIR& IR)
{
	return IR.Ops.Num();
}

//-----------------------------------------------------------------------------
// Cache Access
//-----------------------------------------------------------------------------

UDynamicMesh* UFlightMeshIRLibrary::GetOrCreateMesh(const FFlightMeshIR& IR)
{
	UFlightMeshIRCache* Cache = GetGlobalCache();
	if (Cache)
	{
		return Cache->GetOrCreate(IR);
	}
	// Fallback to direct evaluation
	return EvaluateIR(IR);
}

void UFlightMeshIRLibrary::ClearGlobalCache()
{
	UFlightMeshIRCache* Cache = GetGlobalCache();
	if (Cache)
	{
		Cache->ClearCache();
	}
}

void UFlightMeshIRLibrary::GetCacheStats(int32& OutEntryCount, int32& OutHitCount, int32& OutMissCount)
{
	UFlightMeshIRCache* Cache = GetGlobalCache();
	if (Cache)
	{
		Cache->GetStats(OutEntryCount, OutHitCount, OutMissCount);
	}
	else
	{
		OutEntryCount = 0;
		OutHitCount = 0;
		OutMissCount = 0;
	}
}

//-----------------------------------------------------------------------------
// UFlightMeshIRPresets
//-----------------------------------------------------------------------------

FFlightMeshIR UFlightMeshIRPresets::MakeRoundedBox(
	float Width,
	float Depth,
	float Height,
	float BevelRadius)
{
	// Start with a box
	FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(Width, Depth, Height);

	// Subtract cylinders from each edge to create bevels
	// This is a simplified approximation - proper rounded box would need more complex geometry

	const float HalfW = Width * 0.5f;
	const float HalfD = Depth * 0.5f;
	const float HalfH = Height * 0.5f;

	// Vertical edge bevels (4 corners)
	const float EdgeLength = Height + BevelRadius * 2.0f; // Extend past box

	// Corner positions
	TArray<FVector> Corners = {
		FVector(HalfW, HalfD, 0.0f),
		FVector(-HalfW, HalfD, 0.0f),
		FVector(-HalfW, -HalfD, 0.0f),
		FVector(HalfW, -HalfD, 0.0f)
	};

	// For each corner, subtract a box that creates the bevel effect
	// Using box intersection at 45 degrees for edge chamfer
	for (const FVector& Corner : Corners)
	{
		// Rotate a box 45 degrees and position at corner
		FTransform CornerTransform;
		CornerTransform.SetLocation(Corner);
		CornerTransform.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0f)));

		IR.Subtract(
			EFlightMeshPrimitive::Box,
			FFlightMeshPrimitiveParams::Box(BevelRadius * 1.5f, BevelRadius * 1.5f, EdgeLength),
			CornerTransform);
	}

	return IR;
}

FFlightMeshIR UFlightMeshIRPresets::MakeBoxWithHole(
	float BoxWidth,
	float BoxDepth,
	float BoxHeight,
	float HoleRadius)
{
	FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(BoxWidth, BoxDepth, BoxHeight);

	// Cylinder through the center, along Z axis
	// Make it longer than the box to ensure clean cut
	const float CylinderHeight = BoxHeight * 1.5f;

	IR.Subtract(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(HoleRadius, CylinderHeight, 32),
		FTransform::Identity);

	return IR;
}

FFlightMeshIR UFlightMeshIRPresets::MakeLBracket(
	float Length,
	float Width,
	float Thickness)
{
	// Vertical part
	FFlightMeshIR IR = UFlightMeshIRLibrary::MakeBoxIR(
		Width, Thickness, Length,
		FTransform(FVector(0.0f, 0.0f, Length * 0.5f)));

	// Horizontal part (base)
	FTransform BaseTransform;
	BaseTransform.SetLocation(FVector(0.0f, (Length - Thickness) * 0.5f, Thickness * 0.5f));

	IR.Union(
		EFlightMeshPrimitive::Box,
		FFlightMeshPrimitiveParams::Box(Width, Length, Thickness),
		BaseTransform);

	return IR;
}

FFlightMeshIR UFlightMeshIRPresets::MakeTJunction(
	float MainRadius,
	float MainLength,
	float BranchRadius,
	float BranchLength)
{
	// Main cylinder along X axis
	FTransform MainTransform;
	MainTransform.SetRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(90.0f)));

	FFlightMeshIR IR;
	IR.AddBase(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(MainRadius, MainLength, 32),
		MainTransform,
		EFlightMeshOrigin::Center);

	// Branch cylinder along Z axis
	FTransform BranchTransform;
	BranchTransform.SetLocation(FVector(0.0f, 0.0f, BranchLength * 0.5f));

	IR.Union(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(BranchRadius, BranchLength, 32),
		BranchTransform,
		EFlightMeshOrigin::Base);

	return IR;
}

FFlightMeshIR UFlightMeshIRPresets::MakeHexNut(
	float OuterRadius,
	float InnerRadius,
	float Thickness)
{
	// Create hex shape using intersection of rotated boxes
	// A hexagon can be created by intersecting 3 rectangles at 60 degree intervals

	// Start with a cylinder as base (will be trimmed to hex)
	FFlightMeshIR IR;
	IR.AddBase(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(OuterRadius, Thickness, 6), // 6 sides = hexagon!
		FTransform::Identity,
		EFlightMeshOrigin::Center);

	// Subtract center hole
	IR.Subtract(
		EFlightMeshPrimitive::Cylinder,
		FFlightMeshPrimitiveParams::Cylinder(InnerRadius, Thickness * 1.5f, 32),
		FTransform::Identity);

	return IR;
}
