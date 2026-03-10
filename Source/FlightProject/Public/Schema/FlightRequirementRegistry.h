#pragma once

#include "CoreMinimal.h"
#include "Schema/FlightRequirementSchema.h"

namespace Flight::Schema
{

struct FFlightGpuStructuredBufferContract
{
	FName Owner = NAME_None;
	FName RequirementId = NAME_None;
	FString ResourceId;
	FString BindingName;
	uint32 ElementStrideBytes = 0;
	uint32 LayoutHash = 0;
	EFlightGpuResourceKind ResourceKind = EFlightGpuResourceKind::Unknown;
	EFlightGpuResourceLifetime ResourceLifetime = EFlightGpuResourceLifetime::Transient;
	bool bPreferUnrealRdg = true;
	bool bRequiresRawVulkanInterop = false;
	bool bSupportsTransferSource = false;
	bool bSupportsTransferDestination = false;
	bool bSupportsShaderRead = false;
	bool bSupportsShaderWrite = false;

	bool IsValid() const
	{
		return !ResourceId.IsEmpty() && ElementStrideBytes > 0;
	}
};

struct FManifestData
{
	TArray<FFlightAssetRequirementRow> AssetRequirements;
	TArray<FFlightNiagaraRequirementRow> NiagaraRequirements;
	TArray<FFlightCVarRequirementRow> CVarRequirements;
	TArray<FFlightPluginRequirementRow> PluginRequirements;
	TArray<FFlightVexSymbolRow> VexSymbolRequirements;
	TArray<FFlightGpuResourceContractRow> GpuResourceContracts;
	TArray<FFlightGpuAccessRuleRow> GpuAccessRules;
	TArray<FFlightRenderProfileRow> RenderProfiles;
};

/** Build the current code-first requirement manifest. */
FManifestData BuildManifestData();

/** Serialize the current manifest to JSON. */
FString BuildManifestJson();

/** Build the shared generated GPU resource contract shader include for project shaders. */
FString BuildGeneratedGpuResourceContractHlsl();

/** Resolve a structured-buffer-style GPU contract by logical resource id. */
TOptional<FFlightGpuStructuredBufferContract> ResolveStructuredBufferContract(const FString& ResourceId);

/** Validate a structured buffer contract against the native runtime type shape. */
TArray<FString> ValidateStructuredBufferContract(const FString& ResourceId, uint32 NativeStrideBytes, uint32 NativeLayoutHash);

} // namespace Flight::Schema
