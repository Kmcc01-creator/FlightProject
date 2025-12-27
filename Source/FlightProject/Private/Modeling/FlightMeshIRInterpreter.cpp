// FlightMeshIRInterpreter.cpp

#include "Modeling/FlightMeshIRInterpreter.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/GeometryScriptTypes.h"

//-----------------------------------------------------------------------------
// UFlightMeshIRInterpreter
//-----------------------------------------------------------------------------

bool UFlightMeshIRInterpreter::Evaluate(const FFlightMeshIR& IR, FFlightMeshIRResult& OutResult)
{
	OutResult = FFlightMeshIRResult();

	// Validate IR
	if (!IR.IsValid())
	{
		OutResult.ErrorMessage = TEXT("Invalid IR: must have at least one Base operation");
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();

	// Create the result mesh
	UDynamicMesh* ResultMesh = NewObject<UDynamicMesh>();
	if (!ResultMesh)
	{
		OutResult.ErrorMessage = TEXT("Failed to create result mesh");
		return false;
	}

	// Evaluate first operation (must be Base)
	const FFlightMeshOpDescriptor& BaseOp = IR.Ops[0];
	if (!CreatePrimitive(BaseOp, ResultMesh))
	{
		OutResult.ErrorMessage = FString::Printf(TEXT("Failed to create base primitive (type %d)"),
			static_cast<int32>(BaseOp.Primitive));
		return false;
	}
	OutResult.OpsEvaluated = 1;

	// Apply subsequent boolean operations
	for (int32 i = 1; i < IR.Ops.Num(); ++i)
	{
		const FFlightMeshOpDescriptor& Op = IR.Ops[i];

		// Create tool mesh for this operation
		UDynamicMesh* ToolMesh = NewObject<UDynamicMesh>();
		if (!ToolMesh)
		{
			OutResult.ErrorMessage = FString::Printf(TEXT("Failed to create tool mesh for op %d"), i);
			return false;
		}

		if (!CreatePrimitive(Op, ToolMesh))
		{
			OutResult.ErrorMessage = FString::Printf(TEXT("Failed to create tool primitive for op %d (type %d)"),
				i, static_cast<int32>(Op.Primitive));
			return false;
		}

		// Apply boolean
		ResultMesh = ApplyBoolean(ResultMesh, ToolMesh, Op);
		if (!ResultMesh)
		{
			OutResult.ErrorMessage = FString::Printf(TEXT("Boolean operation failed at op %d"), i);
			return false;
		}

		OutResult.OpsEvaluated++;
	}

	const double EndTime = FPlatformTime::Seconds();

	OutResult.Mesh = ResultMesh;
	OutResult.SourceHash = IR.ComputeHash();
	OutResult.EvaluationTimeMs = static_cast<float>((EndTime - StartTime) * 1000.0);
	OutResult.bSuccess = true;

	return true;
}

UDynamicMesh* UFlightMeshIRInterpreter::EvaluateToMesh(const FFlightMeshIR& IR)
{
	FFlightMeshIRResult Result;
	if (Evaluate(IR, Result))
	{
		return Result.Mesh;
	}

	UE_LOG(LogTemp, Warning, TEXT("MeshIR evaluation failed: %s"), *Result.ErrorMessage);
	return nullptr;
}

bool UFlightMeshIRInterpreter::CreatePrimitive(
	const FFlightMeshOpDescriptor& Desc,
	UDynamicMesh* OutMesh)
{
	if (!OutMesh)
	{
		return false;
	}

	// Common primitive options
	FGeometryScriptPrimitiveOptions PrimOptions;
	PrimOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
	PrimOptions.bFlipOrientation = false;
	PrimOptions.UVMode = EGeometryScriptPrimitiveUVMode::Uniform;

	const EGeometryScriptPrimitiveOriginMode Origin = ToGeometryScriptOrigin(Desc.Origin);
	const FFlightMeshPrimitiveParams& P = Desc.Params;
	const FTransform& T = Desc.Transform;

	switch (Desc.Primitive)
	{
	case EFlightMeshPrimitive::Box:
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			OutMesh, PrimOptions, T,
			P.X, P.Y, P.Z,  // DimensionX, DimensionY, DimensionZ
			0, 0, 0,        // Steps (0 = no subdivision)
			Origin);
		break;

	case EFlightMeshPrimitive::Sphere:
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
			OutMesh, PrimOptions, T,
			P.X,                           // Radius
			FMath::Max(4, static_cast<int32>(P.Y)),  // StepsPhi
			FMath::Max(4, static_cast<int32>(P.Z)),  // StepsTheta
			Origin);
		break;

	case EFlightMeshPrimitive::Cylinder:
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
			OutMesh, PrimOptions, T,
			P.X,                           // Radius
			P.Y,                           // Height
			FMath::Max(8, static_cast<int32>(P.Z)),  // RadialSteps
			0,                             // HeightSteps
			true,                          // bCapped
			Origin);
		break;

	case EFlightMeshPrimitive::Capsule:
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCapsule(
			OutMesh, PrimOptions, T,
			P.X,                           // Radius
			P.Y,                           // LineLength
			FMath::Max(3, static_cast<int32>(P.Z)),  // HemisphereSteps
			FMath::Max(8, static_cast<int32>(P.W > 0 ? P.W : 16)),  // CircleSteps
			0,                             // SegmentSteps
			Origin);
		break;

	case EFlightMeshPrimitive::Cone:
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(
			OutMesh, PrimOptions, T,
			P.X,                           // BaseRadius
			P.Y,                           // TopRadius
			P.Z,                           // Height
			FMath::Max(8, static_cast<int32>(P.W > 0 ? P.W : 24)),  // RadialSteps
			4,                             // HeightSteps
			true,                          // bCapped
			Origin);
		break;

	case EFlightMeshPrimitive::Torus:
		{
			FGeometryScriptRevolveOptions RevolveOptions;
			RevolveOptions.RevolveDegrees = 360.0f;
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTorus(
				OutMesh, PrimOptions, T,
				RevolveOptions,
				P.X,                           // MajorRadius
				P.Y,                           // MinorRadius
				FMath::Max(8, static_cast<int32>(P.Z)),  // MajorSteps
				FMath::Max(4, static_cast<int32>(P.W > 0 ? P.W : 8)),  // MinorSteps
				Origin);
		}
		break;

	case EFlightMeshPrimitive::Disc:
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendDisc(
			OutMesh, PrimOptions, T,
			P.X,                           // Radius
			FMath::Max(8, static_cast<int32>(P.Y)),  // AngleSteps
			0,                             // SpokeSteps
			0.0f,                          // StartAngle
			360.0f,                        // EndAngle
			P.Z);                          // HoleRadius
		break;

	case EFlightMeshPrimitive::RoundedBox:
		// TODO: Implement rounded box (box + edge bevels)
		// For now, fall back to regular box
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			OutMesh, PrimOptions, T,
			P.X, P.Y, P.Z,
			0, 0, 0,
			Origin);
		UE_LOG(LogTemp, Warning, TEXT("RoundedBox not yet implemented, using Box"));
		break;

	case EFlightMeshPrimitive::Wedge:
		// TODO: Implement wedge (triangular prism)
		// For now, fall back to box
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			OutMesh, PrimOptions, T,
			P.X, P.Y, P.Z,
			0, 0, 0,
			Origin);
		UE_LOG(LogTemp, Warning, TEXT("Wedge not yet implemented, using Box"));
		break;

	default:
		UE_LOG(LogTemp, Error, TEXT("Unknown primitive type: %d"), static_cast<int32>(Desc.Primitive));
		return false;
	}

	return true;
}

