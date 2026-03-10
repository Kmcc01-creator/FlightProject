// Copyright Kelly Rey Wilson. All Rights Reserved.
// FlightGpuCompute - RDG compute shaders with PostConfigInit shader registration
//
// CRITICAL: This module must load at PostConfigInit phase for IMPLEMENT_GLOBAL_SHADER
// to work. Game modules load too late - shaders must be registered before shader compilation.

#include "FlightGpuComputeModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "GlobalShader.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlightGpuCompute, Log, All);

bool FFlightGpuComputeModule::bShadersRegistered = false;

FGlobalShaderMap* FFlightGpuComputeModule::TryGetFlightGlobalShaderMap()
{
	if (!FApp::CanEverRender())
	{
		return nullptr;
	}

	const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel];
	if (ShaderPlatform == SP_NumPlatforms || GGlobalShaderMap[ShaderPlatform] == nullptr)
	{
		return nullptr;
	}

	return GetGlobalShaderMap(ShaderPlatform);
}

void FFlightGpuComputeModule::StartupModule()
{
#if WITH_FLIGHT_COMPUTE_SHADERS
	UE_LOG(LogFlightGpuCompute, Log,
		TEXT("FlightGpuCompute: Registering shader directory..."));

	// Map /FlightProject virtual path to our project's Shaders directory
	// This allows IMPLEMENT_GLOBAL_SHADER to find "/FlightProject/Private/FlightTestCompute.usf"
	FString PluginShaderDir = FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("Shaders"));

	if (FPaths::DirectoryExists(PluginShaderDir))
	{
		const FString VirtualPath = TEXT("/FlightProject");
		if (!AllShaderSourceDirectoryMappings().Contains(VirtualPath))
		{
			AddShaderSourceDirectoryMapping(VirtualPath, PluginShaderDir);
			UE_LOG(LogFlightGpuCompute, Log,
				TEXT("FlightGpuCompute: Mapped /FlightProject -> %s"), *PluginShaderDir);
		}
		else
		{
			UE_LOG(LogFlightGpuCompute, Log,
				TEXT("FlightGpuCompute: Shader directory already mapped for /FlightProject"));
		}

		bShadersRegistered = true;
	}
	else
	{
		UE_LOG(LogFlightGpuCompute, Warning,
			TEXT("FlightGpuCompute: Shader directory not found: %s"), *PluginShaderDir);
	}
#else
	UE_LOG(LogFlightGpuCompute, Log,
		TEXT("FlightGpuCompute: Compute shaders not available on this platform"));
#endif
}

void FFlightGpuComputeModule::ShutdownModule()
{
	UE_LOG(LogFlightGpuCompute, Log, TEXT("FlightGpuCompute: Module shutdown"));
}

IMPLEMENT_MODULE(FFlightGpuComputeModule, FlightGpuCompute)
