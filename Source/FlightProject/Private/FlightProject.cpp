#include "FlightProject.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY(LogFlightProject);
IMPLEMENT_PRIMARY_GAME_MODULE(FFlightProjectModule, FlightProject, "FlightProject");

namespace
{
    constexpr const TCHAR* FlightProjectShaderVirtualPath = TEXT("/Shaders");
}

void FFlightProjectModule::StartupModule()
{
    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module initialized"));

#if USING_SHADER_COMPILER
    const FString ShaderDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
    if (IFileManager::Get().DirectoryExists(*ShaderDirectory))
    {
        FShaderDirectories::AddShaderSourceDirectoryMapping(FlightProjectShaderVirtualPath, ShaderDirectory);
        UE_LOG(LogFlightProject, Log, TEXT("Registered shader source directory: %s -> %s"), FlightProjectShaderVirtualPath, *ShaderDirectory);
    }
    else
    {
        UE_LOG(LogFlightProject, Warning, TEXT("Expected shader directory missing at %s; custom shaders will not compile until it exists."), *ShaderDirectory);
    }
#endif
}

void FFlightProjectModule::ShutdownModule()
{
#if USING_SHADER_COMPILER
    FShaderDirectories::RemoveShaderSourceDirectoryMapping(FlightProjectShaderVirtualPath);
#endif

    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module shut down"));
}
