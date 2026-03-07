# FlightProject Bootstrap Automation
# Python bridge to C++ UFlightWorldBootstrapSubsystem and UFlightSwarmSpawnerSubsystem
import unreal


def run_bootstrap():
    """
    Run the full world bootstrap sequence via C++.
    - Resumes Mass simulation
    - Applies night environment lighting
    - Ensures spatial layout director exists
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            unreal.FlightScriptingLibrary.run_bootstrap(world)
            unreal.log("Bootstrap completed")
            return True
        else:
            unreal.log_warning("No editor world available")
            return False
    except AttributeError:
        unreal.log_warning("FlightScriptingLibrary not available - rebuild C++ module")
        return False


def spawn_swarm() -> int:
    """
    Spawn the initial swarm entities via Mass Entity framework.
    Returns the number of entities spawned.
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            count = unreal.FlightScriptingLibrary.spawn_initial_swarm(world)
            unreal.log(f"Spawned {count} swarm entities")
            return count
        else:
            unreal.log_warning("No editor world available")
            return 0
    except AttributeError:
        unreal.log_warning("FlightScriptingLibrary not available - rebuild C++ module")
        return 0


def initialize_gpu_swarm(entity_count: int = 500000):
    """Initialize the 3D GPU SPH simulation with massive entity scale."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            unreal.FlightScriptingLibrary.initialize_gpu_swarm(world, entity_count)
            unreal.log(f"GPU Swarm initialized with {entity_count} entities")
            return True
        return False
    except AttributeError:
        unreal.log_warning("FlightScriptingLibrary not available")
        return False


def clear_swarm():
    """Destroy all swarm entities."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            unreal.FlightScriptingLibrary.clear_all_swarm_entities(world)
            unreal.log("Swarm cleared")
    except AttributeError:
        unreal.log_warning("FlightScriptingLibrary not available")


def get_swarm_count() -> int:
    """Get current swarm entity count."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            return unreal.FlightScriptingLibrary.get_swarm_entity_count(world)
        return 0
    except AttributeError:
        return 0


def rebuild_spatial_layout():
    """Trigger rebuild of the spatial layout director."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            unreal.FlightScriptingLibrary.rebuild_spatial_layout(world)
            unreal.log("Spatial layout rebuilt")
    except AttributeError:
        unreal.log_warning("FlightScriptingLibrary not available")


def full_setup():
    """
    Complete setup sequence for testing:
    1. Run bootstrap (lighting, spatial layout)
    2. Spawn swarm entities
    """
    unreal.log("=== Full Flight Setup ===")

    # Run bootstrap first
    if not run_bootstrap():
        unreal.log_error("Bootstrap failed")
        return False

    # Then spawn swarm
    count = spawn_swarm()
    if count == 0:
        unreal.log_warning("No swarm entities spawned")

    unreal.log(f"=== Setup Complete: {count} entities ===")
    return True


def status():
    """Print current bootstrap/swarm status."""
    unreal.log("=== Flight Status ===")

    swarm_count = get_swarm_count()
    unreal.log(f"  Swarm entities: {swarm_count}")

    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            layout_count = unreal.FlightScriptingLibrary.get_spatial_layout_row_count(world)
            unreal.log(f"  Spatial layout rows: {layout_count}")

            data_loaded = unreal.FlightScriptingLibrary.is_data_fully_loaded(world)
            unreal.log(f"  Data subsystem: {'All loaded' if data_loaded else 'Incomplete'}")
    except AttributeError:
        unreal.log_warning("  FlightScriptingLibrary not available")
