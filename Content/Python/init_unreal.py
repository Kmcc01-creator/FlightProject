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

    # 1. Validate data files
    from FlightProject import Validation
    validation_ok = Validation.run_all_validation()

    # 2. Ensure required assets exist
    from FlightProject import AssetTools
    AssetTools.ensure_swarm_encounter_assets()

    # 3. Print Data Table summary
    from FlightProject import DataReload
    DataReload.print_data_summary()

    # Report status
    if validation_ok:
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

# Run on import (editor startup)
on_editor_startup()
