// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Vex/FlightVexTypes.h"
#include "FlightRequirementSchema.generated.h"

/**
 * Generic asset contract row.
 * This is the base schema for deterministic "asset must exist + match policy" checks.
 */
USTRUCT(BlueprintType)
struct FFlightAssetRequirementRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName Owner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName RequirementId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString AssetClass = TEXT("Object");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FSoftObjectPath AssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FSoftObjectPath TemplatePath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	TArray<FString> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	TMap<FString, FString> RequiredProperties;
};

/**
 * Niagara-specific requirement contract for system-level validation and synthesis.
 */
USTRUCT(BlueprintType)
struct FFlightNiagaraRequirementRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName Owner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName RequirementId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FSoftObjectPath SystemPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	TArray<FSoftObjectPath> EmitterTemplatePaths;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	TArray<FString> RequiredUserParameters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	TArray<FString> RequiredDataInterfaces;
};

UENUM(BlueprintType)
enum class EFlightCVarValueType : uint8
{
	String,
	Int,
	Float,
	Bool
};

/**
 * CVar policy contract row.
 * Used to validate runtime/build profile CVars through the same manifest pipeline.
 */
USTRUCT(BlueprintType)
struct FFlightCVarRequirementRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName Owner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName RequirementId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName ProfileName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString CVarName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString ExpectedValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightCVarValueType ValueType = EFlightCVarValueType::String;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	float FloatTolerance = 0.001f;
};

/**
 * Plugin policy contract row.
 * Used to validate expected plugin enabled/mounted state for a runtime profile.
 */
USTRUCT(BlueprintType)
struct FFlightPluginRequirementRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName Owner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName RequirementId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName ProfileName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString PluginName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bExpectedEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bExpectedMounted = true;
};

UENUM(BlueprintType)
enum class EFlightVexSymbolValueType : uint8
{
	Float,
	Float2,
	Float3,
	Float4,
	Int,
	Bool
};

UENUM(BlueprintType)
enum class EFlightGpuResourceKind : uint8
{
	Unknown,
	StructuredBuffer,
	StorageBuffer,
	SampledImage,
	StorageImage,
	UniformBuffer,
	ReadbackBuffer,
	StagingBuffer,
	TransientAttachment,
	PersistentTexture
};

UENUM(BlueprintType)
enum class EFlightGpuResourceLifetime : uint8
{
	Transient,
	Persistent,
	ExtractedPersistent,
	ExternalInterop
};

UENUM(BlueprintType)
enum class EFlightGpuExecutionDomain : uint8
{
	Any,
	Cpu,
	RenderGraph,
	GpuCompute,
	ExternalInterop
};

UENUM(BlueprintType)
enum class EFlightGpuAccessClass : uint8
{
	HostRead,
	HostWrite,
	TransferSource,
	TransferDestination,
	ShaderRead,
	ShaderWrite,
	SampledRead,
	ColorAttachmentWrite,
	DepthStencilRead,
	DepthStencilWrite,
	IndirectArgumentRead,
	ExternalSync
};

/**
 * VEX symbol contract row.
 * Defines typed symbol exposure and backend lowering targets for orchestrator compilation.
 */
USTRUCT(BlueprintType)
struct FFlightVexSymbolRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName Owner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName RequirementId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString SymbolName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightVexSymbolValueType ValueType = EFlightVexSymbolValueType::Float;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightVexSymbolResidency Residency = EFlightVexSymbolResidency::Shared;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightVexSymbolAffinity Affinity = EFlightVexSymbolAffinity::Any;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString HlslIdentifier;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString VerseIdentifier;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bWritable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bRequired = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bSimdReadAllowed = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bSimdWriteAllowed = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bGpuTier1Allowed = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightVexAlignmentRequirement AlignmentRequirement = EFlightVexAlignmentRequirement::Any;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightVexMathDeterminismProfile MathDeterminismProfile = EFlightVexMathDeterminismProfile::Fast;
};

USTRUCT(BlueprintType)
struct FFlightGpuResourceContractRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName Owner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName RequirementId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString ResourceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightGpuResourceKind ResourceKind = EFlightGpuResourceKind::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightGpuResourceLifetime ResourceLifetime = EFlightGpuResourceLifetime::Transient;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString ValueTypeName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString HlslStructName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString BindingName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	int32 ElementStrideBytes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	int32 LayoutHash = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bRequired = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bPreferUnrealRdg = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bRequiresRawVulkanInterop = false;
};

USTRUCT(BlueprintType)
struct FFlightGpuAccessRuleRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName Owner = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName RequirementId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FString ResourceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightGpuExecutionDomain ExecutionDomain = EFlightGpuExecutionDomain::Any;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	EFlightGpuAccessClass AccessClass = EFlightGpuAccessClass::ShaderRead;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bRequired = true;
};

/**
 * Render policy contract row.
 * PoC target: runtime/editor profile validation for Lumen/Nanite + key CVars.
 */
USTRUCT(BlueprintType)
struct FFlightRenderProfileRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	FName ProfileName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bEnableLumen = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	bool bEnableNanite = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	TMap<FString, FString> RequiredCVars;
};
