#include "FlightScriptingLibrary.h"

#include "FlightProject.h"
#include "FlightDataSubsystem.h"
#include "FlightWorldBootstrapSubsystem.h"
#include "FlightSpatialLayoutDirector.h"
#include "FlightProjectDeveloperSettings.h"
#include "Swarm/FlightSwarmSubsystem.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Diagnostics/FlightPIEObservationSubsystem.h"

#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/FlightMassFragments.h"

#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceUtilities.h"
#include "NiagaraSystem.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

namespace
{

bool TryParseBool(const FString& InValue, bool& OutValue)
{
	FString Value = InValue.TrimStartAndEnd();
	Value.ToLowerInline();

	if (Value == TEXT("1") || Value == TEXT("true") || Value == TEXT("on") || Value == TEXT("yes"))
	{
		OutValue = true;
		return true;
	}

	if (Value == TEXT("0") || Value == TEXT("false") || Value == TEXT("off") || Value == TEXT("no"))
	{
		OutValue = false;
		return true;
	}

	return false;
}

FString CVarValueTypeToString(const EFlightCVarValueType ValueType)
{
	switch (ValueType)
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

bool EvaluateCVarContractInternal(
	const FString& CVarName,
	const FString& ExpectedValue,
	const EFlightCVarValueType ValueType,
	const float FloatTolerance,
	FString& OutObservedValue,
	FString& OutIssue)
{
	OutObservedValue.Reset();
	OutIssue.Reset();

	const FString TrimmedName = CVarName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty())
	{
		OutIssue = TEXT("CVar contract: cvar name is empty");
		return false;
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*TrimmedName);
	if (!CVar)
	{
		OutIssue = FString::Printf(TEXT("CVar contract: cvar '%s' was not found"), *TrimmedName);
		return false;
	}

	const FString TrimmedExpectedValue = ExpectedValue.TrimStartAndEnd();

	switch (ValueType)
	{
	case EFlightCVarValueType::Int:
	{
		int32 ExpectedInt = 0;
		if (!LexTryParseString(ExpectedInt, *TrimmedExpectedValue))
		{
			OutIssue = FString::Printf(
				TEXT("CVar contract: expected int value '%s' is invalid for '%s'"),
				*TrimmedExpectedValue,
				*TrimmedName);
			return false;
		}

		const int32 ActualInt = CVar->GetInt();
		OutObservedValue = FString::FromInt(ActualInt);
		if (ActualInt != ExpectedInt)
		{
			OutIssue = FString::Printf(
				TEXT("CVar contract: '%s' expected int '%d' but found '%d'"),
				*TrimmedName,
				ExpectedInt,
				ActualInt);
			return false;
		}
		return true;
	}
	case EFlightCVarValueType::Float:
	{
		float ExpectedFloat = 0.0f;
		if (!LexTryParseString(ExpectedFloat, *TrimmedExpectedValue))
		{
			OutIssue = FString::Printf(
				TEXT("CVar contract: expected float value '%s' is invalid for '%s'"),
				*TrimmedExpectedValue,
				*TrimmedName);
			return false;
		}

		const float ActualFloat = CVar->GetFloat();
		OutObservedValue = FString::Printf(TEXT("%g"), ActualFloat);
		if (!FMath::IsNearlyEqual(ActualFloat, ExpectedFloat, FloatTolerance))
		{
			OutIssue = FString::Printf(
				TEXT("CVar contract: '%s' expected float '%g' but found '%g' (tol=%g)"),
				*TrimmedName,
				ExpectedFloat,
				ActualFloat,
				FloatTolerance);
			return false;
		}
		return true;
	}
	case EFlightCVarValueType::Bool:
	{
		bool bExpectedBool = false;
		if (!TryParseBool(TrimmedExpectedValue, bExpectedBool))
		{
			OutIssue = FString::Printf(
				TEXT("CVar contract: expected bool value '%s' is invalid for '%s'"),
				*TrimmedExpectedValue,
				*TrimmedName);
			return false;
		}

		bool bActualBool = false;
		const FString ActualStringValue = CVar->GetString();
		if (!TryParseBool(ActualStringValue, bActualBool))
		{
			bActualBool = CVar->GetInt() != 0;
		}

		OutObservedValue = bActualBool ? TEXT("true") : TEXT("false");
		if (bActualBool != bExpectedBool)
		{
			OutIssue = FString::Printf(
				TEXT("CVar contract: '%s' expected bool '%s' but found '%s'"),
				*TrimmedName,
				bExpectedBool ? TEXT("true") : TEXT("false"),
				bActualBool ? TEXT("true") : TEXT("false"));
			return false;
		}
		return true;
	}
	case EFlightCVarValueType::String:
	default:
	{
		const FString ActualStringValue = CVar->GetString().TrimStartAndEnd();
		OutObservedValue = ActualStringValue;
		if (!ActualStringValue.Equals(TrimmedExpectedValue, ESearchCase::CaseSensitive))
		{
			OutIssue = FString::Printf(
				TEXT("CVar contract: '%s' expected string '%s' but found '%s'"),
				*TrimmedName,
				*TrimmedExpectedValue,
				*ActualStringValue);
			return false;
		}
		return true;
	}
	}
}

TSharedPtr<FJsonObject> MakeObservedCVarRowObject(
	const FFlightCVarRequirementRow& Requirement,
	const FString& ObservedValue,
	const FString& DriftIssue)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("owner"), Requirement.Owner.ToString());
	Object->SetStringField(TEXT("requirementId"), Requirement.RequirementId.ToString());
	Object->SetStringField(TEXT("profileName"), Requirement.ProfileName.ToString());
	Object->SetStringField(TEXT("cvarName"), Requirement.CVarName);
	Object->SetStringField(TEXT("expectedValue"), Requirement.ExpectedValue);
	Object->SetStringField(TEXT("observedValue"), ObservedValue);
	Object->SetStringField(TEXT("valueType"), CVarValueTypeToString(Requirement.ValueType));
	Object->SetNumberField(TEXT("floatTolerance"), Requirement.FloatTolerance);
	Object->SetBoolField(TEXT("matches"), DriftIssue.IsEmpty());
	if (!DriftIssue.IsEmpty())
	{
		Object->SetStringField(TEXT("issue"), DriftIssue);
	}

	return Object;
}

