// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Flight::Vex
{

enum class EVexBoundaryDirection : uint8
{
	Import,
	Export
};

enum class EVexBoundaryPayloadKind : uint8
{
	Unknown,
	Value,
	Symbol,
	Context,
	ResourceView,
	Mirror,
	SubmissionHandle
};

struct FLIGHTPROJECT_API FVexBoundaryMetadata
{
	EVexBoundaryDirection Direction = EVexBoundaryDirection::Import;
	EVexBoundaryPayloadKind PayloadKind = EVexBoundaryPayloadKind::Unknown;
	FString SourceContractKey;
	FString DestinationContractKey;
	bool bAwaitable = false;
	bool bMirrorRequested = false;
};

} // namespace Flight::Vex
