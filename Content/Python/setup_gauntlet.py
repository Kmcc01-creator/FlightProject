# Gauntlet Map Setup Script
# Procedurally generates the testing playground for the GPU Swarm.
import unreal
import importlib
import random
from FlightProject import Bootstrap

# Force reload Bootstrap in case the module was cached by the editor
importlib.reload(Bootstrap)

def hard_clear_level():
    """Remove almost everything to ensure a clean slate for the GPU simulation."""
    editor_actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = editor_actor_sub.get_all_level_actors()
    removed = 0
    
    # Essential classes we want to keep
    keep_classes = [
        "WorldSettings", 
        "LevelScriptActor", 
        "AtmosphericFog", 
        "SkyLight", 
        "DirectionalLight",
        "PostProcessVolume", 
        "SkyAtmosphere",
        "VolumetricCloud"
    ]
    
    for actor in actors:
        class_name = actor.get_class().get_name()
        if class_name not in keep_classes:
            editor_actor_sub.destroy_actor(actor)
            removed += 1
            
    unreal.log(f"Hard Clear: Removed {removed} non-essential actors.")

def spawn_obstacle(name, location, rotation, scale, mesh_path="/Engine/BasicShapes/Sphere.Sphere"):
    """Helper to spawn a static mesh actor with a visible mesh."""
    editor_actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = editor_actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, location, rotation)
    
    if actor:
        actor.set_actor_label(name)
        actor.set_actor_scale3d(scale)
        
        # Set the mesh component's static mesh
        mesh_asset = unreal.EditorAssetLibrary.load_asset(mesh_path)
        if mesh_asset:
            actor.static_mesh_component.set_static_mesh(mesh_asset)
        else:
            unreal.log_warning(f"Failed to load mesh: {mesh_path}")
            
    return actor

def setup_visualizer():
    """Ensure a Niagara System exists to render the swarm."""
    ns_path = "/Game/Effects/NS_SwarmVisualizer"
    
    # Auto-create if missing
    if not unreal.EditorAssetLibrary.does_asset_exist(ns_path):
        unreal.log("NS_SwarmVisualizer missing. Attempting to scaffold...")
        try:
            import create_visualizer
            importlib.reload(create_visualizer)
            create_visualizer.create_swarm_visualizer()
        except ImportError:
            unreal.log_error("Could not find create_visualizer.py script!")
            return

    editor_actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # Check if one already exists in the level
    for actor in editor_actor_sub.get_all_level_actors():
        if actor.get_actor_label() == "SwarmVisualizer":
            unreal.log("SwarmVisualizer actor already in level.")
            return

    # Spawn it
    ns_asset = unreal.EditorAssetLibrary.load_asset(ns_path)
    if ns_asset:
        # Note: We use spawn_actor_from_class which works for NiagaraActor
        actor = editor_actor_sub.spawn_actor_from_class(unreal.NiagaraActor, unreal.Vector(0,0,0))
        if actor:
            actor.set_actor_label("SwarmVisualizer")
            actor.niagara_component.set_asset(ns_asset)
            unreal.log("Spawned SwarmVisualizer actor.")
    else:
        unreal.log_warning(f"Failed to load Niagara asset at {ns_path}")

def setup_gauntlet(entity_count=100000):
    unreal.log("=== Initializing The Gauntlet Map ===")
    
    # 1. Ensure we are in the correct map
    map_path = "/Game/Maps/PersistentFlightTest"
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    
    # 2. Clear everything
    hard_clear_level()
    
    # 3. Mark the level as a gauntlet test for the GameMode
    unreal_editor_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = unreal_editor_sub.get_editor_world()
    world_settings = world.get_world_settings()
    
    if world_settings:
        tags = world_settings.get_editor_property("tags")
        if "GauntletTest" not in tags:
            tags.append("GauntletTest")
            world_settings.set_editor_property("tags", tags)
            unreal.log("Tagged WorldSettings with 'GauntletTest'")

    # 4. Spawn Obstacles (SDF Candidates)
    # Visual markers for our 3D SDF-driven swarm
    
    # Large Torus Gate (using a ring of spheres or a cylinder if torus unavailable)
    spawn_obstacle("Entry_Gate", unreal.Vector(0, 0, 2000), unreal.Rotator(90, 0, 0), unreal.Vector(10, 10, 10), "/Engine/BasicShapes/Cylinder.Cylinder")
    
    # Random Sphere Field
    for i in range(20):
        pos = unreal.Vector(
            random.uniform(-15000, 15000),
            random.uniform(-15000, 15000),
            random.uniform(500, 8000)
        )
        spawn_obstacle(f"Sphere_Obs_{i}", pos, unreal.Rotator(0,0,0), unreal.Vector(4, 4, 4), "/Engine/BasicShapes/Sphere.Sphere")

    # 5. Setup Visualization
    setup_visualizer()

    # 6. Initialize the Swarm for the simulation
    if hasattr(Bootstrap, "initialize_gpu_swarm"):
        if Bootstrap.initialize_gpu_swarm(entity_count):
            unreal.log(f"Gauntlet set up. Swarm initialized with {entity_count} GPU entities.")
        else:
            unreal.log_error("Failed to initialize GPU swarm. Is the C++ module built?")
    else:
        unreal.log_error("Bootstrap module missing 'initialize_gpu_swarm'. Python caching issue likely.")

if __name__ == "__main__":
    setup_gauntlet()