struct FObservedPluginState
{
	bool bFound = false;
	bool bEnabled = false;
	bool bMounted = false;
};

FString NormalizePluginKey(const FString& PluginName)
{
	FString Key = PluginName.TrimStartAndEnd();
	Key.ToLowerInline();
	return Key;
}

TMap<FString, FObservedPluginState> BuildObservedPluginStateMap()
{
	TMap<FString, FObservedPluginState> States;
	IPluginManager& PluginManager = IPluginManager::Get();

	const TArray<TSharedRef<IPlugin>> DiscoveredPlugins = PluginManager.GetDiscoveredPlugins();
	for (const TSharedRef<IPlugin>& Plugin : DiscoveredPlugins)
	{
		const FString Key = NormalizePluginKey(Plugin->GetName());
		if (Key.IsEmpty())
		{
			continue;
		}

		FObservedPluginState& State = States.FindOrAdd(Key);
		State.bFound = true;
		State.bEnabled = State.bEnabled || Plugin->IsEnabled();
		State.bMounted = State.bMounted || Plugin->IsMounted();
	}

	const TArray<TSharedRef<IPlugin>> EnabledPlugins = PluginManager.GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
	{
		const FString Key = NormalizePluginKey(Plugin->GetName());
		if (Key.IsEmpty())
		{
			continue;
		}

		FObservedPluginState& State = States.FindOrAdd(Key);
		State.bFound = true;
		State.bEnabled = true;
		State.bMounted = State.bMounted || Plugin->IsMounted();
	}

	return States;
}

bool EvaluatePluginRequirementInternal(
	const FFlightPluginRequirementRow& Requirement,
	const TMap<FString, FObservedPluginState>& ObservedStates,
	bool& OutFound,
	bool& OutEnabled,
	bool& OutMounted,
	FString& OutIssue)
{
	OutFound = false;
	OutEnabled = false;
	OutMounted = false;
	OutIssue.Reset();

	const FString Key = NormalizePluginKey(Requirement.PluginName);
	if (Key.IsEmpty())
	{
		OutIssue = TEXT("Plugin contract: plugin name is empty");
		return false;
	}

	if (const FObservedPluginState* State = ObservedStates.Find(Key))
	{
		OutFound = State->bFound;
		OutEnabled = State->bEnabled;
		OutMounted = State->bMounted;
	}

	if (!OutFound)
	{
		if (Requirement.bExpectedEnabled || Requirement.bExpectedMounted)
		{
			OutIssue = FString::Printf(
				TEXT("Plugin contract: plugin '%s' was not discovered"),
				*Requirement.PluginName);
			return false;
		}

		return true;
	}

	if (Requirement.bExpectedEnabled && !OutEnabled)
	{
		OutIssue = FString::Printf(
			TEXT("Plugin contract: plugin '%s' expected enabled but is disabled"),
			*Requirement.PluginName);
		return false;
	}

	if (!Requirement.bExpectedEnabled && OutEnabled)
	{
		OutIssue = FString::Printf(
			TEXT("Plugin contract: plugin '%s' expected disabled but is enabled"),
			*Requirement.PluginName);
		return false;
	}

	if (Requirement.bExpectedMounted && !OutMounted)
	{
		OutIssue = FString::Printf(
			TEXT("Plugin contract: plugin '%s' expected mounted but is not mounted"),
			*Requirement.PluginName);
		return false;
	}

	if (!Requirement.bExpectedMounted && OutMounted)
	{
		OutIssue = FString::Printf(
			TEXT("Plugin contract: plugin '%s' expected not mounted but is mounted"),
			*Requirement.PluginName);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> MakeObservedPluginRowObject(
	const FFlightPluginRequirementRow& Requirement,
	const bool bObservedFound,
	const bool bObservedEnabled,
	const bool bObservedMounted,
	const FString& DriftIssue)
{
	TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("owner"), Requirement.Owner.ToString());
	Object->SetStringField(TEXT("requirementId"), Requirement.RequirementId.ToString());
	Object->SetStringField(TEXT("profileName"), Requirement.ProfileName.ToString());
	Object->SetStringField(TEXT("pluginName"), Requirement.PluginName);
	Object->SetBoolField(TEXT("expectedEnabled"), Requirement.bExpectedEnabled);
	Object->SetBoolField(TEXT("expectedMounted"), Requirement.bExpectedMounted);
	Object->SetBoolField(TEXT("observedFound"), bObservedFound);
	Object->SetBoolField(TEXT("observedEnabled"), bObservedEnabled);
	Object->SetBoolField(TEXT("observedMounted"), bObservedMounted);
	Object->SetBoolField(TEXT("matches"), DriftIssue.IsEmpty());
	if (!DriftIssue.IsEmpty())
	{
		Object->SetStringField(TEXT("issue"), DriftIssue);
	}
	return Object;
}

} // namespace

