#include "Schema/FlightRequirementRegistry.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Core/FlightHlslReflection.h"
#include "Swarm/SwarmSimulationTypes.h"

namespace Flight::Schema
{

namespace
{

EFlightCVarValueType InferValueType(const FString& InValue)
{
	const FString Value = InValue.TrimStartAndEnd();
	if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		return EFlightCVarValueType::Bool;
	}

	int32 IntValue = 0;
	if (LexTryParseString(IntValue, *Value))
	{
		return EFlightCVarValueType::Int;
	}

	float FloatValue = 0.0f;
	if (LexTryParseString(FloatValue, *Value))
	{
		return EFlightCVarValueType::Float;
	}

	return EFlightCVarValueType::String;
}

FString CVarValueTypeToString(const EFlightCVarValueType Type)
{
	switch (Type)
	{
	case EFlightCVarValueType::String:
		return TEXT("String");
	case EFlightCVarValueType::Int:
		return TEXT("Int");
	case EFlightCVarValueType::Float:
		return TEXT("Float");
	case EFlightCVarValueType::Bool:
		return TEXT("Bool");
	default:
		return TEXT("Unknown");
	}
}

FString VexValueTypeToString(const EFlightVexSymbolValueType Type)
{
	switch (Type)
	{
	case EFlightVexSymbolValueType::Float:
		return TEXT("float");
	case EFlightVexSymbolValueType::Float2:
		return TEXT("float2");
	case EFlightVexSymbolValueType::Float3:
		return TEXT("float3");
	case EFlightVexSymbolValueType::Float4:
		return TEXT("float4");
	case EFlightVexSymbolValueType::Int:
		return TEXT("int");
	case EFlightVexSymbolValueType::Bool:
		return TEXT("bool");
	default:
		return TEXT("unknown");
	}
}

FString VexResidencyToString(const EFlightVexSymbolResidency Residency)
{
	switch (Residency)
	{
	case EFlightVexSymbolResidency::Shared:
		return TEXT("Shared");
	case EFlightVexSymbolResidency::GpuOnly:
		return TEXT("GpuOnly");
	case EFlightVexSymbolResidency::CpuOnly:
		return TEXT("CpuOnly");
	default:
		return TEXT("Unknown");
	}
}

FString VexAffinityToString(const EFlightVexSymbolAffinity Affinity)
{
	switch (Affinity)
	{
	case EFlightVexSymbolAffinity::Any:
		return TEXT("Any");
	case EFlightVexSymbolAffinity::GameThread:
		return TEXT("GameThread");
	case EFlightVexSymbolAffinity::RenderThread:
		return TEXT("RenderThread");
	case EFlightVexSymbolAffinity::WorkerThread:
		return TEXT("WorkerThread");
	default:
		return TEXT("Unknown");
	}
}

void AddSwarmNiagaraRequirements(FManifestData& Data)
{
	{
		FFlightAssetRequirementRow Row;
		Row.Owner = TEXT("Swarm.NiagaraDataInterface");
		Row.RequirementId = TEXT("SwarmVisualizerSystem");
		Row.AssetClass = TEXT("NiagaraSystem");
		Row.AssetPath = FSoftObjectPath(TEXT("/Game/Effects/NS_SwarmVisualizer.NS_SwarmVisualizer"));
		Row.Tags = {
			TEXT("phase1"),
			TEXT("proof_of_concept"),
			TEXT("swarm"),
			TEXT("niagara")
		};
		Row.RequiredProperties.Add(TEXT("SimulationTarget"), TEXT("GPUComputeSim"));
		Row.RequiredProperties.Add(TEXT("ValidationMode"), TEXT("SchemaContract"));
		Data.AssetRequirements.Add(MoveTemp(Row));
	}

	{
		FFlightNiagaraRequirementRow Row;
		Row.Owner = TEXT("Swarm.NiagaraDataInterface");
		Row.RequirementId = TEXT("SwarmVisualizerContract");
		Row.SystemPath = FSoftObjectPath(TEXT("/Game/Effects/NS_SwarmVisualizer.NS_SwarmVisualizer"));
		Row.RequiredUserParameters = {
			TEXT("User.SwarmSubsystem"),
			TEXT("User.DroneCount")
		};
		Row.RequiredDataInterfaces = {
			TEXT("/Script/FlightProject.NiagaraDataInterfaceSwarm")
		};
		Data.NiagaraRequirements.Add(MoveTemp(Row));
	}
}

void AddRenderProfileRequirements(FManifestData& Data)
{
	FFlightRenderProfileRow Row;
	Row.ProfileName = TEXT("HeadlessValidation");
	Row.bEnableLumen = false;
	Row.bEnableNanite = false;
	Row.RequiredCVars.Add(TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("0"));
	Row.RequiredCVars.Add(TEXT("r.Lumen.Reflections.Allow"), TEXT("1"));
	Row.RequiredCVars.Add(TEXT("r.Nanite"), TEXT("0"));
	Row.RequiredCVars.Add(TEXT("r.RayTracing"), TEXT("0"));

	for (const TPair<FString, FString>& CVarPolicy : Row.RequiredCVars)
	{
		FFlightCVarRequirementRow CVarRow;
		CVarRow.Owner = TEXT("RenderProfile");
		CVarRow.RequirementId = FName(*FString::Printf(TEXT("%s.%s"), *Row.ProfileName.ToString(), *CVarPolicy.Key));
		CVarRow.ProfileName = Row.ProfileName;
		CVarRow.CVarName = CVarPolicy.Key;
		CVarRow.ExpectedValue = CVarPolicy.Value;
		CVarRow.ValueType = InferValueType(CVarPolicy.Value);
		Data.CVarRequirements.Add(MoveTemp(CVarRow));
	}

	Data.RenderProfiles.Add(MoveTemp(Row));
}

void AddPluginPolicyRequirements(FManifestData& Data)
{
	const auto AddRow = [&Data](const FName ProfileName, const FString& PluginName, const bool bExpectedEnabled, const bool bExpectedMounted)
	{
		FFlightPluginRequirementRow Row;
		Row.Owner = TEXT("PluginPolicy");
		Row.ProfileName = ProfileName;
		Row.RequirementId = FName(*FString::Printf(TEXT("%s.%s"), *ProfileName.ToString(), *PluginName));
		Row.PluginName = PluginName;
		Row.bExpectedEnabled = bExpectedEnabled;
		Row.bExpectedMounted = bExpectedMounted;
		Data.PluginRequirements.Add(MoveTemp(Row));
	};

	AddRow(TEXT("HeadlessValidation"), TEXT("Niagara"), true, true);
	AddRow(TEXT("HeadlessValidation"), TEXT("FlightGpuCompute"), true, true);
	AddRow(TEXT("HeadlessValidation"), TEXT("FlightVulkanExtensions"), true, true);

	// Negative-control profile used by schema tests to verify missing-plugin detection.
	AddRow(TEXT("PluginNegativeTest"), TEXT("FlightProject_ThisShouldNotExist"), true, true);
}

void AddVexSymbolRequirements(FManifestData& Data)
{
	using namespace Flight::Reflection::HLSL;
	Data.VexSymbolRequirements.Append(GenerateVexSymbolsFromStruct<Flight::Swarm::FDroidState>(TEXT("Swarm.Orchestrator")));
}

TSharedPtr<FJsonObject> MakeStringMapObject(const TMap<FString, FString>& Source)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Entry : Source)
	{
		Object->SetStringField(Entry.Key, Entry.Value);
	}
	return Object;
}

TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Source)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	Array.Reserve(Source.Num());

	for (const FString& Value : Source)
	{
		Array.Add(MakeShared<FJsonValueString>(Value));
	}

	return Array;
}

TArray<TSharedPtr<FJsonValue>> MakeSoftPathArray(const TArray<FSoftObjectPath>& Source)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	Array.Reserve(Source.Num());

	for (const FSoftObjectPath& Value : Source)
	{
		Array.Add(MakeShared<FJsonValueString>(Value.ToString()));
	}

	return Array;
}

TSharedPtr<FJsonObject> MakeAssetRowObject(const FFlightAssetRequirementRow& Row)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("owner"), Row.Owner.ToString());
	Object->SetStringField(TEXT("requirementId"), Row.RequirementId.ToString());
	Object->SetStringField(TEXT("assetClass"), Row.AssetClass);
	Object->SetStringField(TEXT("assetPath"), Row.AssetPath.ToString());
	Object->SetStringField(TEXT("templatePath"), Row.TemplatePath.ToString());
	Object->SetArrayField(TEXT("tags"), MakeStringArray(Row.Tags));
	Object->SetObjectField(TEXT("requiredProperties"), MakeStringMapObject(Row.RequiredProperties));
	return Object;
}

TSharedPtr<FJsonObject> MakeNiagaraRowObject(const FFlightNiagaraRequirementRow& Row)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("owner"), Row.Owner.ToString());
	Object->SetStringField(TEXT("requirementId"), Row.RequirementId.ToString());
	Object->SetStringField(TEXT("systemPath"), Row.SystemPath.ToString());
	Object->SetArrayField(TEXT("emitterTemplatePaths"), MakeSoftPathArray(Row.EmitterTemplatePaths));
	Object->SetArrayField(TEXT("requiredUserParameters"), MakeStringArray(Row.RequiredUserParameters));
	Object->SetArrayField(TEXT("requiredDataInterfaces"), MakeStringArray(Row.RequiredDataInterfaces));
	return Object;
}

