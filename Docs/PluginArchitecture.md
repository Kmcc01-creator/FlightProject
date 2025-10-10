# Unreal Engine 5 Plugin Architecture (FlightProject Notes)

## Why Plugins Matter
- **Modularity**: Plugins encapsulate optional features (Mass AI, Niagara, Chaos) without bloating the core `Engine` module. You can enable only what you need per project.
- **Update cadence**: Experimental systems ship as plugins so Epic can iterate faster without breaking engine ABI. UE updates often tweak plugin APIs, so keeping code in plugins reduces merge overhead.
- **Runtime flexibility**: Plugins can be loaded per-platform or per-target. In `.uproject`, each entry can specify target allow/deny lists, runtime vs. editor only, and whether to include content.

## Types of Plugins
1. **Engine Plugins** (`Engine/Plugins/*`)
   - Ship with the engine source/build. Categories include Runtime, Editor, AI, Experimental, etc.
   - Examples we rely on: `MassGameplay` (bundles MassActors/MassSimulation), `MassAI`, `StateTree`, `Niagara`.
2. **Project Plugins** (`[Project]/Plugins/*`)
   - Checked into your project for custom features, tools, or third-party integrations.
   - Can distribute content (materials, Blueprints) and C++ modules together.
3. **Marketplace Plugins**
   - Installed into the engine or project via the Epic marketplace. Functionally identical to project plugins.

## Plugin Anatomy
- **`.uplugin` Descriptor**: Similar to `.uproject`; declares modules, dependencies, loading phases, and editor vs. runtime behavior.
- **Modules**: Each plugin can expose multiple modules (Runtime, Editor, Developer, ThirdParty). UE builds the modules based on the active target rules.
- **Dependencies**: Modules list other modules they depend on. UnrealBuildTool resolves these graphs when generating project files.
- **Activation**: Enabled via `.uproject` (project-wide) or `DefaultEngine.ini` under `[/Script/PluginName.PluginGameSettings]` for runtime toggles.

## Mass Plugins Overview (Key for FlightProject)
- **MassGameplay** (Runtime): Bundles MassEntity runtime modules, MassActors, MassSimulation, MassMovement, LOD, replication, etc.
- **MassAI** (AI): Behavior supplements, nav integration, debugging, and editor tooling for Mass-based agents.
- **StateTree**: Finite-state logic authoring tool; plugin includes both runtime and editor modules.
- **Note**: The legacy `MassEntity` plugin is deprecated in UE 5.5+—the code now lives in the core runtime, so no plugin entry is required.

## Best Practices
- **Enable only what you use**: Every plugin adds initialization cost and build time. Start with MassGameplay/MassAI/StateTree and expand as features demand.
- **Check platform support**: Some plugins ship editor-only or Windows-only. Consult the `.uplugin` for `SupportedTargetPlatforms` before enabling on Linux.
- **Version tolerance**: When porting to a new UE release, skim plugin changelogs. Experimental plugins can rename modules or shift responsibilities (e.g., MassEntity -> core runtime in UE 5.5).
- **Custom plugins**: For reusable FlightProject systems (e.g., Mass processors), consider promoting them into a project-level plugin once mature to keep the runtime module lean.
- **CI Integration**: Ensure build scripts (e.g., `./Scripts/generate_project_files.sh`) run after changing plugin dependencies so Visual Studio Code and UBT have an up-to-date module graph.

## References
- UE Docs: [Plugins Architecture](https://docs.unrealengine.com/5.3/en-US/plugins-in-unreal-engine/)
- Mass Framework docs & GDC talks (Mass AI, City Sample) for broader design context.
- FlightProject internal docs: `Docs/ProjectSpecification.md`, `Docs/RebuildAndOverview.md` for how these plugins integrate with our systems.
