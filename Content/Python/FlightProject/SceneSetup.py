# FlightProject Scene and Level Automation
import unreal

# Default test map path
DEFAULT_TEST_MAP = "/Game/Maps/L_SwarmTest"


def load_map(map_path: str = DEFAULT_TEST_MAP) -> bool:
    """Load a map in the editor."""
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        unreal.EditorLoadingAndSavingUtils.load_map(map_path)
        unreal.log(f"Loaded map: {map_path}")
        return True
    else:
        unreal.log_error(f"Map not found: {map_path}")
        return False


def setup_swarm_test(drone_count: int = 100):
    """
    Set up a standard swarm test scenario.
    Loads the test map and configures for swarm testing.
    """
    unreal.log(f"=== Setting up swarm test with {drone_count} drones ===")

    # Load test map if it exists
    if unreal.EditorAssetLibrary.does_asset_exist(DEFAULT_TEST_MAP):
        load_map(DEFAULT_TEST_MAP)
    else:
        unreal.log_warning(f"Test map not found: {DEFAULT_TEST_MAP}")
        unreal.log_warning("Using current map")

    unreal.log(f"Swarm test ready - configure spawn count in CSV or Data Asset")


def spawn_waypoint_path(name: str, center: unreal.Vector, radius: float = 5000.0,
                        altitude: float = 1200.0, point_count: int = 8):
    """
    Spawn a circular waypoint path in the editor world.
    Useful for quickly creating test flight paths.
    """
    import math

    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        unreal.log_error("No editor world available")
        return None

    # Spawn the waypoint path actor
    actor_class = unreal.load_class(None, "/Script/FlightProject.FlightWaypointPath")
    if not actor_class:
        unreal.log_error("FlightWaypointPath class not found - is the module loaded?")
        return None

    spawn_location = unreal.Vector(center.x, center.y, altitude)
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, spawn_location)

    if actor:
        actor.set_actor_label(name)
        unreal.log(f"Spawned waypoint path: {name} at {spawn_location}")
        unreal.log_warning("Configure spline points manually in editor")
    else:
        unreal.log_error(f"Failed to spawn waypoint path: {name}")

    return actor


def spawn_nav_buoy_region(name: str, center: unreal.Vector,
                          radius: float = 25000.0, buoy_count: int = 12):
    """Spawn a nav buoy region actor for navigation markers."""
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        unreal.log_error("No editor world available")
        return None

    actor_class = unreal.load_class(None, "/Script/FlightProject.FlightNavBuoyRegion")
    if not actor_class:
        unreal.log_error("FlightNavBuoyRegion class not found")
        return None

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, center)

    if actor:
        actor.set_actor_label(name)
        # Properties would need to be exposed via BlueprintReadWrite to set from Python
        unreal.log(f"Spawned nav buoy region: {name}")
    else:
        unreal.log_error(f"Failed to spawn nav buoy region: {name}")

    return actor


def spawn_swarm_anchor(name: str, location: unreal.Vector, drone_count: int = 32):
    """Spawn a swarm spawn anchor for Mass entity spawning."""
    actor_class = unreal.load_class(None, "/Script/FlightProject.FlightSpawnSwarmAnchor")
    if not actor_class:
        unreal.log_error("FlightSpawnSwarmAnchor class not found")
        return None

    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, location)

    if actor:
        actor.set_actor_label(name)
        unreal.log(f"Spawned swarm anchor: {name} (configure count in Data Asset or CSV)")
    else:
        unreal.log_error(f"Failed to spawn swarm anchor: {name}")

    return actor


def clear_actors_by_class(class_name: str) -> int:
    """Remove all actors of a given class from the current level."""
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    removed = 0

    for actor in actors:
        if actor.get_class().get_name() == class_name:
            actor.destroy_actor()
            removed += 1

    unreal.log(f"Removed {removed} actors of class {class_name}")
    return removed


def clear_all_flight_actors() -> int:
    """Remove all FlightProject actors from the current level."""
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    removed = 0

    flight_prefixes = ['FlightAI', 'FlightSpawn', 'FlightNav', 'FlightWaypoint', 'FlightSpatial']

    for actor in actors:
        class_name = actor.get_class().get_name()
        if any(class_name.startswith(prefix) for prefix in flight_prefixes):
            actor.destroy_actor()
            removed += 1

    unreal.log(f"Removed {removed} Flight actors")
    return removed


def list_flight_actors():
    """List all FlightProject actors in the current level."""
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    flight_actors = []

    for actor in actors:
        class_name = actor.get_class().get_name()
        if class_name.startswith('Flight') or class_name.startswith('AFlight'):
            flight_actors.append({
                'name': actor.get_actor_label(),
                'class': class_name,
                'location': actor.get_actor_location()
            })

    unreal.log(f"Found {len(flight_actors)} Flight actors:")
    for fa in flight_actors:
        unreal.log(f"  - {fa['name']} ({fa['class']}) at {fa['location']}")

    return flight_actors
