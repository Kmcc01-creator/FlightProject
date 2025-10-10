# Linux Wayland & Hyprland Playbook

_Last updated: October 10, 2025_

This note tracks the experimental knobs we use to keep Unreal Engine responsive on CachyOS/Hyprland while prioritising a native Wayland stack. Treat the suggestions as opt-in toggles; the editor still ships with X11 defaults, so stage any risky changes behind the new script flags before promoting them to project-wide defaults.

## Current Constraints

- Unreal’s Linux build links against SDL2 with X11 defaults. Forcing Wayland (`SDL_VIDEODRIVER=wayland`) works when SDL can dynamically resolve the system’s `libSDL2.so`, but some Arch-based layouts expose only `libSDL2-2.0.so.0`. If SDL cannot find the stub, it falls back to X11 or crashes when opening menus. Use `FP_SDL_DYNAMIC_API` (or `--video-backend wayland`) so the launchers point SDL at a concrete library path before we start the editor.
- Native Wayland paths still ship with rough edges: pointer capture can fail, dropdowns may stop responding, and some UI hover handlers trigger crashes. Staying inside gamescope or having quick shell aliases to fall back to XWayland keeps these issues from blocking day-to-day work.
- Hyprland adds additional quirks when fractional scaling or decoration plugins are active. Disable fractional scaling on the workspace that hosts Unreal and keep `force_zero_scaling` handy in case pop-up menus render at the wrong DPI.

## Scriptable Launch Options

Both `run_editor.sh` and `run_game.sh` now understand a set of Wayland-focused flags:

| Flag | Effect |
| --- | --- |
| `--video-backend auto|wayland|x11` | Chooses the SDL backend. `--wayland`/`--x11` act as shortcuts. Defaults to `auto` (the engine decides). |
| `--gamescope` | Wraps the launch in `gamescope --expose-wayland --prefer-vk -- …`. Repeat `--gamescope-arg <value>` to add extra flags. |
| `--no-gamescope` | Explicitly disable the wrapper (helpful when `FP_USE_GAMESCOPE` is exported globally). |

The helpers also respect environment overrides:

- `FP_VIDEO_BACKEND` sets the default backend without touching CLI arguments.
- `FP_USE_GAMESCOPE` (`1/true/on`) pre-enables the gamescope wrapper; `FP_GAMESCOPE_ARGS` injects additional flags (for example `--fullscreen` or `--prefer-vk` tweaks).
- `FP_SDL_DYNAMIC_API` pins a specific `libSDL2` path when the autodetector cannot find a suitable stub.

See `Scripts/README.md` for a quick cheat sheet.

## Hyprland-Focused Tweaks

Keep these compositor settings ready when iterating on Wayland builds:

- **Scaling** – work in a 1.0 scale workspace when possible. If scaling is required, test `force_zero_scaling = true` inside the relevant Hyprland workspace group so XWayland surfaces (like editor pop-ups) do not blur or misalign.
- **Render scheduling** – Hyprland 0.50 introduced an experimental `render:new_render_scheduling` block that swaps to a triple-buffer frame pacing path. Toggle it on while testing the editor; disable it again if it introduces latency spikes.
- **Decorations** – we export `SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0` by default so SDL defers to server-side decorations. If your theme expects libdecor, override it to `1` temporarily.
- **Gamescope** – even though we aim for native Wayland, wrapping the editor in gamescope remains the quickest safety net when pointer capture regresses. Combine it with `--prefer-vk` to bias the compositor toward RADV.

Document any compositor config you commit to the repo so other machines replicate it cleanly.

## Validation Checklist

1. Launch the editor via `./Scripts/run_editor.sh --wayland --gamescope --gamescope-arg --fullscreen` and confirm window focus, input capture, and viewport interaction all survive map changes.
2. Run the standalone build with `./Scripts/run_game.sh --wayland --gamescope -- -- -log` and watch for frame pacing problems. Capture MangoHud stats in both cases.
3. Flip `FP_VIDEO_BACKEND=x11` and re-run the same steps to establish a baseline delta before rolling any Wayland-specific defaults into version control.
4. When Hyprland or Mesa updates land, note the version in this document along with any regressions so we can bisect driver interactions quickly.
