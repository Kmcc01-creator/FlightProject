# FlightProject Python Module
# Auto-imports common modules when FlightProject is imported

import unreal

# Submodules - imported on demand to avoid circular imports
from . import SwarmSetup
from . import Validation
from . import SceneSetup
from . import AssetTools
from . import DataReload
from . import Bootstrap
from . import SchemaTools
from . import PIETrace
from . import VexTools

__all__ = [
    'SwarmSetup',
    'Validation',
    'SceneSetup',
    'AssetTools',
    'DataReload',
    'Bootstrap',
    'SchemaTools',
    'PIETrace',
    'VexTools',
]

def initialize():
    """One-time initialization for FlightProject scripting."""
    unreal.log("FlightProject scripting module loaded")

def get_project_paths():
    """Return commonly used project paths."""
    return {
        'content': '/Game/',
        'data': '/Game/Data/',
        'maps': '/Game/Maps/',
        'swarm_plugin': '/SwarmEncounter/',
        'csv_files': [
            '/Game/Data/FlightLightingConfig',
            '/Game/Data/FlightAutopilotConfig',
            '/Game/Data/FlightSpatialLayout',
            '/Game/Data/FlightSpatialProcedural',
        ]
    }