// ============================================================================
// Data Subsystem
// ============================================================================

void UFlightScriptingLibrary::ReloadDataConfigs(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: ReloadDataConfigs called with null context"));
        return;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: No GameInstance available"));
        return;
    }

    UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    if (DataSubsystem)
    {
        DataSubsystem->ReloadAllConfigs();
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Data configs reloaded"));
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: FlightDataSubsystem not found"));
    }
}

void UFlightScriptingLibrary::ReloadCSVConfigs(const UObject* WorldContextObject)
{
    // Legacy name - forwards to new function
    ReloadDataConfigs(WorldContextObject);
}

bool UFlightScriptingLibrary::ReloadCSVConfig(const UObject* WorldContextObject, const FString& ConfigName)
{
    if (!WorldContextObject)
    {
        return false;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        return false;
    }

    UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    if (DataSubsystem)
    {
        return DataSubsystem->ReloadConfig(ConfigName);
    }

    return false;
}

bool UFlightScriptingLibrary::IsDataFullyLoaded(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return false;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        return false;
    }

    const UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    return DataSubsystem ? DataSubsystem->IsFullyLoaded() : false;
}

// ============================================================================
// Bootstrap
// ============================================================================

void UFlightScriptingLibrary::RunBootstrap(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    UFlightWorldBootstrapSubsystem* Bootstrap = World->GetSubsystem<UFlightWorldBootstrapSubsystem>();
    if (Bootstrap)
    {
        Bootstrap->RunBootstrap();
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Bootstrap completed"));
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: FlightWorldBootstrapSubsystem not found"));
    }
}

int32 UFlightScriptingLibrary::SpawnInitialSwarm(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return 0;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return 0;
    }

    // SwarmSpawnerSubsystem is in the SwarmEncounter plugin - find it dynamically
    // This avoids a hard dependency on the GameFeatures plugin
    static UClass* SwarmSpawnerClass = nullptr;
    if (!SwarmSpawnerClass)
    {
        SwarmSpawnerClass = FindObject<UClass>(nullptr, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"));
    }

    if (!SwarmSpawnerClass)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: SwarmEncounter plugin not loaded"));
        return 0;
    }

    UWorldSubsystem* Subsystem = World->GetSubsystemBase(SwarmSpawnerClass);
    if (!Subsystem)
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: FlightSwarmSpawnerSubsystem not available"));
        return 0;
    }

    // Call SpawnInitialSwarm via reflection
    UFunction* SpawnFunc = SwarmSpawnerClass->FindFunctionByName(TEXT("SpawnInitialSwarm"));
    if (SpawnFunc)
    {
        Subsystem->ProcessEvent(SpawnFunc, nullptr);
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: SpawnInitialSwarm called"));
        // Return 1 to indicate success - the spawner logs actual count
        return 1;
    }

    UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: SpawnInitialSwarm function not found"));
    return 0;
}

void UFlightScriptingLibrary::InitializeGpuSwarm(const UObject* WorldContextObject, int32 EntityCount)
{
    if (!WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    if (UFlightSwarmSubsystem* SwarmSubsystem = World->GetSubsystem<UFlightSwarmSubsystem>())
    {
        SwarmSubsystem->InitializeSwarm(EntityCount);
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: InitializeGpuSwarm called for %d entities"), EntityCount);
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: FlightSwarmSubsystem not found"));
    }
}

// ============================================================================
// Spatial Layout
// ============================================================================

void UFlightScriptingLibrary::RebuildSpatialLayout(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    // Find the spatial layout director in the world
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(World, AFlightSpatialLayoutDirector::StaticClass(), FoundActors);

    if (FoundActors.Num() > 0)
    {
        AFlightSpatialLayoutDirector* Director = Cast<AFlightSpatialLayoutDirector>(FoundActors[0]);
        if (Director)
        {
            Director->RebuildLayout();
            UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Spatial layout rebuilt"));
        }
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: No FlightSpatialLayoutDirector found in world"));
    }
}

int32 UFlightScriptingLibrary::GetSpatialLayoutRowCount(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return 0;
    }

    const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
    if (!GameInstance)
    {
        return 0;
    }

    const UFlightDataSubsystem* DataSubsystem = GameInstance->GetSubsystem<UFlightDataSubsystem>();
    if (DataSubsystem)
    {
        return DataSubsystem->GetSpatialLayout().Num();
    }

    return 0;
}

