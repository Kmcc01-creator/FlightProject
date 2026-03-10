# FlightProject Editor Startup Script
# This runs automatically when the Unreal Editor loads
import unreal

def on_editor_startup():
    """Main entry point for editor initialization."""
    unreal.log("=== FlightProject Editor Startup ===")

    # Import the FlightProject module (handles submodule loading)
    try:
        import FlightProject
        FlightProject.initialize()
    except ImportError as e:
        unreal.log_error(f"Failed to import FlightProject module: {e}")
        return

    # 1. Ensure required assets exist before validation.
    from FlightProject import AssetTools
    AssetTools.ensure_swarm_encounter_assets()
    try:
        AssetTools.ensure_flight_startup_profiles()
    except Exception as e:
        unreal.log_warning(f"Failed to ensure startup profile assets: {e}")

    # 2. Validate data files
    from FlightProject import Validation
    validation_ok = Validation.run_all_validation()

    # 3. Ensure schema-based contracts (PoC: Niagara + render profile manifest)
    from FlightProject import SchemaTools
    schema_issues = SchemaTools.ensure_manifest_requirements(create_missing=True)
    generated_gpu_contracts = unreal.FlightScriptingLibrary.export_generated_gpu_resource_contracts()
    if not generated_gpu_contracts:
        unreal.log_warning("Failed to export generated GPU resource contracts include")

    # 4. Arm PIE tracing defaults and reporting hooks
    from FlightProject import PIETrace
    if not PIETrace.ensure_blutility_loaded():
        unreal.log_warning(
            "PIETrace: Blutility module unavailable at startup; PIE end delegate reporting may not bind in this session."
        )
    PIETrace.arm_default_observability(auto_export=True, log_each_event=False)

    # 5. Print Data Table summary
    from FlightProject import DataReload
    DataReload.print_data_summary()

    # 6. Start VEX hot-reload watcher
    try:
        from FlightProject import VexHotReload
        VexHotReload.start()
    except Exception as e:
        unreal.log_warning(f"Failed to start VexHotReload: {e}")

    # Report status
    if validation_ok and not schema_issues:
        unreal.log("=== FlightProject Ready ===")
    else:
        unreal.log_warning("=== FlightProject Started with Warnings ===")

    # Provide quick reference
    unreal.log("")
    unreal.log("Quick commands (Output Log console):")
    unreal.log("  from FlightProject import SceneSetup")
    unreal.log("  SceneSetup.setup_swarm_test()")
    unreal.log("  SceneSetup.list_flight_actors()")
    unreal.log("")
    unreal.log("  from FlightProject import DataReload")
    unreal.log("  DataReload.reload()  # Hot-reload configs")
    unreal.log("")
    unreal.log("  from FlightProject import PIETrace")
    unreal.log("  PIETrace.arm_default_observability()")
    unreal.log("  PIETrace.get_event_count()  # while PIE is running")
    unreal.log("  PIETrace.export_trace()     # manual export while PIE is running")
    unreal.log("  PIETrace.find_latest_auto_export_path()")
    unreal.log("")
    unreal.log("  from FlightProject import VexTools")
    unreal.log("  VexTools.get_contract_symbols()")
    unreal.log("  VexTools.validate_source('@velocity += normalize(@position) * 25.0;')")
    unreal.log("  VexTools.export_validation_report('@velocity += @foo;')")

# Run on import (editor startup)
on_editor_startup()
