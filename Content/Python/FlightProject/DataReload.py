# FlightProject Data Table Hot-Reload and Management
import unreal

# Data Table asset paths
DATA_TABLES = {
    'LightingConfig': '/Game/Data/DT_LightingConfig',
    'AutopilotConfig': '/Game/Data/DT_AutopilotConfig',
    'SpatialLayout': '/Game/Data/DT_SpatialLayout',
    'ProceduralAnchor': '/Game/Data/DT_ProceduralAnchor',
}


def reload_all_configs():
    """
    Reload all configurations via C++ FlightDataSubsystem.
    This is the primary hot-reload function.
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            unreal.FlightScriptingLibrary.reload_csv_configs(world)
            unreal.log("Data configs reloaded via C++ subsystem")
            return True
        else:
            unreal.log_warning("No editor world available")
            return False
    except AttributeError:
        unreal.log_warning("FlightScriptingLibrary not available - rebuild C++ module")
        return False


def reload_config(config_name: str) -> bool:
    """
    Reload a specific configuration.
    Valid names: Lighting, Autopilot, SpatialLayout, ProceduralAnchors
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            result = unreal.FlightScriptingLibrary.reload_csv_config(world, config_name)
            if result:
                unreal.log(f"Reloaded: {config_name}")
            else:
                unreal.log_warning(f"Failed to reload: {config_name}")
            return result
        return False
    except AttributeError:
        unreal.log_warning("FlightScriptingLibrary not available")
        return False


def is_data_loaded() -> bool:
    """Check if all data is loaded in C++."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            return unreal.FlightScriptingLibrary.is_data_fully_loaded(world)
        return False
    except AttributeError:
        return False


def print_data_summary():
    """Print summary of Data Table assets and their status."""
    unreal.log("=== Data Table Summary ===")

    for name, asset_path in DATA_TABLES.items():
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            unreal.log_warning(f"  {name}: MISSING")
            continue

        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset and isinstance(asset, unreal.DataTable):
            row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(asset)
            unreal.log(f"  {name}: {len(row_names)} rows")
        else:
            unreal.log_warning(f"  {name}: INVALID")

    # Check C++ subsystem status
    if is_data_loaded():
        unreal.log("  C++ Subsystem: All configs loaded")
    else:
        unreal.log_warning("  C++ Subsystem: Some configs missing")


def get_spatial_layout_count() -> int:
    """Get the number of spatial layout rows loaded in C++."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            return unreal.FlightScriptingLibrary.get_spatial_layout_row_count(world)
        return 0
    except AttributeError:
        return 0


def list_table_rows(table_name: str):
    """List all row names in a Data Table."""
    asset_path = DATA_TABLES.get(table_name)
    if not asset_path:
        unreal.log_error(f"Unknown table: {table_name}. Valid: {list(DATA_TABLES.keys())}")
        return

    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset and isinstance(asset, unreal.DataTable):
        row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(asset)
        unreal.log(f"=== {table_name} ({len(row_names)} rows) ===")
        for row in row_names:
            unreal.log(f"  - {row}")
    else:
        unreal.log_error(f"Failed to load: {asset_path}")


# Convenience aliases
reload = reload_all_configs
summary = print_data_summary
