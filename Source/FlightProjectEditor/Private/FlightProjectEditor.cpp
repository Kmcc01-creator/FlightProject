#include "Modules/ModuleManager.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/FlightLogTab.h"
#include "UI/SwarmOrchestratorTab.h"

namespace
{

bool ShouldRegisterEditorUi()
{
	return !IsRunningCommandlet()
		&& !FApp::IsUnattended()
		&& FApp::CanEverRender()
		&& FSlateApplication::IsInitialized()
		&& !FParse::Param(FCommandLine::Get(), TEXT("NullRHI"));
}

} // namespace

class FFlightProjectEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (!ShouldRegisterEditorUi())
        {
            return;
        }

        Flight::Log::RegisterLogViewerTab();
        Flight::Swarm::RegisterSwarmOrchestrator();
    }

    virtual void ShutdownModule() override
    {
        if (!ShouldRegisterEditorUi())
        {
            return;
        }

        Flight::Log::UnregisterLogViewerTab();
        Flight::Swarm::UnregisterSwarmOrchestrator();
    }
};

IMPLEMENT_MODULE(FFlightProjectEditorModule, FlightProjectEditor)
