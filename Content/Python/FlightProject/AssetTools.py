# FlightProject Asset Creation and Manipulation Tools
import unreal

SWARM_TRAIT_CLASS_PATHS = [
    "/Script/SwarmEncounter.FlightSwarmTrait",
]

STARTUP_PROFILE_ASSET_PATHS = {
    "default_sandbox": "/Game/Data/StartupProfiles/DA_FlightStartup_DefaultSandbox",
    "gauntlet_gpu_swarm": "/Game/Data/StartupProfiles/DA_FlightStartup_GauntletGpuSwarm",
}

STARTUP_PROFILE_SPECS = {
    "DEFAULT_SANDBOX": {
        "ordinal": 0,
        "candidates": ["DefaultSandbox", "DEFAULT_SANDBOX", "Default_Sandbox"],
    },
    "GAUNTLET_GPU_SWARM": {
        "ordinal": 1,
        "candidates": ["GauntletGpuSwarm", "GAUNTLET_GPU_SWARM", "Gauntlet_GPU_Swarm"],
    },
    "LEGACY_AUTO": {
        "ordinal": 2,
        "candidates": ["LegacyAuto", "LEGACY_AUTO", "Legacy_Auto"],
    },
}


def resolve_startup_profile_enum(enum_name: str):
    """
    Resolve Flight startup profile values safely across Unreal Python enum naming styles.
    Falls back to the raw enum ordinal when the reflected enum type is unavailable.
    """
    normalized_name = enum_name.replace(" ", "_").upper()
    spec = STARTUP_PROFILE_SPECS.get(normalized_name)
    if spec is None:
        raise AttributeError(f"Unknown Flight startup profile '{enum_name}'")

    enum_type = getattr(unreal, "FlightStartupProfileType", None)
    if enum_type is None:
        enum_type = getattr(unreal, "EFlightStartupProfile", None)
    candidates = list(spec["candidates"])
    candidates.extend(
        [
            enum_name,
            normalized_name,
            enum_name.replace(" ", "_"),
        ]
    )

    if enum_type is not None:
        for candidate in candidates:
            if hasattr(enum_type, candidate):
                return getattr(enum_type, candidate)

        unreal.log_warning(
            f"EFlightStartupProfile is reflected but '{enum_name}' was not found; using raw ordinal {spec['ordinal']}"
        )
    else:
        unreal.log_warning(
            f"EFlightStartupProfile is not exposed to Unreal Python; using raw ordinal {spec['ordinal']} for '{enum_name}'"
        )

    return spec["ordinal"]


def ensure_mass_entity_config_traits(asset_path: str, trait_class_paths: list[str]) -> list[str]:
    """
    Ensure a MassEntityConfigAsset contains the requested trait classes.
    Requires the FlightProject editor shim in C++.
    """
    if not hasattr(unreal, "FlightScriptingLibrary"):
        return ["FlightScriptingLibrary unavailable"]

    try:
        return unreal.FlightScriptingLibrary.ensure_mass_entity_config_traits(asset_path, trait_class_paths)
    except Exception as exc:
        return [f"trait ensure failed for {asset_path}: {exc}"]


def create_swarm_config(name: str, package_path: str = "/Game/Data/Swarms") -> unreal.Object:
    """
    Create a new MassEntityConfigAsset for swarm drones and apply the default Flight trait set.
    """
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # Ensure the directory exists
    full_path = f"{package_path}/DA_{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.log(f"Asset already exists: {full_path}")
        issues = ensure_mass_entity_config_traits(full_path, SWARM_TRAIT_CLASS_PATHS)
        for issue in issues:
            unreal.log_warning(f"Swarm config trait ensure: {issue}")
        unreal.EditorAssetLibrary.save_asset(full_path)
        return unreal.EditorAssetLibrary.load_asset(full_path)

    # Create using DataAssetFactory
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.MassEntityConfigAsset)

    asset = asset_tools.create_asset(
        asset_name=f"DA_{name}",
        package_path=package_path,
        asset_class=unreal.MassEntityConfigAsset,
        factory=factory
    )

    if asset:
        unreal.log(f"Created swarm config: {full_path}")
        issues = ensure_mass_entity_config_traits(full_path, SWARM_TRAIT_CLASS_PATHS)
        for issue in issues:
            unreal.log_warning(f"Swarm config trait ensure: {issue}")
        unreal.EditorAssetLibrary.save_asset(full_path)
    else:
        unreal.log_error(f"Failed to create swarm config: {full_path}")

    return asset


def create_startup_profile(
    name: str,
    startup_profile,
    gauntlet_gpu_swarm_entity_count: int = 100000,
    description: str = "",
    package_path: str = "/Game/Data/StartupProfiles",
) -> unreal.Object:
    """
    Create or update a FlightStartupProfile data asset.
    """
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_name = f"DA_{name}"
    full_path = f"{package_path}/{asset_name}"

    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        asset = unreal.EditorAssetLibrary.load_asset(full_path)
        unreal.log(f"Startup profile already exists: {full_path}")
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.FlightStartupProfile)

        asset = asset_tools.create_asset(
            asset_name=asset_name,
            package_path=package_path,
            asset_class=unreal.FlightStartupProfile,
            factory=factory,
        )

        if asset:
            unreal.log(f"Created startup profile: {full_path}")
        else:
            unreal.log_error(f"Failed to create startup profile: {full_path}")
            return None

    if asset is None:
        unreal.log_error(f"Could not load startup profile asset: {full_path}")
        return None

    asset.set_editor_property("startup_profile", startup_profile)
    asset.set_editor_property("gauntlet_gpu_swarm_entity_count", max(1, int(gauntlet_gpu_swarm_entity_count)))
    asset.set_editor_property("description", description)
    unreal.EditorAssetLibrary.save_asset(full_path)
    return asset


