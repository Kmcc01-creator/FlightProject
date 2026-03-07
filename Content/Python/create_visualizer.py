# Script to scaffold the NS_SwarmVisualizer Niagara System
import unreal

def create_swarm_visualizer():
    package_path = "/Game/Effects"
    asset_name = "NS_SwarmVisualizer"
    full_path = f"{package_path}/{asset_name}"

    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.log(f"Asset already exists at {full_path}")
        return

    # 1. Create the Niagara System
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.NiagaraSystemFactoryNew()
    
    # We create an empty system first
    ns_system = asset_tools.create_asset(asset_name, package_path, unreal.NiagaraSystem, factory)
    
    if not ns_system:
        unreal.log_error("Failed to create Niagara System")
        return

    unreal.log(f"Created Niagara System: {full_path}")

    # 2. Add the Swarm Data Interface as a System constant
    # Note: In 5.x, adding emitters and modules via Python is very verbose.
    # We will provide the instructions for the manual "Last Mile" in the UI
    # but we can ensure the Data Interface class is known.
    
    unreal.EditorAssetLibrary.save_asset(full_path)
    
    unreal.log("=== NS_SwarmVisualizer Scaffolding Complete ===")
    unreal.log("Final Manual Steps in Unreal Editor:")
    unreal.log("1. Open /Game/Effects/NS_SwarmVisualizer")
    unreal.log("2. Add an Emitter (Generic -> Fountain or Empty)")
    unreal.log("3. In Emitter Properties: Set 'Sim Target' to 'GPU Compute Sim'")
    unreal.log("4. In Emitter Properties: Set 'Fixed Bounds' to something large (e.g. 50000)")
    unreal.log("5. In System Parameters: Add a new variable of type 'FlightSwarmSubsystem' (our NDI)")
    unreal.log("6. In Particle Update: Add a 'Set Variables' module")
    unreal.log("   - Map 'Particles.Position' to 'Swarm.GetDroidPosition(Index: ExecutionIndex)'")
    unreal.log("7. Set Spawn Count to 100,000 (Burst at System Spawn)")

if __name__ == "__main__":
    create_swarm_visualizer()
