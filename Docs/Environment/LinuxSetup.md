# Linux Wayland & Hyprland Playbook

_Last updated: October 10, 2025_

This note tracks the experimental knobs we use to keep Unreal Engine responsive on CachyOS/Hyprland while prioritising a native Wayland stack. Treat the suggestions as opt-in toggles; the editor still ships with X11 defaults, so stage any risky changes behind the new script flags before promoting them to project-wide defaults.

## Current Constraints

- Unreal’s Linux build links against SDL2 with X11 defaults. Forcing Wayland (`SDL_VIDEODRIVER=wayland`) works when SDL can dynamically resolve the system’s `libSDL2.so`, but some Arch-based layouts expose only `libSDL2-2.0.so.0`. If SDL cannot find the stub, it falls back to X11 or crashes when opening menus. Use `FP_SDL_DYNAMIC_API` (or `--video-backend wayland`) so the launchers point SDL at a concrete library path before we start the editor.
- Native Wayland paths still ship with rough edges: pointer capture can fail, dropdowns may stop responding, and some UI hover handlers trigger crashes. Staying inside gamescope or having quick shell aliases to fall back to XWayland keeps these issues from blocking day-to-day work.
- Hyprland adds additional quirks when fractional scaling or decoration plugins are active. Disable fractional scaling on the workspace that hosts Unreal and keep `force_zero_scaling` handy in case pop-up menus render at the wrong DPI.

## Prerequisites on CachyOS / Arch

- Packages: `sudo pacman -S sdl2 gamescope mangohud lib32-sdl2` keeps SDL stubs, the gamescope wrapper, and telemetry tools in sync with CachyOS updates. `gamescope` is mandatory whenever you pass `--gamescope` (our scripts exit early otherwise).
- SDL discovery: check which library Arch installed via `pacman -Ql sdl2 | rg -i libSDL2`. Export `FP_SDL_DYNAMIC_API=/usr/lib/libSDL2-2.0.so.0` (or the path pacman prints) before launching if the helper cannot auto-detect that directory.
- Hyprland config lives in `~/.config/hypr/` on this machine; keep tweaks isolated inside the `config/*.conf` fragments so distro updates do not overwrite them.

## Scriptable Launch Options

Both `run_editor.sh` and `run_game.sh` now understand a set of Wayland-focused flags:

| Flag | Effect |
| --- | --- |
| `--video-backend auto|wayland|x11` | Chooses the SDL backend. Defaults to `auto`. |
| `--wayland` / `--x11` | Shorthand for forcing a backend without typing the entire flag. |
| `--gamescope` | Wraps the launch in `gamescope --expose-wayland --prefer-vk -- …`. |
| `--no-gamescope` | Explicitly disable the wrapper (helpful when `FP_USE_GAMESCOPE` is exported globally). |
| `--gamescope-arg <value>` | Appends a single gamescope flag (repeatable). |
| `--gamescope-args=a,b,c` | Appends comma-separated gamescope flags (e.g., `--fullscreen`). |

The helpers also respect environment overrides:

- `FP_VIDEO_BACKEND` sets the default backend without touching CLI arguments.
- `FP_USE_GAMESCOPE` (`1/true/on`) pre-enables the gamescope wrapper; `FP_GAMESCOPE_ARGS` injects additional flags (for example `--fullscreen` or `--prefer-vk` tweaks).
- `FP_SDL_DYNAMIC_API` pins a specific `libSDL2` path when the autodetector cannot find a suitable stub. Example:
  ```bash
  export FP_SDL_DYNAMIC_API="$(pacman -Ql sdl2 | rg -o '/.*libSDL2-2.0.so.0' | head -n1)"
  ./Scripts/run_editor.sh --wayland -Log
  ```

See `Scripts/README.md` for a quick cheat sheet.

## Hyprland-Focused Tweaks

Keep these compositor settings ready when iterating on Wayland builds:

- **Scaling** – work in a 1.0 scale workspace when possible. If scaling is required, test `force_zero_scaling = true` inside the relevant Hyprland workspace group so XWayland surfaces (like editor pop-ups) do not blur or misalign.
- **Render scheduling** – Hyprland 0.50 introduced an experimental `render:new_render_scheduling` block that swaps to a triple-buffer frame pacing path. Toggle it on while testing the editor; disable it again if it introduces latency spikes.
- **Decorations** – we export `SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0` by default so SDL defers to server-side decorations. If your theme expects libdecor, override it to `1` temporarily.
- **Gamescope** – even though we aim for native Wayland, wrapping the editor in gamescope remains the quickest safety net when pointer capture regresses. Combine it with `--prefer-vk` to bias the compositor toward RADV.

Document any compositor config you commit to the repo so other machines replicate it cleanly.

## Hyprland Config Snapshot (validated October 2025)

The CachyOS image loads Hyprland from `~/.config/hypr/hyprland.conf`, which in turn sources modular fragments:

- `config/monitor.conf` keeps every output at scale `1` via `monitor = , preferred, auto, 1`. Enable the commented `xwayland { force_zero_scaling = true }` block whenever Unreal pop-ups blur inside XWayland.
- `config/decorations.conf` disables libdecor via `shadow { enabled = false }` and server-side decorations. This matches our default `SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0` export, so no extra tweaks are required.
- `config/windowrules.conf` routes long-running tools (Emacs, browsers, Alacritty) to their own workspaces as documented in `Documents/Hyprland-Rendering-Workspaces-Animations.md`; this guarantees the editor always spawns on the primary workspace without inherited scaling oddities.
- `config/environment.conf` only touches cursor variables, so editor launches inherit whatever `env_common.sh` sets for SDL, QT, and GDK.

Validate changes with `hyprctl reload` and capture any new directives in `Documents/Hyprland-Rendering-Workspaces-Animations.md` so other contributors can mirror the compositor setup.

## Validation Checklist

1. Launch the editor via `./Scripts/run_editor.sh --wayland --gamescope --gamescope-arg --fullscreen` and confirm window focus, input capture, and viewport interaction all survive map changes.
2. Run the standalone build with `./Scripts/run_game.sh --wayland --gamescope -- -- -log` and watch for frame pacing problems. Capture MangoHud stats (`mangohud ./Scripts/run_editor.sh …`) in both cases.
3. Flip `FP_VIDEO_BACKEND=x11` and re-run the same steps to establish a baseline delta before rolling any Wayland-specific defaults into version control.
4. Toggle Hyprland-side settings (e.g., comment/uncomment `force_zero_scaling` or gamescope usage) in `~/.config/hypr/config` and `hyprctl reload` after each edit to confirm they behave as described.
5. When Hyprland or Mesa updates land, note the version in this document along with any regressions so we can bisect driver interactions quickly.
