# FlightProject Swarm Encounter Setup
# Legacy module - delegates to AssetTools for consistency
import unreal
from . import AssetTools


def run_setup():
    """
    Ensure SwarmEncounter assets exist.
    Delegates to AssetTools.ensure_swarm_encounter_assets().
    """
    AssetTools.ensure_swarm_encounter_assets()
