# Legacy script - now delegates to FlightProject.AssetTools
# Kept for backwards compatibility with any external references
import unreal

def create_swarm_game_feature_data():
    """Create SwarmEncounter GameFeatureData asset."""
    try:
        from FlightProject import AssetTools
        AssetTools.create_game_feature_data("SwarmEncounter")
    except ImportError:
        # Fallback if FlightProject module not available
        unreal.log_warning("FlightProject module not loaded - using inline implementation")
        _create_inline()


def _create_inline():
    """Inline fallback implementation."""
    asset_name = "SwarmEncounter"
    package_path = "/SwarmEncounter"
    full_path = f"{package_path}/{asset_name}"

    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.log(f"GameFeatureData already exists: {full_path}")
        return

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)

    new_asset = asset_tools.create_asset(
        asset_name=asset_name,
        package_path=package_path,
        asset_class=unreal.GameFeatureData,
        factory=factory
    )

    if new_asset:
        unreal.log(f"Created GameFeatureData: {full_path}")
        unreal.EditorAssetLibrary.save_loaded_asset(new_asset)
    else:
        unreal.log_error(f"Failed to create GameFeatureData at {full_path}")


if __name__ == "__main__":
    create_swarm_game_feature_data()
