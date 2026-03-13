// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vex/FlightVexSchema.h"

namespace Flight::VerseRuntime
{

uint32 ResolveRuntimeValueStride(const Flight::Vex::FVexSymbolRecord& Symbol);

Flight::Vex::FVexRuntimeValue ReadRuntimeValueFromAddress(
	const uint8* Bytes,
	Flight::Vex::EVexValueType ValueType,
	uint32 ElementStride);

bool WriteRuntimeValueToAddress(
	uint8* Bytes,
	Flight::Vex::EVexValueType ValueType,
	uint32 ElementStride,
	const Flight::Vex::FVexRuntimeValue& Value);

} // namespace Flight::VerseRuntime
