#include "FlightProject.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogFlightProject);
IMPLEMENT_PRIMARY_GAME_MODULE(FFlightProjectModule, FlightProject, "FlightProject");

void FFlightProjectModule::StartupModule()
{
    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module initialized"));
}

void FFlightProjectModule::ShutdownModule()
{
    UE_LOG(LogFlightProject, Log, TEXT("FlightProject module shut down"));
}
