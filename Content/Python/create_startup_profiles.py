import unreal
import importlib.util
from pathlib import Path


def load_asset_tools_module():
    script_dir = Path(__file__).resolve().parent
    asset_tools_path = script_dir / "FlightProject" / "AssetTools.py"
    spec = importlib.util.spec_from_file_location("FlightProjectAssetTools", asset_tools_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Could not load AssetTools module from {asset_tools_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    try:
        asset_tools = load_asset_tools_module()
    except Exception as exc:
        unreal.log_error(f"Failed to load FlightProject AssetTools module: {exc}")
        raise

    created = asset_tools.ensure_flight_startup_profiles()
    for key, asset in created.items():
        unreal.log(f"startup profile [{key}] -> {asset.get_path_name() if asset else 'FAILED'}")


if __name__ == "__main__":
    main()