def ensure_flight_startup_profiles() -> dict[str, unreal.Object]:
    """
    Ensure the standard Flight startup profile assets exist.
    """
    unreal.log("=== Ensuring Flight Startup Profiles ===")

    created = {
        "default_sandbox": create_startup_profile(
            name="FlightStartup_DefaultSandbox",
            startup_profile=resolve_startup_profile_enum("DEFAULT_SANDBOX"),
            gauntlet_gpu_swarm_entity_count=100000,
            description="Default sandbox startup. Runs reusable world bootstrap and initial swarm spawn.",
        ),
        "gauntlet_gpu_swarm": create_startup_profile(
            name="FlightStartup_GauntletGpuSwarm",
            startup_profile=resolve_startup_profile_enum("GAUNTLET_GPU_SWARM"),
            gauntlet_gpu_swarm_entity_count=100000,
            description="Gauntlet GPU swarm startup. Skips normal sandbox bootstrap and initializes the GPU swarm path.",
        ),
    }

    unreal.log("=== Flight Startup Profiles Ready ===")
    return created


def create_game_feature_data(plugin_name: str, package_path: str = None) -> unreal.Object:
    """
    Create a GameFeatureData asset for a GameFeatures plugin.
    """
    if package_path is None:
        package_path = f"/{plugin_name}"

    full_path = f"{package_path}/{plugin_name}"

    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.log(f"GameFeatureData already exists: {full_path}")
        return unreal.EditorAssetLibrary.load_asset(full_path)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)

    asset = asset_tools.create_asset(
        asset_name=plugin_name,
        package_path=package_path,
        asset_class=unreal.GameFeatureData,
        factory=factory
    )

    if asset:
        unreal.log(f"Created GameFeatureData: {full_path}")
        unreal.EditorAssetLibrary.save_asset(full_path)
    else:
        unreal.log_error(f"Failed to create GameFeatureData: {full_path}")

    return asset


def duplicate_and_modify(source_path: str, new_name: str, **properties) -> unreal.Object:
    """
    Duplicate an asset and modify its properties.
    Properties must be exposed via BlueprintReadWrite.
    """
    new_path = source_path.rsplit('/', 1)[0] + '/' + new_name

    if unreal.EditorAssetLibrary.does_asset_exist(new_path):
        unreal.log_warning(f"Target already exists: {new_path}")
        return unreal.EditorAssetLibrary.load_asset(new_path)

    if unreal.EditorAssetLibrary.duplicate_asset(source_path, new_path):
        asset = unreal.EditorAssetLibrary.load_asset(new_path)
        modified = 0

        for prop, value in properties.items():
            try:
                asset.set_editor_property(prop, value)
                modified += 1
            except Exception as e:
                unreal.log_warning(f"Could not set {prop}: {e}")

        unreal.EditorAssetLibrary.save_asset(new_path)
        unreal.log(f"Duplicated {source_path} -> {new_path} ({modified} properties set)")
        return asset
    else:
        unreal.log_error(f"Failed to duplicate: {source_path}")
        return None


def list_assets(folder: str, asset_class: str = None, recursive: bool = True) -> list:
    """
    List all assets in a folder, optionally filtered by class.
    """
    assets = unreal.EditorAssetLibrary.list_assets(folder, recursive=recursive)
    result = []

    for asset_path in assets:
        if asset_class:
            asset = unreal.EditorAssetLibrary.load_asset(asset_path)
            if asset and asset.get_class().get_name() == asset_class:
                result.append(asset_path)
        else:
            result.append(asset_path)

    return result


def rename_assets_by_pattern(folder: str, old_pattern: str, new_pattern: str) -> int:
    """Bulk rename assets matching a pattern."""
    assets = unreal.EditorAssetLibrary.list_assets(folder, recursive=True)
    renamed = 0

    for asset_path in assets:
        asset_name = asset_path.split('.')[-1]
        if old_pattern in asset_name:
            new_name = asset_name.replace(old_pattern, new_pattern)
            new_path = asset_path.rsplit('.', 1)[0].rsplit('/', 1)[0] + '/' + new_name
            if unreal.EditorAssetLibrary.rename_asset(asset_path, new_path):
                renamed += 1

    unreal.log(f"Renamed {renamed} assets ({old_pattern} -> {new_pattern})")
    return renamed


def ensure_swarm_encounter_assets():
    """
    Ensure all SwarmEncounter plugin assets exist.
    Called from init_unreal.py on editor startup.
    """
    unreal.log("=== Ensuring SwarmEncounter Assets ===")

    # 1. GameFeatureData
    create_game_feature_data("SwarmEncounter")

    # 2. MassEntityConfigAsset
    config_path = "/SwarmEncounter/DA_SwarmDroneConfig"
    if not unreal.EditorAssetLibrary.does_asset_exist(config_path):
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.MassEntityConfigAsset)

        asset = asset_tools.create_asset(
            asset_name="DA_SwarmDroneConfig",
            package_path="/SwarmEncounter",
            asset_class=unreal.MassEntityConfigAsset,
            factory=factory
        )

        if asset:
            unreal.log(f"Created: {config_path}")
    else:
        unreal.log(f"Asset exists: {config_path}")

    trait_issues = ensure_mass_entity_config_traits(config_path, SWARM_TRAIT_CLASS_PATHS)
    if trait_issues:
        for issue in trait_issues:
            unreal.log_warning(f"SwarmEncounter config: {issue}")
    else:
        unreal.log("SwarmEncounter config traits ensured")

    unreal.EditorAssetLibrary.save_asset(config_path)

    unreal.log("=== SwarmEncounter Assets Ready ===")