// ============================================================================
// Mass Entity
// ============================================================================

int32 UFlightScriptingLibrary::GetSwarmEntityCount(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return 0;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return 0;
    }

    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
    if (!MassSubsystem)
    {
        return 0;
    }

    // Query entities with the swarm member tag
    FMassEntityQuery Query;
    Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);

    // UE 5.7: CacheArchetypes and GetNumMatchingEntities no longer need EntityManager param
    Query.CacheArchetypes();

    return Query.GetNumMatchingEntities();
}

void UFlightScriptingLibrary::ClearAllSwarmEntities(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        return;
    }

    // Try the swarm spawner's DestroySwarm first (cleaner)
    static UClass* SwarmSpawnerClass = FindObject<UClass>(nullptr, TEXT("/Script/SwarmEncounter.FlightSwarmSpawnerSubsystem"));
    if (SwarmSpawnerClass)
    {
        UWorldSubsystem* Subsystem = World->GetSubsystemBase(SwarmSpawnerClass);
        if (Subsystem)
        {
            UFunction* DestroyFunc = SwarmSpawnerClass->FindFunctionByName(TEXT("DestroySwarm"));
            if (DestroyFunc)
            {
                Subsystem->ProcessEvent(DestroyFunc, nullptr);
                UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Swarm destroyed via spawner"));
                return;
            }
        }
    }

    // Fallback: destroy entities directly via Mass
    UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
    if (!MassSubsystem)
    {
        return;
    }

    FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

    FMassEntityQuery Query;
    Query.AddTagRequirement<FFlightSwarmMemberTag>(EMassFragmentPresence::All);

    // UE 5.7: Use GetMatchingEntityHandles to collect all matching entities
    TArray<FMassEntityHandle> EntitiesToDestroy = Query.GetMatchingEntityHandles();

    for (const FMassEntityHandle& Entity : EntitiesToDestroy)
    {
        EntityManager.DestroyEntity(Entity);
    }

    UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Destroyed %d swarm entities"), EntitiesToDestroy.Num());
}

// ============================================================================
// Validation
// ============================================================================

TArray<FString> UFlightScriptingLibrary::ValidateDataAssets()
{
    TArray<FString> Issues;

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (!Settings)
    {
        Issues.Add(TEXT("FlightProjectDeveloperSettings not found"));
        return Issues;
    }

    // Check Data Table configurations
    if (Settings->LightingConfigTable.IsNull())
    {
        Issues.Add(TEXT("LightingConfigTable is not configured"));
    }
    else if (!Settings->LightingConfigTable.LoadSynchronous())
    {
        Issues.Add(TEXT("LightingConfigTable failed to load"));
    }

    if (Settings->AutopilotConfigTable.IsNull())
    {
        Issues.Add(TEXT("AutopilotConfigTable is not configured"));
    }
    else if (!Settings->AutopilotConfigTable.LoadSynchronous())
    {
        Issues.Add(TEXT("AutopilotConfigTable failed to load"));
    }

    if (Settings->SpatialLayoutTable.IsNull())
    {
        Issues.Add(TEXT("SpatialLayoutTable is not configured"));
    }
    else if (!Settings->SpatialLayoutTable.LoadSynchronous())
    {
        Issues.Add(TEXT("SpatialLayoutTable failed to load"));
    }

    // ProceduralAnchorTable is optional
    if (!Settings->ProceduralAnchorTable.IsNull())
    {
        if (!Settings->ProceduralAnchorTable.LoadSynchronous())
        {
            Issues.Add(TEXT("ProceduralAnchorTable configured but failed to load"));
        }
    }

    if (Issues.Num() == 0)
    {
        UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: All data assets validated successfully"));
    }
    else
    {
        for (const FString& Issue : Issues)
        {
            UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: Validation issue: %s"), *Issue);
        }
    }

    return Issues;
}

TArray<FString> UFlightScriptingLibrary::GetConfiguredDataTablePaths()
{
    TArray<FString> Paths;

    const UFlightProjectDeveloperSettings* Settings = GetDefault<UFlightProjectDeveloperSettings>();
    if (Settings)
    {
        if (!Settings->LightingConfigTable.IsNull())
        {
            Paths.Add(Settings->LightingConfigTable.ToString());
        }
        if (!Settings->AutopilotConfigTable.IsNull())
        {
            Paths.Add(Settings->AutopilotConfigTable.ToString());
        }
        if (!Settings->SpatialLayoutTable.IsNull())
        {
            Paths.Add(Settings->SpatialLayoutTable.ToString());
        }
        if (!Settings->ProceduralAnchorTable.IsNull())
        {
            Paths.Add(Settings->ProceduralAnchorTable.ToString());
        }
    }

    return Paths;
}

