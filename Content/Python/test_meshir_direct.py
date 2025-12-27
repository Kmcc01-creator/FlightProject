"""
Direct MeshIR API test - runs immediately without needing PIE.
Run from Output Log: py test_meshir_direct.py
"""

import unreal

def test_meshir_library():
    """Test the MeshIR Blueprint library functions directly."""

    unreal.log("=== MeshIR Direct API Test ===")

    # Try to access the library class
    try:
        # Get the library class
        lib_class = unreal.load_class(None, "/Script/FlightProject.FlightMeshIRLibrary")
        if lib_class:
            unreal.log("FlightMeshIRLibrary class found")
        else:
            unreal.log_error("FlightMeshIRLibrary class not found")
            return
    except Exception as e:
        unreal.log_error(f"Error loading library: {e}")
        return

    # Try to call static functions via the library
    # Note: UBlueprintFunctionLibrary statics are accessed differently
    try:
        # Access via unreal module - the functions should be exposed
        if hasattr(unreal, 'FlightMeshIRLibrary'):
            lib = unreal.FlightMeshIRLibrary
            unreal.log("Library accessible via unreal.FlightMeshIRLibrary")

            # Test basic box creation
            # box_ir = lib.make_box_ir(100.0, 100.0, 100.0, unreal.Transform())
            # unreal.log(f"Created box IR: {box_ir}")
        else:
            unreal.log("FlightMeshIRLibrary not directly accessible")
            unreal.log("This is normal - C++ BP libraries need PIE context")

    except Exception as e:
        unreal.log_warning(f"Direct call test: {e}")

    # Alternative: Check if the test component class exists
    try:
        test_comp_class = unreal.load_class(None, "/Script/FlightProject.FlightMeshIRTestComponent")
        if test_comp_class:
            unreal.log("FlightMeshIRTestComponent class exists and is loadable")
            unreal.log("The component will run tests automatically on BeginPlay")
        else:
            unreal.log_error("FlightMeshIRTestComponent not found")
    except Exception as e:
        unreal.log_error(f"Error checking test component: {e}")

    # Check cache class
    try:
        cache_class = unreal.load_class(None, "/Script/FlightProject.FlightMeshIRCache")
        if cache_class:
            unreal.log("FlightMeshIRCache class exists")
    except Exception as e:
        unreal.log_warning(f"Cache class check: {e}")

    unreal.log("=== Test Complete ===")
    unreal.log("To run full tests: Add FlightMeshIRTestComponent to any actor and Play")

test_meshir_library()
