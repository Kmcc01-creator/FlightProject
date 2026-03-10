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
};

} // namespace Flight::Vex