TArray<FString> UFlightScriptingLibrary::GetConfiguredCSVPaths()
{
    // Legacy - now returns Data Table paths
    return GetConfiguredDataTablePaths();
}

// ============================================================================
// Schema Contracts
// ============================================================================

FString UFlightScriptingLibrary::GetRequirementManifestJson()
{
    return Flight::Schema::BuildManifestJson();
}

FString UFlightScriptingLibrary::ExportRequirementManifest(const FString& RelativeOutputPath)
{
    FString AbsolutePath = RelativeOutputPath;
    if (FPaths::IsRelative(AbsolutePath))
    {
        AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), AbsolutePath);
    }

    const FString DirectoryPath = FPaths::GetPath(AbsolutePath);
    if (!DirectoryPath.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*DirectoryPath, true);
    }

    const FString ManifestJson = Flight::Schema::BuildManifestJson();
    if (ManifestJson.IsEmpty())
    {
        UE_LOG(LogFlightProject, Error, TEXT("FlightScriptingLibrary: Manifest serialization produced empty output"));
        return FString();
    }

    if (!FFileHelper::SaveStringToFile(ManifestJson, *AbsolutePath))
    {
        UE_LOG(LogFlightProject, Error, TEXT("FlightScriptingLibrary: Failed to write manifest '%s'"), *AbsolutePath);
        return FString();
    }

    UE_LOG(LogFlightProject, Log, TEXT("FlightScriptingLibrary: Exported requirement manifest to '%s'"), *AbsolutePath);
    return AbsolutePath;
}

TArray<FString> UFlightScriptingLibrary::ValidateNiagaraSystemContract(
    const FString& SystemObjectPath,
    const TArray<FString>& RequiredUserParameters,
    const TArray<FString>& RequiredDataInterfaceClassPaths)
{
    TArray<FString> Issues;

    if (SystemObjectPath.IsEmpty())
    {
        Issues.Add(TEXT("Niagara contract: system path is empty"));
        return Issues;
    }

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(
        StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *SystemObjectPath, nullptr, LOAD_NoWarn));
    if (!NiagaraSystem)
    {
        Issues.Add(FString::Printf(TEXT("Niagara contract: failed to load system '%s'"), *SystemObjectPath));
        return Issues;
    }

    auto NormalizeUserName = [](const FString& InName) -> FString
    {
        FString Name = InName.TrimStartAndEnd();
        if (Name.IsEmpty())
        {
            return Name;
        }

        if (!Name.StartsWith(TEXT("User."), ESearchCase::IgnoreCase))
        {
            Name = FString::Printf(TEXT("User.%s"), *Name);
        }

        Name.ToLowerInline();
        return Name;
    };

    TSet<FString> ExposedUserParams;
    {
        TArray<FNiagaraVariable> UserParams;
        NiagaraSystem->GetExposedParameters().GetUserParameters(UserParams);
        for (const FNiagaraVariable& Param : UserParams)
        {
            ExposedUserParams.Add(NormalizeUserName(Param.GetName().ToString()));
        }
    }

    for (const FString& RequiredParam : RequiredUserParameters)
    {
        const FString NormalizedRequired = NormalizeUserName(RequiredParam);
        if (NormalizedRequired.IsEmpty())
        {
            continue;
        }

        if (!ExposedUserParams.Contains(NormalizedRequired))
        {
            Issues.Add(FString::Printf(
                TEXT("Niagara contract: missing user parameter '%s' on '%s'"),
                *RequiredParam,
                *SystemObjectPath));
        }
    }

    TSet<FString> DataInterfaceClassPaths;
    TSet<FString> DataInterfaceClassNames;

    auto AddDataInterfaceClass = [&DataInterfaceClassPaths, &DataInterfaceClassNames](UNiagaraDataInterface* DataInterface)
    {
        if (!DataInterface || !DataInterface->GetClass())
        {
            return;
        }

        FString ClassPath = DataInterface->GetClass()->GetPathName();
        ClassPath.ToLowerInline();
        DataInterfaceClassPaths.Add(ClassPath);

        FString ClassName = DataInterface->GetClass()->GetName();
        ClassName.ToLowerInline();
        DataInterfaceClassNames.Add(ClassName);
    };

    FNiagaraDataInterfaceUtilities::ForEachDataInterface(
        NiagaraSystem,
        [&AddDataInterfaceClass](const FNiagaraDataInterfaceUtilities::FDataInterfaceUsageContext& Context)
        {
            AddDataInterfaceClass(Context.DataInterface);
            return true;
        });

    for (UNiagaraDataInterface* DataInterface : NiagaraSystem->GetExposedParameters().GetDataInterfaces())
    {
        AddDataInterfaceClass(DataInterface);
    }

    for (const FString& RequiredDataInterfaceClass : RequiredDataInterfaceClassPaths)
    {
        FString RequiredPath = RequiredDataInterfaceClass.TrimStartAndEnd();
        if (RequiredPath.IsEmpty())
        {
            continue;
        }

        FString RequiredPathLower = RequiredPath;
        RequiredPathLower.ToLowerInline();

        FString RequiredClassName = RequiredPathLower;
        int32 DotIndex = INDEX_NONE;
        if (RequiredClassName.FindLastChar(TEXT('.'), DotIndex) && DotIndex + 1 < RequiredClassName.Len())
        {
            RequiredClassName = RequiredClassName.Mid(DotIndex + 1);
        }

        const bool bFound =
            DataInterfaceClassPaths.Contains(RequiredPathLower) ||
            DataInterfaceClassNames.Contains(RequiredPathLower) ||
            DataInterfaceClassNames.Contains(RequiredClassName);

        if (!bFound)
        {
            Issues.Add(FString::Printf(
                TEXT("Niagara contract: missing data interface class '%s' on '%s'"),
                *RequiredDataInterfaceClass,
                *SystemObjectPath));
        }
    }

	return Issues;
}

