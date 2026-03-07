// Copyright Kelly Rey Wilson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"
#include "UI/SwarmOrchestrator.h"

namespace Flight::Swarm
{
    // Tab identifier
    static const FName SwarmOrchestratorTabName(TEXT("SwarmOrchestrator"));

    /**
     * FSwarmOrchestratorTabManager
     * 
     * Handles the registration and spawning of the Swarm Orchestrator tab.
     */
    class FSwarmOrchestratorTabManager
    {
    public:
        static FSwarmOrchestratorTabManager& Get()
        {
            static FSwarmOrchestratorTabManager Instance;
            return Instance;
        }

        void Register()
        {
            if (bRegistered) return;

            FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
                SwarmOrchestratorTabName,
                FOnSpawnTab::CreateRaw(this, &FSwarmOrchestratorTabManager::SpawnTab))
                .SetDisplayName(FText::FromString(TEXT("Swarm Orchestrator")))
                .SetTooltipText(FText::FromString(TEXT("Live-code and control the GPU Swarm simulation")))
                .SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
                .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"));

            bRegistered = true;
        }

        void Unregister()
        {
            if (!bRegistered) return;
            FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SwarmOrchestratorTabName);
            bRegistered = false;
        }

        void OpenTab()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(SwarmOrchestratorTabName);
        }

    private:
        TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
        {
            return SNew(SDockTab)
                .TabRole(ETabRole::NomadTab)
                .Label(FText::FromString(TEXT("Swarm Orchestrator")))
                [
                    SNew(SSwarmOrchestrator)
                ];
        }

        bool bRegistered = false;
    };

    /**
     * Menu extension to add the tool to the Window menu.
     */
    class FSwarmOrchestratorMenuExtension
    {
    public:
        static void Register()
        {
            UToolMenus* ToolMenus = UToolMenus::Get();
            if (!ToolMenus) return;

            UToolMenu* WindowMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Window");
            if (WindowMenu)
            {
                FToolMenuSection& Section = WindowMenu->FindOrAddSection("FlightProject");
                Section.AddMenuEntry(
                    "OpenSwarmOrchestrator",
                    FText::FromString(TEXT("Swarm Orchestrator")),
                    FText::FromString(TEXT("Open the GPU Swarm VEX Live-Coding tool")),
                    FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
                    FUIAction(FExecuteAction::CreateStatic(&FSwarmOrchestratorMenuExtension::OpenTool))
                );
            }
        }

        static void OpenTool()
        {
            FSwarmOrchestratorTabManager::Get().OpenTab();
        }
    };

    /** Convenience entry points */
    inline void RegisterSwarmOrchestrator()
    {
        FSwarmOrchestratorTabManager::Get().Register();
        FSwarmOrchestratorMenuExtension::Register();
    }

    inline void UnregisterSwarmOrchestrator()
    {
        FSwarmOrchestratorTabManager::Get().Unregister();
    }

} // namespace Flight::Swarm
