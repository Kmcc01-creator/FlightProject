# FlightProject Data Table and Asset Validation
import unreal
from pathlib import Path

REQUIRED_SWARM_TRAITS = {
    "FlightSwarmTrait",
}

STARTUP_PROFILES = {
    "DA_FlightStartup_DefaultSandbox": "/Game/Data/StartupProfiles/DA_FlightStartup_DefaultSandbox",
    "DA_FlightStartup_GauntletGpuSwarm": "/Game/Data/StartupProfiles/DA_FlightStartup_GauntletGpuSwarm",
}

# Data Table asset paths (migrated from CSV)
DATA_TABLES = {
    'DT_LightingConfig': '/Game/Data/DT_LightingConfig',
    'DT_AutopilotConfig': '/Game/Data/DT_AutopilotConfig',
    'DT_SpatialLayout': '/Game/Data/DT_SpatialLayout',
    'DT_ProceduralAnchor': '/Game/Data/DT_ProceduralAnchor',
}

def validate_data_tables() -> list:
    """
    Validate all Data Table assets exist and have data.
    Returns list of issues found.
    """
    issues = []

    for table_name, asset_path in DATA_TABLES.items():
        # Check if asset exists in content browser
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            issues.append(f"MISSING: {asset_path}")
            continue

        # Load and validate the data table
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset is None:
            issues.append(f"LOAD_FAILED: {asset_path}")
            continue

        # Check if it's a DataTable
        if not isinstance(asset, unreal.DataTable):
            issues.append(f"WRONG_TYPE: {asset_path} is not a DataTable")
            continue

        # Get row names to verify data exists
        row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(asset)
        if len(row_names) == 0:
            issues.append(f"EMPTY: {asset_path} has no rows")
        else:
            unreal.log(f"  {table_name}: {len(row_names)} rows")

    return issues


def validate_swarm_assets() -> list:
    """Validate SwarmEncounter plugin assets are configured correctly."""
    issues = []

    # Check MassEntityConfigAsset
    config_path = "/SwarmEncounter/DA_SwarmDroneConfig"
    if not unreal.EditorAssetLibrary.does_asset_exist(config_path):
        issues.append(f"MISSING: {config_path}")
    else:
        asset = unreal.EditorAssetLibrary.load_asset(config_path)
        if asset:
            unreal.log(f"Found swarm config: {config_path}")
            if hasattr(unreal, "FlightScriptingLibrary"):
                try:
                    trait_names = set(unreal.FlightScriptingLibrary.get_mass_entity_config_trait_class_names(config_path))
                    missing_traits = sorted(REQUIRED_SWARM_TRAITS - trait_names)
                    for trait_name in missing_traits:
                        issues.append(f"MISSING_TRAIT: {config_path} missing {trait_name}")
                except Exception as exc:
                    issues.append(f"TRAIT_CHECK_FAILED: {config_path} -> {exc}")

    # Check GameFeatureData
    gf_path = "/SwarmEncounter/SwarmEncounter"
    if not unreal.EditorAssetLibrary.does_asset_exist(gf_path):
        issues.append(f"MISSING: {gf_path}")

    return issues


def validate_startup_profiles() -> list:
    """Validate startup profile assets exist and load."""
    issues = []

    for profile_name, asset_path in STARTUP_PROFILES.items():
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            issues.append(f"MISSING: {asset_path}")
            continue

        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset is None:
            issues.append(f"LOAD_FAILED: {asset_path}")
            continue

        if not isinstance(asset, unreal.FlightStartupProfile):
            issues.append(f"WRONG_TYPE: {asset_path} is not a FlightStartupProfile")
            continue

        unreal.log(f"Found startup profile: {profile_name} -> {asset_path}")

    return issues


def run_all_validation() -> bool:
    """
    Run all validation checks. Returns True if all pass.
    Called from init_unreal.py on editor startup.
    """
    unreal.log("=== FlightProject Validation ===")
    all_issues = []

    # 1. Data Tables
    unreal.log("Checking Data Tables...")
    table_issues = validate_data_tables()
    all_issues.extend(table_issues)

    # 2. Swarm assets
    swarm_issues = validate_swarm_assets()
    all_issues.extend(swarm_issues)

    # 3. Startup profiles
    startup_profile_issues = validate_startup_profiles()
    all_issues.extend(startup_profile_issues)

    # Report results
    if all_issues:
        unreal.log_error(f"Validation found {len(all_issues)} issues:")
        for issue in all_issues:
            unreal.log_error(f"  - {issue}")
        return False
    else:
        unreal.log("All validation checks passed")
        return True
