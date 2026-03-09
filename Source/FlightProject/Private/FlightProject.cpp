#include "FlightProject.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ShaderCore.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "Verse/FlightVerseAssemblerPass.h"
#include "uLang/Toolchain/ModularFeatureManager.h"
#endif

#include "Core/FlightLogging.h"
#include "UI/FlightLogCapture.h"

DEFINE_LOG_CATEGORY(LogFlightProject);
IMPLEMENT_PRIMARY_GAME_MODULE(FFlightProjectModule, FlightProject, "FlightProject");

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
namespace
{
	TUniquePtr<uLang::TModularFeatureRegHandle<Flight::Verse::FFlightVerseAssemblerPass>> GVerseAssemblerPassHandle;
}
#endif

void FFlightProjectModule::StartupModule()
{
    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module initialized"));

    // Ensure global log capture is active
    Flight::Log::FGlobalLogCapture::Initialize();

    // Register our custom logging sinks
    using namespace Flight::Logging;
    FLogger::Get().AddSink(MakeShared<FInternalLogService>());
    FLogger::Get().AddSink(MakeShared<FUnrealLogSink>());

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

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	if (!GVerseAssemblerPassHandle.IsValid())
	{
		GVerseAssemblerPassHandle = MakeUnique<uLang::TModularFeatureRegHandle<Flight::Verse::FFlightVerseAssemblerPass>>();
		UE_LOG(LogFlightProject, Log, TEXT("Registered experimental Verse IAssemblerPass scaffold."));
	}
#endif
}

void FFlightProjectModule::ShutdownModule()
{
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	if (GVerseAssemblerPassHandle.IsValid())
	{
		GVerseAssemblerPassHandle.Reset();
		UE_LOG(LogFlightProject, Log, TEXT("Unregistered experimental Verse IAssemblerPass scaffold."));
	}
#endif

    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module shut down"));
}
