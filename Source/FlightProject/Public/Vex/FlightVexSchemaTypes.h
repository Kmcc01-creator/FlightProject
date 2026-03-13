// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexTypes.h"

namespace Flight::Vex
{

enum class EVexStorageKind : uint8
{
	None,
	AosOffset,
	Accessor,
	MassFragmentField,
	SoaColumn,
	GpuBufferElement,
	ExternalProvider
};

enum class EVexVectorStorageClass : uint8
{
	None,
	ScalarOnly,
	AosPacked,
	SoaContiguous,
	MassFragmentColumn,
	GpuBufferColumn,
	GatherScatterFallback
};

struct FVexTypeId
{
	const void* RuntimeKey = nullptr;
	FName StableName = NAME_None;

	bool IsValid() const
	{
		return RuntimeKey != nullptr || StableName != NAME_None;
	}
};

struct FVexBackendBinding
{
	FString HlslIdentifier;
	FString VerseIdentifier;
};

struct FVexStorageBinding
{
	EVexStorageKind Kind = EVexStorageKind::None;
	int32 MemberOffset = INDEX_NONE;
	uint32 ElementStride = 0;
	FName FragmentType = NAME_None;
	FString BufferBinding;

	bool HasDirectOffset() const
	{
		return Kind == EVexStorageKind::AosOffset && MemberOffset != INDEX_NONE;
	}
};

struct FVexVectorPackContract
{
	EVexVectorStorageClass StorageClass = EVexVectorStorageClass::None;
	EFlightVexAlignmentRequirement MinAlignment = EFlightVexAlignmentRequirement::Any;
	uint32 NaturalLaneStrideBytes = 0;
	bool bContiguousLaneAccess = false;
	bool bSupportsGather = false;
	bool bSupportsScatter = false;
	bool bSupportsDirectSimdRead = false;
	bool bSupportsDirectSimdWrite = false;
	bool bSupportsGpuColumnRead = false;
	bool bSupportsGpuColumnWrite = false;
	TArray<uint8> PreferredPackWidths;
};

inline FString VexVectorStorageClassToString(const EVexVectorStorageClass StorageClass)
{
	switch (StorageClass)
	{
	case EVexVectorStorageClass::ScalarOnly:
		return TEXT("ScalarOnly");
	case EVexVectorStorageClass::AosPacked:
		return TEXT("AosPacked");
	case EVexVectorStorageClass::SoaContiguous:
		return TEXT("SoaContiguous");
	case EVexVectorStorageClass::MassFragmentColumn:
		return TEXT("MassFragmentColumn");
	case EVexVectorStorageClass::GpuBufferColumn:
		return TEXT("GpuBufferColumn");
	case EVexVectorStorageClass::GatherScatterFallback:
		return TEXT("GatherScatterFallback");
	case EVexVectorStorageClass::None:
	default:
		return TEXT("None");
	}
}

inline FVexVectorPackContract BuildDefaultVectorPackContract(
	const FVexStorageBinding& Storage,
	const EVexValueType ValueType,
	const EFlightVexAlignmentRequirement AlignmentRequirement,
	const bool bSimdReadAllowed,
	const bool bSimdWriteAllowed,
	const bool bGpuTier1Allowed)
{
	FVexVectorPackContract Contract;
	Contract.MinAlignment = AlignmentRequirement;
	Contract.NaturalLaneStrideBytes = Storage.ElementStride;
	Contract.PreferredPackWidths = {4, 8};

	switch (Storage.Kind)
	{
	case EVexStorageKind::AosOffset:
		Contract.StorageClass = EVexVectorStorageClass::AosPacked;
		Contract.bContiguousLaneAccess = true;
		Contract.bSupportsDirectSimdRead = ValueType == EVexValueType::Float && bSimdReadAllowed;
		Contract.bSupportsDirectSimdWrite = ValueType == EVexValueType::Float && bSimdWriteAllowed;
		break;
	case EVexStorageKind::MassFragmentField:
		Contract.StorageClass = EVexVectorStorageClass::MassFragmentColumn;
		Contract.bContiguousLaneAccess = true;
		Contract.bSupportsDirectSimdRead = ValueType == EVexValueType::Float && bSimdReadAllowed;
		Contract.bSupportsDirectSimdWrite = ValueType == EVexValueType::Float && bSimdWriteAllowed;
		break;
	case EVexStorageKind::SoaColumn:
		Contract.StorageClass = EVexVectorStorageClass::SoaContiguous;
		Contract.bContiguousLaneAccess = true;
		Contract.bSupportsGpuColumnRead = bGpuTier1Allowed;
		Contract.bSupportsGpuColumnWrite = bGpuTier1Allowed;
		break;
	case EVexStorageKind::GpuBufferElement:
		Contract.StorageClass = EVexVectorStorageClass::GpuBufferColumn;
		Contract.bContiguousLaneAccess = true;
		Contract.bSupportsGpuColumnRead = bGpuTier1Allowed;
		Contract.bSupportsGpuColumnWrite = bGpuTier1Allowed;
		break;
	case EVexStorageKind::Accessor:
	case EVexStorageKind::ExternalProvider:
	case EVexStorageKind::None:
	default:
		Contract.StorageClass = (bSimdReadAllowed || bSimdWriteAllowed || bGpuTier1Allowed)
			? EVexVectorStorageClass::GatherScatterFallback
			: EVexVectorStorageClass::ScalarOnly;
		Contract.bSupportsGather = bSimdReadAllowed;
		Contract.bSupportsScatter = bSimdWriteAllowed;
		break;
	}

	return Contract;
}

struct FVexLogicalSymbolSchema
{
	FString SymbolName;
	EVexValueType ValueType = EVexValueType::Unknown;
	bool bReadable = true;
	bool bWritable = true;
	bool bRequired = false;
	bool bSimdReadAllowed = true;
	bool bSimdWriteAllowed = true;
	bool bGpuTier1Allowed = true;
	EFlightVexSymbolResidency Residency = EFlightVexSymbolResidency::Shared;
	EFlightVexSymbolAffinity Affinity = EFlightVexSymbolAffinity::Any;
	EFlightVexAlignmentRequirement AlignmentRequirement = EFlightVexAlignmentRequirement::Any;
	EFlightVexMathDeterminismProfile MathDeterminismProfile = EFlightVexMathDeterminismProfile::Fast;
	FVexBackendBinding BackendBinding;
	FVexStorageBinding Storage;
	FVexVectorPackContract VectorPack;
};

} // namespace Flight::Vex
