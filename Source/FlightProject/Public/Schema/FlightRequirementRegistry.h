#pragma once

#include "CoreMinimal.h"
#include "Schema/FlightRequirementSchema.h"

namespace Flight::Schema
{

struct FManifestData
{
	TArray<FFlightAssetRequirementRow> AssetRequirements;
	TArray<FFlightNiagaraRequirementRow> NiagaraRequirements;
	TArray<FFlightCVarRequirementRow> CVarRequirements;
	TArray<FFlightPluginRequirementRow> PluginRequirements;
	TArray<FFlightVexSymbolRow> VexSymbolRequirements;
	TArray<FFlightRenderProfileRow> RenderProfiles;
};

/** Build the current code-first requirement manifest. */
FManifestData BuildManifestData();

/** Serialize the current manifest to JSON. */
FString BuildManifestJson();

} // namespace Flight::Schema