TSharedPtr<FJsonObject> MakeRenderProfileObject(const FFlightRenderProfileRow& Row)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("profileName"), Row.ProfileName.ToString());
	Object->SetBoolField(TEXT("enableLumen"), Row.bEnableLumen);
	Object->SetBoolField(TEXT("enableNanite"), Row.bEnableNanite);
	Object->SetObjectField(TEXT("requiredCVars"), MakeStringMapObject(Row.RequiredCVars));
	return Object;
}

TSharedPtr<FJsonObject> MakeCVarRowObject(const FFlightCVarRequirementRow& Row)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("owner"), Row.Owner.ToString());
	Object->SetStringField(TEXT("requirementId"), Row.RequirementId.ToString());
	Object->SetStringField(TEXT("profileName"), Row.ProfileName.ToString());
	Object->SetStringField(TEXT("cvarName"), Row.CVarName);
	Object->SetStringField(TEXT("expectedValue"), Row.ExpectedValue);
	Object->SetStringField(TEXT("valueType"), CVarValueTypeToString(Row.ValueType));
	Object->SetNumberField(TEXT("floatTolerance"), Row.FloatTolerance);
	return Object;
}

TSharedPtr<FJsonObject> MakePluginRowObject(const FFlightPluginRequirementRow& Row)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("owner"), Row.Owner.ToString());
	Object->SetStringField(TEXT("requirementId"), Row.RequirementId.ToString());
	Object->SetStringField(TEXT("profileName"), Row.ProfileName.ToString());
	Object->SetStringField(TEXT("pluginName"), Row.PluginName);
	Object->SetBoolField(TEXT("expectedEnabled"), Row.bExpectedEnabled);
	Object->SetBoolField(TEXT("expectedMounted"), Row.bExpectedMounted);
	return Object;
}

TSharedPtr<FJsonObject> MakeVexSymbolRowObject(const FFlightVexSymbolRow& Row)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("owner"), Row.Owner.ToString());
	Object->SetStringField(TEXT("requirementId"), Row.RequirementId.ToString());
	Object->SetStringField(TEXT("symbolName"), Row.SymbolName);
	Object->SetStringField(TEXT("valueType"), VexValueTypeToString(Row.ValueType));
	Object->SetStringField(TEXT("residency"), VexResidencyToString(Row.Residency));
	Object->SetStringField(TEXT("affinity"), VexAffinityToString(Row.Affinity));
	Object->SetStringField(TEXT("hlslIdentifier"), Row.HlslIdentifier);
	Object->SetStringField(TEXT("verseIdentifier"), Row.VerseIdentifier);
	Object->SetBoolField(TEXT("writable"), Row.bWritable);
	Object->SetBoolField(TEXT("required"), Row.bRequired);
	return Object;
}

} // namespace