TArray<FString> UFlightScriptingLibrary::ValidateCVarContract(
	const FString& CVarName,
	const FString& ExpectedValue,
	const EFlightCVarValueType ValueType,
	const float FloatTolerance)
{
	TArray<FString> Issues;
	FString ObservedValue;
	FString DriftIssue;
	const bool bMatch = EvaluateCVarContractInternal(
		CVarName,
		ExpectedValue,
		ValueType,
		FloatTolerance,
		ObservedValue,
		DriftIssue);
	if (!bMatch)
	{
		Issues.Add(DriftIssue);
	}
	return Issues;
}

TArray<FString> UFlightScriptingLibrary::ValidateCVarRequirementsForProfile(const FName ProfileName)
{
	return DiffCVarRequirementsForProfile(ProfileName);
}

TArray<FString> UFlightScriptingLibrary::DiffCVarRequirementsForProfile(const FName ProfileName)
{
	TArray<FString> Issues;

	if (ProfileName.IsNone())
	{
		Issues.Add(TEXT("CVar profile contract: profile name is empty"));
		return Issues;
	}

	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();

	int32 CheckedRows = 0;
	for (const FFlightCVarRequirementRow& Requirement : ManifestData.CVarRequirements)
	{
		if (Requirement.ProfileName != ProfileName)
		{
			continue;
		}

		++CheckedRows;

		FString ObservedValue;
		FString DriftIssue;
		const bool bMatch = EvaluateCVarContractInternal(
			Requirement.CVarName,
			Requirement.ExpectedValue,
			Requirement.ValueType,
			Requirement.FloatTolerance,
			ObservedValue,
			DriftIssue);

		if (!bMatch)
		{
			Issues.Add(FString::Printf(
				TEXT("[%s/%s:%s] %s"),
				*Requirement.Owner.ToString(),
				*Requirement.ProfileName.ToString(),
				*Requirement.RequirementId.ToString(),
				*DriftIssue));
		}
	}

	if (CheckedRows == 0)
	{
		Issues.Add(FString::Printf(
			TEXT("CVar profile contract: no cvar requirements found for profile '%s'"),
			*ProfileName.ToString()));
	}

	return Issues;
}

FString UFlightScriptingLibrary::ExportObservedCVarSnapshotForProfile(
	const FName ProfileName,
	const FString& RelativeOutputPath)
{
	if (ProfileName.IsNone())
	{
		UE_LOG(LogFlightProject, Error, TEXT("FlightScriptingLibrary: ExportObservedCVarSnapshotForProfile called with empty profile"));
		return FString();
	}

	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();

	int32 CheckedRows = 0;
	int32 MismatchRows = 0;
	TArray<TSharedPtr<FJsonValue>> ObservedRows;

	for (const FFlightCVarRequirementRow& Requirement : ManifestData.CVarRequirements)
	{
		if (Requirement.ProfileName != ProfileName)
		{
			continue;
		}

		++CheckedRows;

		FString ObservedValue;
		FString DriftIssue;
		const bool bMatch = EvaluateCVarContractInternal(
			Requirement.CVarName,
			Requirement.ExpectedValue,
			Requirement.ValueType,
			Requirement.FloatTolerance,
			ObservedValue,
			DriftIssue);
		if (!bMatch)
		{
			++MismatchRows;
		}

		ObservedRows.Add(MakeShared<FJsonValueObject>(
			MakeObservedCVarRowObject(Requirement, ObservedValue, DriftIssue)));
	}

	if (CheckedRows == 0)
	{
		UE_LOG(
			LogFlightProject,
			Error,
			TEXT("FlightScriptingLibrary: No CVar requirements found for profile '%s'"),
			*ProfileName.ToString());
		return FString();
	}

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("schemaVersion"), TEXT("0.1"));
	RootObject->SetStringField(TEXT("generatedAtUtc"), FDateTime::UtcNow().ToIso8601());
	RootObject->SetStringField(TEXT("profileName"), ProfileName.ToString());
	RootObject->SetArrayField(TEXT("observedCVarRequirements"), ObservedRows);
	RootObject->SetNumberField(TEXT("checkedRequirementCount"), CheckedRows);
	RootObject->SetNumberField(TEXT("mismatchRequirementCount"), MismatchRows);

	FString AbsolutePath = RelativeOutputPath;
	if (FPaths::IsRelative(AbsolutePath))
	{
		AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), AbsolutePath);
	}

	const FString DirectoryPath = FPaths::GetPath(AbsolutePath);
	if (!DirectoryPath.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*DirectoryPath, true);
	}

	FString SnapshotJson;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&SnapshotJson);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter);

	if (!FFileHelper::SaveStringToFile(SnapshotJson, *AbsolutePath))
	{
		UE_LOG(
			LogFlightProject,
			Error,
			TEXT("FlightScriptingLibrary: Failed to write observed CVar snapshot '%s'"),
			*AbsolutePath);
		return FString();
	}

	UE_LOG(
		LogFlightProject,
		Log,
		TEXT("FlightScriptingLibrary: Exported observed CVar snapshot for profile '%s' to '%s' (%d checked, %d mismatch)"),
		*ProfileName.ToString(),
		*AbsolutePath,
		CheckedRows,
		MismatchRows);

	return AbsolutePath;
}