UDynamicMesh* UFlightMeshIRInterpreter::ApplyBoolean(
	UDynamicMesh* TargetMesh,
	UDynamicMesh* ToolMesh,
	const FFlightMeshOpDescriptor& Desc,
	UGeometryScriptDebug* Debug)
{
	if (!TargetMesh || !ToolMesh)
	{
		return nullptr;
	}

	// Skip if operation is Base (shouldn't happen, but be defensive)
	if (Desc.Operation == EFlightMeshOp::Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyBoolean called with Base operation, skipping"));
		return TargetMesh;
	}

	// Configure boolean options from flags
	FGeometryScriptMeshBooleanOptions Options;
	Options.bFillHoles = Desc.Flags.bFillHoles;
	Options.bSimplifyOutput = Desc.Flags.bSimplifyOutput;
	Options.SimplifyPlanarTolerance = 0.01f;
	Options.bAllowEmptyResult = false;
	Options.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;

	const EGeometryScriptBooleanOperation GeomOp = ToGeometryScriptOp(Desc.Operation);

	// Apply the boolean
	// Note: Tool transform is baked into the tool mesh during CreatePrimitive,
	// so we use Identity transforms here
	return UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		TargetMesh,
		FTransform::Identity,
		ToolMesh,
		FTransform::Identity,  // Transform already applied in CreatePrimitive
		GeomOp,
		Options,
		Debug);
}

