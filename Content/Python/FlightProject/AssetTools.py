# FlightProject Asset Creation and Manipulation Tools
import unreal


def create_swarm_config(name: str, package_path: str = "/Game/Data/Swarms") -> unreal.Object:
    """
    Create a new MassEntityConfigAsset for swarm drones.
    The asset still needs traits configured manually in editor.
    """
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # Ensure the directory exists
    full_path = f"{package_path}/DA_{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.log(f"Asset already exists: {full_path}")
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
        unreal.log_warning("ACTION: Add traits (PathFollow, Visual, SwarmMember) in editor")
        unreal.EditorAssetLibrary.save_asset(full_path)
    else:
        unreal.log_error(f"Failed to create swarm config: {full_path}")

    return asset


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
            unreal.log_warning("ACTION: Add Flight traits to DA_SwarmDroneConfig")
            unreal.EditorAssetLibrary.save_asset(config_path)
    else:
        unreal.log(f"Asset exists: {config_path}")

    unreal.log("=== SwarmEncounter Assets Ready ===")
