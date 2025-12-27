#include "FlightProject.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogFlightProject);
IMPLEMENT_PRIMARY_GAME_MODULE(FFlightProjectModule, FlightProject, "FlightProject");

void FFlightProjectModule::StartupModule()
{
    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module initialized"));

    // Register shader directory with virtual path /FlightProject
    // This allows shaders to be referenced as "/FlightProject/Private/ShaderName.usf"
    const FString ShaderDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
    if (IFileManager::Get().DirectoryExists(*ShaderDirectory))
    {
        AddShaderSourceDirectoryMapping(TEXT("/FlightProject"), ShaderDirectory);
        UE_LOG(LogFlightProject, Log, TEXT("Registered shader directory: /FlightProject -> %s"), *ShaderDirectory);
    }
    else
    {
        UE_LOG(LogFlightProject, Warning,
            TEXT("Shader directory not found at %s - custom shaders unavailable"), *ShaderDirectory);
    }
}

void FFlightProjectModule::ShutdownModule()
{
    // Shader compiler shutdown moved to module shutdown
    // FShaderCompilingManager::Get().OnShaderCompilerShutdown();

    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module shut down"));
}
