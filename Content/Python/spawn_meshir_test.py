"""
Spawn an actor with the MeshIR test component.
Run from Output Log: py spawn_meshir_test.py
Or from Python console: exec(open('/path/to/spawn_meshir_test.py').read())
"""

import unreal

def spawn_meshir_test_actor():
    """Spawn an empty actor with UFlightMeshIRTestComponent attached."""

    # Get the editor world
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_editor_world()

    if not world:
        unreal.log_error("No editor world available")
        return None

    # Spawn an empty actor at origin
    spawn_location = unreal.Vector(0, 0, 500)
    spawn_rotation = unreal.Rotator(0, 0, 0)

    # Create actor
    actor_class = unreal.EditorAssetLibrary.load_blueprint_class("/Script/Engine.Actor")
    if not actor_class:
        # Fallback: spawn a basic actor
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.Actor,
            spawn_location,
            spawn_rotation
        )
    else:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.Actor,
            spawn_location,
            spawn_rotation
        )

    if not actor:
        unreal.log_error("Failed to spawn actor")
        return None

    actor.set_actor_label("MeshIR_TestActor")

    # Add the test component
    # Note: We need to add the component via the subsystem
    component_class = unreal.load_class(None, "/Script/FlightProject.FlightMeshIRTestComponent")

    if component_class:
        # Add component to actor
        component = actor.add_component_by_class(
            component_class,
            False,  # manual attachment
            unreal.Transform(),
            False   # deferred finish
        )
        if component:
            unreal.log("Successfully added FlightMeshIRTestComponent to actor")
            unreal.log("Enter PIE (Play In Editor) to run the tests")
        else:
            unreal.log_error("Failed to add component - try manual approach")
    else:
        unreal.log_error("Could not load FlightMeshIRTestComponent class")
        unreal.log("Try: Actor Details panel -> Add Component -> FlightMeshIRTestComponent")

    return actor

# Run it
actor = spawn_meshir_test_actor()
if actor:
    unreal.log(f"Spawned test actor: {actor.get_actor_label()}")
    unreal.log("Now press Play (Alt+P) to run MeshIR tests")