TArray<FString> UFlightScriptingLibrary::DiffPluginRequirementsForProfile(const FName ProfileName)
{
	TArray<FString> Issues;

	if (ProfileName.IsNone())
	{
		Issues.Add(TEXT("Plugin profile contract: profile name is empty"));
		return Issues;
	}

	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TMap<FString, FObservedPluginState> ObservedStates = BuildObservedPluginStateMap();

	int32 CheckedRows = 0;
	for (const FFlightPluginRequirementRow& Requirement : ManifestData.PluginRequirements)
	{
		if (Requirement.ProfileName != ProfileName)
		{
			continue;
		}

		++CheckedRows;

		bool bFound = false;
		bool bEnabled = false;
		bool bMounted = false;
		FString DriftIssue;
		const bool bMatch = EvaluatePluginRequirementInternal(
			Requirement,
			ObservedStates,
			bFound,
			bEnabled,
			bMounted,
			DriftIssue);
		if (!bMatch)
		{
			Issues.Add(FString::Printf(
				TEXT("[%s/%s:%s] %s"),
				*Requirement.Owner.ToString(),
				*Requirement.ProfileName.ToString(),
				*Requirement.RequirementId.ToString(),
				*DriftIssue));
		}
	}

	if (CheckedRows == 0)
	{
		Issues.Add(FString::Printf(
			TEXT("Plugin profile contract: no plugin requirements found for profile '%s'"),
			*ProfileName.ToString()));
	}

	return Issues;
}

FString UFlightScriptingLibrary::ExportObservedPluginSnapshotForProfile(
	const FName ProfileName,
	const FString& RelativeOutputPath)
{
	if (ProfileName.IsNone())
	{
		UE_LOG(LogFlightProject, Error, TEXT("FlightScriptingLibrary: ExportObservedPluginSnapshotForProfile called with empty profile"));
		return FString();
	}

	const Flight::Schema::FManifestData ManifestData = Flight::Schema::BuildManifestData();
	const TMap<FString, FObservedPluginState> ObservedStates = BuildObservedPluginStateMap();

	int32 CheckedRows = 0;
	int32 MismatchRows = 0;
	TArray<TSharedPtr<FJsonValue>> ObservedRows;

	for (const FFlightPluginRequirementRow& Requirement : ManifestData.PluginRequirements)
	{
		if (Requirement.ProfileName != ProfileName)
		{
			continue;
		}

		++CheckedRows;

		bool bFound = false;
		bool bEnabled = false;
		bool bMounted = false;
		FString DriftIssue;
		const bool bMatch = EvaluatePluginRequirementInternal(
			Requirement,
			ObservedStates,
			bFound,
			bEnabled,
			bMounted,
			DriftIssue);
		if (!bMatch)
		{
			++MismatchRows;
		}

		ObservedRows.Add(MakeShared<FJsonValueObject>(
			MakeObservedPluginRowObject(Requirement, bFound, bEnabled, bMounted, DriftIssue)));
	}

	if (CheckedRows == 0)
	{
		UE_LOG(
			LogFlightProject,
			Error,
			TEXT("FlightScriptingLibrary: No plugin requirements found for profile '%s'"),
			*ProfileName.ToString());
		return FString();
	}

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("schemaVersion"), TEXT("0.1"));
	RootObject->SetStringField(TEXT("generatedAtUtc"), FDateTime::UtcNow().ToIso8601());
	RootObject->SetStringField(TEXT("profileName"), ProfileName.ToString());
	RootObject->SetArrayField(TEXT("observedPluginRequirements"), ObservedRows);
	RootObject->SetNumberField(TEXT("checkedRequirementCount"), CheckedRows);
	RootObject->SetNumberField(TEXT("mismatchRequirementCount"), MismatchRows);

	FString AbsolutePath = RelativeOutputPath;
	if (FPaths::IsRelative(AbsolutePath))
	{
		AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), AbsolutePath);
	}

	const FString DirectoryPath = FPaths::GetPath(AbsolutePath);
	if (!DirectoryPath.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*DirectoryPath, true);
	}

	FString SnapshotJson;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&SnapshotJson);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter);

	if (!FFileHelper::SaveStringToFile(SnapshotJson, *AbsolutePath))
	{
		UE_LOG(
			LogFlightProject,
			Error,
			TEXT("FlightScriptingLibrary: Failed to write observed plugin snapshot '%s'"),
			*AbsolutePath);
		return FString();
	}

	UE_LOG(
		LogFlightProject,
		Log,
		TEXT("FlightScriptingLibrary: Exported observed plugin snapshot for profile '%s' to '%s' (%d checked, %d mismatch)"),
		*ProfileName.ToString(),
		*AbsolutePath,
		CheckedRows,
		MismatchRows);

	return AbsolutePath;
}