EGeometryScriptBooleanOperation UFlightMeshIRInterpreter::ToGeometryScriptOp(EFlightMeshOp Op)
{
	switch (Op)
	{
	case EFlightMeshOp::Union:
		return EGeometryScriptBooleanOperation::Union;
	case EFlightMeshOp::Subtract:
		return EGeometryScriptBooleanOperation::Subtract;
	case EFlightMeshOp::Intersect:
		return EGeometryScriptBooleanOperation::Intersection;
	case EFlightMeshOp::TrimInside:
		return EGeometryScriptBooleanOperation::TrimInside;
	case EFlightMeshOp::TrimOutside:
		return EGeometryScriptBooleanOperation::TrimOutside;
	default:
		UE_LOG(LogTemp, Warning, TEXT("Unknown MeshOp %d, defaulting to Union"), static_cast<int32>(Op));
		return EGeometryScriptBooleanOperation::Union;
	}
}

EGeometryScriptPrimitiveOriginMode UFlightMeshIRInterpreter::ToGeometryScriptOrigin(EFlightMeshOrigin Origin)
{
	switch (Origin)
	{
	case EFlightMeshOrigin::Center:
		return EGeometryScriptPrimitiveOriginMode::Center;
	case EFlightMeshOrigin::Base:
		return EGeometryScriptPrimitiveOriginMode::Base;
	default:
		return EGeometryScriptPrimitiveOriginMode::Center;
	}
}

//-----------------------------------------------------------------------------
// UFlightMeshIRCache
//-----------------------------------------------------------------------------

UFlightMeshIRCache::UFlightMeshIRCache()
{
}

UDynamicMesh* UFlightMeshIRCache::GetOrCreate(const FFlightMeshIR& IR)
{
	const int64 Hash = IR.ComputeHash();

	// Check cache first (read lock)
	{
		FScopeLock Lock(&CacheLock);

		if (TObjectPtr<UDynamicMesh>* Found = Cache.Find(Hash))
		{
			if (UDynamicMesh* Mesh = Found->Get())
			{
				HitCount++;
				return Mesh;
			}
			// Stale entry, will recreate
		}
	}

	// Cache miss - evaluate
	MissCount++;

	FFlightMeshIRResult Result;
	if (!UFlightMeshIRInterpreter::Evaluate(IR, Result))
	{
		UE_LOG(LogTemp, Warning, TEXT("MeshIR cache miss and evaluation failed: %s"), *Result.ErrorMessage);
		return nullptr;
	}

	// Store in cache (write lock)
	{
		FScopeLock Lock(&CacheLock);
		Cache.Add(Hash, Result.Mesh);
	}

	UE_LOG(LogTemp, Verbose, TEXT("MeshIR cache miss, evaluated %d ops in %.2fms (hash: %lld)"),
		Result.OpsEvaluated, Result.EvaluationTimeMs, Hash);

	return Result.Mesh;
}

bool UFlightMeshIRCache::IsCached(const FFlightMeshIR& IR) const
{
	const int64 Hash = IR.ComputeHash();

	FScopeLock Lock(&CacheLock);

	if (const TObjectPtr<UDynamicMesh>* Found = Cache.Find(Hash))
	{
		return Found->Get() != nullptr;
	}

	return false;
}

void UFlightMeshIRCache::CacheResult(const FFlightMeshIR& IR, UDynamicMesh* Mesh)
{
	if (!Mesh)
	{
		return;
	}

	const int64 Hash = IR.ComputeHash();

	FScopeLock Lock(&CacheLock);
	Cache.Add(Hash, Mesh);
}

void UFlightMeshIRCache::ClearCache()
{
	FScopeLock Lock(&CacheLock);
	Cache.Empty();
	HitCount = 0;
	MissCount = 0;
}

bool UFlightMeshIRCache::Invalidate(const FFlightMeshIR& IR)
{
	const int64 Hash = IR.ComputeHash();

	FScopeLock Lock(&CacheLock);
	return Cache.Remove(Hash) > 0;
}

void UFlightMeshIRCache::GetStats(int32& OutEntryCount, int32& OutHitCount, int32& OutMissCount) const
{
	FScopeLock Lock(&CacheLock);
	OutEntryCount = Cache.Num();
	OutHitCount = HitCount;
	OutMissCount = MissCount;
}
