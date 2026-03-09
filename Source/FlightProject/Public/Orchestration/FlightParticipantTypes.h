// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Flight::Orchestration
{

enum class EFlightParticipantKind : uint8
{
	Unknown,
	Service,
	RenderAdapter,
	SpawnAnchor,
	WaypointPath,
	SpatialField,
	BehaviorProvider
};

struct FLIGHTPROJECT_API FFlightParticipantHandle
{
	uint64 Value = 0;

	bool IsValid() const
	{
		return Value != 0;
	}

	friend bool operator==(const FFlightParticipantHandle& Left, const FFlightParticipantHandle& Right)
	{
		return Left.Value == Right.Value;
	}
};

struct FLIGHTPROJECT_API FFlightParticipantRecord
{
	FFlightParticipantHandle Handle;
	EFlightParticipantKind Kind = EFlightParticipantKind::Unknown;
	FName Name = NAME_None;
	FName OwnerSubsystem = NAME_None;
	FString SourceObjectPath;
	TArray<FName> Tags;
	TArray<FName> Capabilities;
	TArray<FName> ContractKeys;
	TWeakObjectPtr<UObject> SourceObject;
};

struct FLIGHTPROJECT_API FFlightCohortRecord
{
	FName Name = NAME_None;
	TArray<FFlightParticipantHandle> Participants;
	TArray<FName> Tags;
};

} // namespace Flight::Orchestration