FManifestData BuildManifestData()
{
	FManifestData Data;
	AddSwarmNiagaraRequirements(Data);
	AddRenderProfileRequirements(Data);
	AddPluginPolicyRequirements(Data);
	AddVexSymbolRequirements(Data);
	return Data;
}

FString BuildManifestJson()
{
	const FManifestData Data = BuildManifestData();

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("schemaVersion"), TEXT("0.2"));
	RootObject->SetStringField(TEXT("generatedAtUtc"), FDateTime::UtcNow().ToIso8601());

	TArray<TSharedPtr<FJsonValue>> AssetRows;
	AssetRows.Reserve(Data.AssetRequirements.Num());
	for (const FFlightAssetRequirementRow& Row : Data.AssetRequirements)
	{
		AssetRows.Add(MakeShared<FJsonValueObject>(MakeAssetRowObject(Row)));
	}
	RootObject->SetArrayField(TEXT("assetRequirements"), AssetRows);

	TArray<TSharedPtr<FJsonValue>> NiagaraRows;
	NiagaraRows.Reserve(Data.NiagaraRequirements.Num());
	for (const FFlightNiagaraRequirementRow& Row : Data.NiagaraRequirements)
	{
		NiagaraRows.Add(MakeShared<FJsonValueObject>(MakeNiagaraRowObject(Row)));
	}
	RootObject->SetArrayField(TEXT("niagaraRequirements"), NiagaraRows);

	TArray<TSharedPtr<FJsonValue>> CVarRows;
	CVarRows.Reserve(Data.CVarRequirements.Num());
	for (const FFlightCVarRequirementRow& Row : Data.CVarRequirements)
	{
		CVarRows.Add(MakeShared<FJsonValueObject>(MakeCVarRowObject(Row)));
	}
	RootObject->SetArrayField(TEXT("cvarRequirements"), CVarRows);

	TArray<TSharedPtr<FJsonValue>> PluginRows;
	PluginRows.Reserve(Data.PluginRequirements.Num());
	for (const FFlightPluginRequirementRow& Row : Data.PluginRequirements)
	{
		PluginRows.Add(MakeShared<FJsonValueObject>(MakePluginRowObject(Row)));
	}
	RootObject->SetArrayField(TEXT("pluginRequirements"), PluginRows);

	TArray<TSharedPtr<FJsonValue>> VexRows;
	VexRows.Reserve(Data.VexSymbolRequirements.Num());
	for (const FFlightVexSymbolRow& Row : Data.VexSymbolRequirements)
	{
		VexRows.Add(MakeShared<FJsonValueObject>(MakeVexSymbolRowObject(Row)));
	}
	RootObject->SetArrayField(TEXT("vexSymbolRequirements"), VexRows);

	TArray<TSharedPtr<FJsonValue>> RenderRows;
	RenderRows.Reserve(Data.RenderProfiles.Num());
	for (const FFlightRenderProfileRow& Row : Data.RenderProfiles)
	{
		RenderRows.Add(MakeShared<FJsonValueObject>(MakeRenderProfileObject(Row)));
	}
	RootObject->SetArrayField(TEXT("renderProfiles"), RenderRows);

	RootObject->SetNumberField(TEXT("assetRequirementCount"), Data.AssetRequirements.Num());
	RootObject->SetNumberField(TEXT("niagaraRequirementCount"), Data.NiagaraRequirements.Num());
	RootObject->SetNumberField(TEXT("cvarRequirementCount"), Data.CVarRequirements.Num());
	RootObject->SetNumberField(TEXT("pluginRequirementCount"), Data.PluginRequirements.Num());
	RootObject->SetNumberField(TEXT("vexSymbolRequirementCount"), Data.VexSymbolRequirements.Num());
	RootObject->SetNumberField(TEXT("renderProfileCount"), Data.RenderProfiles.Num());

	FString ManifestJson;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ManifestJson);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter);
	return ManifestJson;
}

} // namespace Flight::Schema
