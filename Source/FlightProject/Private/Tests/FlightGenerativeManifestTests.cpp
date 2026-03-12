// Copyright Kelly Rey Wilson. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Schema/FlightRequirementRegistry.h"
#include "Schema/FlightRequirementSchema.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FFlightGenerativeManifestTest
 * 
 * A project-wide generative validation suite.
 * It iterates over the entire Project Manifest specification and generates
 * validation tests for every single requirement (Assets, Plugins, CVars, Contracts).
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FFlightGenerativeManifestTest, "FlightProject.Integration.Generative.ProjectManifest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FFlightGenerativeManifestTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	const Flight::Schema::FManifestData Manifest = Flight::Schema::BuildManifestData();

	// 1. Asset Requirements
	for (const FFlightAssetRequirementRow& Row : Manifest.AssetRequirements)
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("Asset.Presence.%s"), *Row.RequirementId.ToString()));
		OutTestCommands.Add(FString::Printf(TEXT("ASSET|%s|%s"), *Row.RequirementId.ToString(), *Row.AssetPath.ToString()));
	}

	// 2. Plugin Requirements
	for (const FFlightPluginRequirementRow& Row : Manifest.PluginRequirements)
	{
		if (Row.ProfileName == TEXT("PluginNegativeTest"))
		{
			continue;
		}

		OutBeautifiedNames.Add(FString::Printf(TEXT("Plugin.State.%s"), *Row.RequirementId.ToString()));
		OutTestCommands.Add(FString::Printf(TEXT("PLUGIN|%s|%s|%d|%d"), 
			*Row.RequirementId.ToString(), *Row.PluginName, Row.bExpectedEnabled, Row.bExpectedMounted));
	}

	// 3. CVar Requirements
	for (const FFlightCVarRequirementRow& Row : Manifest.CVarRequirements)
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("CVar.Value.%s"), *Row.RequirementId.ToString()));
		OutTestCommands.Add(FString::Printf(TEXT("CVAR|%s|%s|%s"), 
			*Row.RequirementId.ToString(), *Row.CVarName, *Row.ExpectedValue));
	}

	// 4. GPU Resource Contracts (Meta-validation)
	for (const FFlightGpuResourceContractRow& Row : Manifest.GpuResourceContracts)
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("GpuContract.Integrity.%s"), *Row.RequirementId.ToString()));
		OutTestCommands.Add(FString::Printf(TEXT("GPU_CONTRACT|%s|%s"), *Row.RequirementId.ToString(), *Row.ResourceId));
	}
}

bool FFlightGenerativeManifestTest::RunTest(const FString& Parameters)
{
	TArray<FString> Parts;
	Parameters.ParseIntoArray(Parts, TEXT("|"), true);
	if (Parts.Num() < 2)
	{
		AddError(FString::Printf(TEXT("Malformed generative test command: %s"), *Parameters));
		return false;
	}

	const FString Type = Parts[0];
	const FString ReqId = Parts[1];

	if (Type == TEXT("ASSET"))
	{
		if (Parts.Num() < 3)
		{
			AddError(FString::Printf(TEXT("Malformed ASSET command for %s: %s"), *ReqId, *Parameters));
			return false;
		}

		const FString AssetPath = Parts[2];
		FSoftObjectPath Path(AssetPath);
		TestTrue(FString::Printf(TEXT("Asset path should be valid for %s"), *ReqId), Path.IsValid());
		TestTrue(
			FString::Printf(TEXT("Asset package should exist for %s"), *AssetPath),
			FPackageName::DoesPackageExist(Path.GetLongPackageName()));

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(Path);
		TestTrue(FString::Printf(TEXT("Asset %s should exist in the asset registry"), *AssetPath), AssetData.IsValid());

		UObject* Loaded = Path.TryLoad();
		TestNotNull(FString::Printf(TEXT("Asset %s should be loadable"), *AssetPath), Loaded);
	}
	else if (Type == TEXT("PLUGIN"))
	{
		if (Parts.Num() < 5)
		{
			AddError(FString::Printf(TEXT("Malformed PLUGIN command for %s: %s"), *ReqId, *Parameters));
			return false;
		}

		const FString PluginName = Parts[2];
		const bool bExpectedEnabled = Parts[3].ToBool();
		const bool bExpectedMounted = Parts[4].ToBool();

		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		const bool bFound = Plugin.IsValid();
		const bool bObservedEnabled = bFound ? Plugin->IsEnabled() : false;
		const bool bObservedMounted = bFound ? Plugin->IsMounted() : false;

		if (!bFound)
		{
			TestFalse(
				FString::Printf(TEXT("Plugin %s should not be required when absent"), *PluginName),
				bExpectedEnabled || bExpectedMounted);
		}
		else
		{
			TestEqual(FString::Printf(TEXT("Plugin %s enabled state should match"), *PluginName), bObservedEnabled, bExpectedEnabled);
			TestEqual(FString::Printf(TEXT("Plugin %s mounted state should match"), *PluginName), bObservedMounted, bExpectedMounted);
		}
	}
	else if (Type == TEXT("CVAR"))
	{
		if (Parts.Num() < 4)
		{
			AddError(FString::Printf(TEXT("Malformed CVAR command for %s: %s"), *ReqId, *Parameters));
			return false;
		}

		const FString CVarName = Parts[2];
		const FString ExpectedValue = Parts[3];

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
		TestNotNull(FString::Printf(TEXT("CVar %s should exist"), *CVarName), CVar);
		if (CVar)
		{
			TestEqual(FString::Printf(TEXT("CVar %s value should match"), *CVarName), CVar->GetString(), ExpectedValue);
		}
	}
	else if (Type == TEXT("GPU_CONTRACT"))
	{
		if (Parts.Num() < 3)
		{
			AddError(FString::Printf(TEXT("Malformed GPU_CONTRACT command for %s: %s"), *ReqId, *Parameters));
			return false;
		}

		const FString ResourceId = Parts[2];
		TOptional<Flight::Schema::FFlightGpuStructuredBufferContract> Contract = Flight::Schema::ResolveStructuredBufferContract(ResourceId);
		
		TestTrue(FString::Printf(TEXT("GPU Contract %s should resolve"), *ResourceId), Contract.IsSet());
		if (Contract.IsSet())
		{
			TestTrue(FString::Printf(TEXT("GPU Contract %s should be valid"), *ResourceId), Contract->IsValid());
			TestTrue(FString::Printf(TEXT("GPU Contract %s should have a binding name"), *ResourceId), !Contract->BindingName.IsEmpty());
		}
	}
	else
	{
		AddError(FString::Printf(TEXT("Unknown generative test command type for %s: %s"), *ReqId, *Type));
		return false;
	}

	return true;
}

#endif
