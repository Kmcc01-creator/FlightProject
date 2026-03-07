#include "FlightProject.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ShaderCore.h"

#if WITH_EDITOR
#include "UI/FlightLogTab.h"
#include "UI/SwarmOrchestratorTab.h"
#endif

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
        const FString VirtualPath = TEXT("/FlightProject");
        if (!AllShaderSourceDirectoryMappings().Contains(VirtualPath))
        {
            AddShaderSourceDirectoryMapping(VirtualPath, ShaderDirectory);
            UE_LOG(LogFlightProject, Log, TEXT("Registered shader directory: %s -> %s"), *VirtualPath, *ShaderDirectory);
        }
    }
    else
    {
        UE_LOG(LogFlightProject, Warning,
            TEXT("Shader directory not found at %s - custom shaders unavailable"), *ShaderDirectory);
    }

#if WITH_EDITOR
    // Register Flight Log Viewer tab
    Flight::Log::RegisterLogViewerTab();

    // Register Swarm Orchestrator tab
    Flight::Swarm::RegisterSwarmOrchestrator();
#endif
}

void FFlightProjectModule::ShutdownModule()
{
#if WITH_EDITOR
    // Unregister tabs
    Flight::Log::UnregisterLogViewerTab();
    Flight::Swarm::UnregisterSwarmOrchestrator();
#endif

    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module shut down"));
}
