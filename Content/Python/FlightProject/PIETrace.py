"""PIE trace helpers for FlightProject."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import unreal

DEFAULT_MANUAL_EXPORT_PATH = "Saved/Flight/Observations/pie_entity_trace_manual.json"
_STATE_KEY = "_flightproject_pie_trace_state"


def _state() -> dict:
    existing = getattr(unreal, _STATE_KEY, None)
    if isinstance(existing, dict):
        return existing

    created = {
        "hooks_installed": False,
        "hook_mode": "",
        "baseline_export_path": "",
        "begin_callback": None,
        "end_callback": None,
        "end_poll_handle": None,
        "watcher_handle": None,
        "was_in_pie": False,
    }
    setattr(unreal, _STATE_KEY, created)
    return created


def _get_editor_subsystem():
    try:
        return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    except Exception:
        return None


def get_editor_world():
    """Return the editor world if available."""
    editor_subsystem = _get_editor_subsystem()
    if editor_subsystem:
        world = editor_subsystem.get_editor_world()
        if world:
            return world

    try:
        return unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        return None


def get_pie_world():
    """Return active PIE world, or None if PIE is not running."""
    editor_subsystem = _get_editor_subsystem()
    if editor_subsystem:
        world = editor_subsystem.get_game_world()
        if world:
            return world

    try:
        pie_worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if pie_worlds:
            return pie_worlds[0]
    except Exception:
        pass

    try:
        return unreal.EditorLevelLibrary.get_game_world()
    except Exception:
        return None


def _execute_console_command(command: str) -> bool:
    world = get_editor_world() or get_pie_world()
    if not world:
        unreal.log_warning(f"PIETrace: could not execute command (no world): {command}")
        return False

    unreal.SystemLibrary.execute_console_command(world, command, None)
    return True


def configure_tracing(enabled: bool = True, auto_export: bool = True, log_each_event: bool = False) -> None:
    """
    Configure PIE actor trace cvars for the next PIE run.
    These map to UFlightPIEObservationSubsystem cvars.
    """
    cvar_values = {
        "Flight.Trace.PIEActors.Enabled": int(enabled),
        "Flight.Trace.PIEActors.AutoExport": int(auto_export),
        "Flight.Trace.PIEActors.LogEachEvent": int(log_each_event),
    }
    for cvar_name, value in cvar_values.items():
        _execute_console_command(f"{cvar_name} {value}")


def _observations_dir() -> Path:
    saved_dir = unreal.Paths.project_saved_dir()
    return Path(saved_dir) / "Flight" / "Observations"


def find_latest_auto_export_path() -> str:
    """Return newest PIE auto-export json path, or empty string when no trace exists."""
    observation_dir = _observations_dir()
    if not observation_dir.is_dir():
        return ""

    matches = sorted(observation_dir.glob("pie_entity_trace_*.json"), key=lambda path: path.stat().st_mtime)
    return str(matches[-1]) if matches else ""


def export_trace(relative_output_path: str = DEFAULT_MANUAL_EXPORT_PATH) -> str:
    """
    Export current PIE entity trace snapshot.
    Returns absolute path on success or empty string on failure.
    """
    world = get_pie_world()
    if not world:
        unreal.log_warning("PIETrace: no PIE world available (is PIE active?)")
        return ""

    try:
        output_path = unreal.FlightScriptingLibrary.export_pie_entity_trace(world, relative_output_path)
        if output_path:
            unreal.log(f"PIETrace: exported trace -> {output_path}")
        else:
            unreal.log_warning("PIETrace: export returned empty path")
        return output_path
    except AttributeError:
        unreal.log_warning("PIETrace: FlightScriptingLibrary is unavailable - rebuild C++ module")
        return ""


def get_event_count() -> int:
    """Return current PIE trace event count."""
    world = get_pie_world()
    if not world:
        return 0

    try:
        return unreal.FlightScriptingLibrary.get_pie_entity_trace_event_count(world)
    except AttributeError:
        return 0


def _bind_delegate(delegate, callback: Callable) -> bool:
    if not delegate:
        return False

    if hasattr(delegate, "add_callable_unique"):
        delegate.add_callable_unique(callback)
        return True

    if hasattr(delegate, "add_callable"):
        delegate.add_callable(callback)
        return True

    return False


def _remove_delegate(delegate, callback: Callable) -> None:
    if not delegate:
        return
    if hasattr(delegate, "remove_callable"):
        delegate.remove_callable(callback)


def _get_editor_utility_subsystem():
    subsystem_type = _ensure_editor_utility_subsystem_type()
    if subsystem_type is None:
        return None
    try:
        return unreal.get_editor_subsystem(subsystem_type)
    except Exception:
        return None


def _ensure_editor_utility_subsystem_type():
    subsystem_type = getattr(unreal, "EditorUtilitySubsystem", None)
    if subsystem_type is not None:
        return subsystem_type

    if hasattr(unreal, "load_module"):
        try:
            unreal.load_module("Blutility")
        except Exception as exc:
            unreal.log_warning(f"PIETrace: failed to load Blutility module: {exc}")

    return getattr(unreal, "EditorUtilitySubsystem", None)


def ensure_blutility_loaded() -> bool:
    """Ensure Blutility editor module is loaded so PIE delegates are exposed."""
    return _ensure_editor_utility_subsystem_type() is not None


def _get_pie_delegates():
    subsystem = _get_editor_utility_subsystem()
    if not subsystem:
        return None, None

    begin_delegate = getattr(subsystem, "on_begin_pie", None)
    end_delegate = getattr(subsystem, "on_end_pie", None)
    return begin_delegate, end_delegate


def _on_pie_started(is_simulating: bool) -> None:
    state = _state()
    _clear_pending_end_poll()
    state["baseline_export_path"] = find_latest_auto_export_path()
    unreal.log(f"PIETrace: PIE started (simulate={is_simulating})")


def _on_pie_ended(is_simulating: bool) -> None:
    state = _state()
    _clear_pending_end_poll()
    previous_path = state.get("baseline_export_path", "")
    if hasattr(unreal, "register_ticker_callback"):
        attempts_remaining = 20

        def _poll_for_export(_delta_seconds: float) -> bool:
            nonlocal attempts_remaining
            latest_path = find_latest_auto_export_path()
            if latest_path and latest_path != previous_path:
                unreal.log(f"PIETrace: PIE ended (simulate={is_simulating}), trace json -> {latest_path}")
                state["end_poll_handle"] = None
                return False

            attempts_remaining -= 1
            if attempts_remaining > 0:
                return True

            if latest_path:
                unreal.log_warning(
                    f"PIETrace: PIE ended (simulate={is_simulating}), no new trace file detected. Latest existing trace: {latest_path}"
                )
            else:
                unreal.log_warning(
                    "PIETrace: PIE ended and no trace file was found. "
                    "Ensure Flight.Trace.PIEActors.Enabled=1 and Flight.Trace.PIEActors.AutoExport=1."
                )
            state["end_poll_handle"] = None
            return False

        state["end_poll_handle"] = unreal.register_ticker_callback(_poll_for_export, 0.1)
        return

    latest_path = find_latest_auto_export_path()
    if latest_path and latest_path != previous_path:
        unreal.log(f"PIETrace: PIE ended (simulate={is_simulating}), trace json -> {latest_path}")
        return

    if latest_path:
        unreal.log_warning(
            f"PIETrace: PIE ended (simulate={is_simulating}), no new trace file detected. Latest existing trace: {latest_path}"
        )
        return

    unreal.log_warning(
        "PIETrace: PIE ended and no trace file was found. "
        "Ensure Flight.Trace.PIEActors.Enabled=1 and Flight.Trace.PIEActors.AutoExport=1."
    )


def _install_ticker_pie_watcher() -> bool:
    state = _state()
    if state.get("watcher_handle"):
        return True

    if not hasattr(unreal, "register_ticker_callback"):
        return False

    state["was_in_pie"] = bool(get_pie_world())

    def _watch_pie_state(_delta_seconds: float) -> bool:
        in_pie = bool(get_pie_world())
        was_in_pie = bool(state.get("was_in_pie"))
        if in_pie and not was_in_pie:
            _on_pie_started(False)
        elif not in_pie and was_in_pie:
            _on_pie_ended(False)
        state["was_in_pie"] = in_pie
        return True

    state["watcher_handle"] = unreal.register_ticker_callback(_watch_pie_state, 0.1)
    return True


def _remove_ticker_pie_watcher() -> None:
    state = _state()
    handle = state.get("watcher_handle")
    if not handle:
        return

    if hasattr(unreal, "unregister_ticker_callback"):
        unreal.unregister_ticker_callback(handle)
    state["watcher_handle"] = None
    state["was_in_pie"] = False


def install_pie_close_reporting() -> bool:
    """
    Install begin/end PIE callbacks that report the generated trace json path.
    Safe to call repeatedly.
    """
    state = _state()
    if state.get("hooks_installed"):
        return True

    begin_delegate, end_delegate = _get_pie_delegates()
    if begin_delegate and end_delegate:
        if not _bind_delegate(begin_delegate, _on_pie_started):
            unreal.log_warning("PIETrace: failed to bind begin PIE delegate")
            return False
        if not _bind_delegate(end_delegate, _on_pie_ended):
            _remove_delegate(begin_delegate, _on_pie_started)
            unreal.log_warning("PIETrace: failed to bind end PIE delegate")
            return False

        state["begin_callback"] = _on_pie_started
        state["end_callback"] = _on_pie_ended
        state["hook_mode"] = "delegate"
        state["hooks_installed"] = True
        unreal.log("PIETrace: installed PIE begin/end reporting hooks (delegate mode)")
        return True

    if _install_ticker_pie_watcher():
        state["hook_mode"] = "ticker"
        state["hooks_installed"] = True
        unreal.log_warning(
            "PIETrace: PIE delegates unavailable; using ticker-based PIE start/end watcher."
        )
        return True

    unreal.log_warning("PIETrace: failed to install PIE begin/end reporting hooks")
    return False


def uninstall_pie_close_reporting() -> None:
    """Remove PIE close reporting callbacks if installed."""
    state = _state()
    if not state.get("hooks_installed"):
        return

    hook_mode = state.get("hook_mode")
    if hook_mode == "delegate":
        begin_delegate, end_delegate = _get_pie_delegates()
        _remove_delegate(begin_delegate, state.get("begin_callback"))
        _remove_delegate(end_delegate, state.get("end_callback"))
    elif hook_mode == "ticker":
        _remove_ticker_pie_watcher()
    _clear_pending_end_poll()

    state["hooks_installed"] = False
    state["hook_mode"] = ""
    state["begin_callback"] = None
    state["end_callback"] = None


def arm_default_observability(auto_export: bool = True, log_each_event: bool = False) -> bool:
    """
    Enable default PIE observability flow:
      - cvars configured
      - PIE begin/end reporting installed
    """
    configure_tracing(enabled=True, auto_export=auto_export, log_each_event=log_each_event)
    return install_pie_close_reporting()


def _clear_pending_end_poll() -> None:
    state = _state()
    handle = state.get("end_poll_handle")
    if not handle:
        return

    if hasattr(unreal, "unregister_ticker_callback"):
        unreal.unregister_ticker_callback(handle)
    state["end_poll_handle"] = None


def print_quick_help() -> None:
    """Print concise quick-commands for PIE trace inspection."""
    unreal.log("PIETrace quick commands:")
    unreal.log("  PIETrace.arm_default_observability()")
    unreal.log("  PIETrace.get_event_count()  # while PIE is running")
    unreal.log("  PIETrace.export_trace()     # manual export while PIE is running")
    unreal.log("  PIETrace.find_latest_auto_export_path()")
