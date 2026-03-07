// FlightLogTab.h
// Editor tab registration for the Flight Log Viewer
// Provides a dockable window in the UE editor

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"
#include "FlightLogViewer.h"
#include "FlightLogCapture.h"
#include "FlightLogCategories.h"

namespace Flight::Log
{
    // Tab identifier
    static const FName FlightLogTabName(TEXT("FlightLogViewer"));

    // ============================================================================
    // Tab Registration
    // ============================================================================

    class FFlightLogTabManager
    {
    public:
        static FFlightLogTabManager& Get()
        {
            static FFlightLogTabManager Instance;
            return Instance;
        }

        void Register()
        {
            if (bRegistered)
            {
                return;
            }

            // Initialize global log capture
            FGlobalLogCapture::Initialize();

            // Register the nomad tab spawner
            FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
                FlightLogTabName,
                FOnSpawnTab::CreateRaw(this, &FFlightLogTabManager::SpawnTab))
                .SetDisplayName(FText::FromString(TEXT("Flight Log Viewer")))
                .SetTooltipText(FText::FromString(TEXT("View and filter FlightProject logs")))
                .SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
                .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Log.TabIcon"));

            bRegistered = true;

            UE_LOG(LogFlightUI, Log, TEXT("FlightLogTabManager registered"));
        }

        void Unregister()
        {
            if (!bRegistered)
            {
                return;
            }

            FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FlightLogTabName);
            FGlobalLogCapture::Shutdown();

            bRegistered = false;

            UE_LOG(LogFlightUI, Log, TEXT("FlightLogTabManager unregistered"));
        }

        void OpenTab()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(FlightLogTabName);
        }

        bool IsRegistered() const { return bRegistered; }

    private:
        TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
        {
            return SNew(SDockTab)
                .TabRole(ETabRole::NomadTab)
                .Label(FText::FromString(TEXT("Flight Logs")))
                [
                    SNew(SLogViewer)
                ];
        }

        bool bRegistered = false;
    };

    // ============================================================================
    // Menu Extension (for Tools menu)
    // ============================================================================

    class FFlightLogMenuExtension
    {
    public:
        static void Register()
        {
            // Add to Window menu under Developer Tools
            UToolMenus* ToolMenus = UToolMenus::Get();
            if (!ToolMenus)
            {
                return;
            }

            // Try to extend the Window menu
            UToolMenu* WindowMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Window");
            if (WindowMenu)
            {
                FToolMenuSection& Section = WindowMenu->FindOrAddSection("FlightProject");
                Section.AddMenuEntry(
                    "OpenFlightLogViewer",
                    FText::FromString(TEXT("Flight Log Viewer")),
                    FText::FromString(TEXT("Open the Flight Project log viewer with filtering")),
                    FSlateIcon(FAppStyle::GetAppStyleSetName(), "Log.TabIcon"),
                    FUIAction(FExecuteAction::CreateStatic(&FFlightLogMenuExtension::OpenLogViewer))
                );
            }

            UE_LOG(LogFlightUI, Log, TEXT("FlightLog menu extension registered"));
        }

        static void OpenLogViewer()
        {
            FFlightLogTabManager::Get().OpenTab();
        }
    };

    // ============================================================================
    // Convenience Functions
    // ============================================================================

    inline void RegisterLogViewerTab()
    {
        FFlightLogTabManager::Get().Register();
        FFlightLogMenuExtension::Register();
    }

    inline void UnregisterLogViewerTab()
    {
        FFlightLogTabManager::Get().Unregister();
    }

    inline void OpenLogViewerTab()
    {
        FFlightLogTabManager::Get().OpenTab();
    }

} // namespace Flight::Log