FString UFlightScriptingLibrary::ExportPIEEntityTrace(
	const UObject* WorldContextObject,
	const FString& RelativeOutputPath)
{
	if (!WorldContextObject)
	{
		return FString();
	}

	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;
	if (!World)
	{
		return FString();
	}

	UFlightPIEObservationSubsystem* ObservationSubsystem = World->GetSubsystem<UFlightPIEObservationSubsystem>();
	if (!ObservationSubsystem)
	{
		UE_LOG(LogFlightProject, Warning, TEXT("FlightScriptingLibrary: PIE observation subsystem is not available for world '%s'"), *World->GetName());
		return FString();
	}

	return ObservationSubsystem->ExportObservationSnapshot(RelativeOutputPath);
}

int32 UFlightScriptingLibrary::GetPIEEntityTraceEventCount(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return 0;
	}

	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;
	if (!World)
	{
		return 0;
	}

	const UFlightPIEObservationSubsystem* ObservationSubsystem = World->GetSubsystem<UFlightPIEObservationSubsystem>();
	return ObservationSubsystem ? ObservationSubsystem->GetObservedEventCount() : 0;
}

#include "Verse/UFlightVerseSubsystem.h"

bool UFlightScriptingLibrary::CompileVex(const UObject* WorldContextObject, int32 BehaviorID, const FString& VexSource, FString& OutErrors)
{
    if (!WorldContextObject)
    {
        OutErrors = TEXT("Null world context");
        return false;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World)
    {
        OutErrors = TEXT("Could not get world from context");
        return false;
    }

    UFlightVerseSubsystem* VerseSubsystem = World->GetSubsystem<UFlightVerseSubsystem>();
    if (!VerseSubsystem)
    {
        OutErrors = TEXT("FlightVerseSubsystem not found");
        return false;
    }

    return VerseSubsystem->CompileVex(static_cast<uint32>(BehaviorID), VexSource, OutErrors);
}

TArray<int32> UFlightScriptingLibrary::GetRegisteredBehaviorIDs(const UObject* WorldContextObject)
{
    TArray<int32> IDs;
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;

    if (VerseSubsystem)
    {
        for (const auto& KV : VerseSubsystem->Behaviors)
        {
            IDs.Add(static_cast<int32>(KV.Key));
        }
    }
    return IDs;
}

float UFlightScriptingLibrary::GetBehaviorExecutionRate(const UObject* WorldContextObject, int32 BehaviorID)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;

    if (VerseSubsystem)
    {
        if (const auto* B = VerseSubsystem->Behaviors.Find(static_cast<uint32>(BehaviorID)))
        {
            return B->ExecutionRateHz;
        }
    }
    return 0.0f;
}

int32 UFlightScriptingLibrary::GetBehaviorFrameInterval(const UObject* WorldContextObject, int32 BehaviorID)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;

    if (VerseSubsystem)
    {
        if (const auto* B = VerseSubsystem->Behaviors.Find(static_cast<uint32>(BehaviorID)))
        {
            return static_cast<int32>(B->FrameInterval);
        }
    }
    return 1;
}

EFlightVerseCompileState UFlightScriptingLibrary::GetBehaviorCompileState(const UObject* WorldContextObject, int32 BehaviorID)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;
    return VerseSubsystem
        ? VerseSubsystem->GetBehaviorCompileState(static_cast<uint32>(BehaviorID))
        : EFlightVerseCompileState::VmCompileFailed;
}

bool UFlightScriptingLibrary::IsBehaviorExecutable(const UObject* WorldContextObject, int32 BehaviorID)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;
    return VerseSubsystem
        ? VerseSubsystem->HasExecutableBehavior(static_cast<uint32>(BehaviorID))
        : false;
}

FString UFlightScriptingLibrary::GetBehaviorCompileDiagnostics(const UObject* WorldContextObject, int32 BehaviorID)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    UFlightVerseSubsystem* VerseSubsystem = World ? World->GetSubsystem<UFlightVerseSubsystem>() : nullptr;
    return VerseSubsystem
        ? VerseSubsystem->GetBehaviorCompileDiagnostics(static_cast<uint32>(BehaviorID))
        : FString();
}
