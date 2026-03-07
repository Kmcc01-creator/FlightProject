# Repository Guidelines

## Project Structure & Module Organization
FlightProject is an Unreal Engine 5 workspace rooted at `FlightProject.uproject`. Gameplay and editor modules live under `Source/FlightProject/{Public,Private}`; keep headers in `Public` for exports and implementation in `Private`. Config defaults live in `Config/Default*.ini`, runtime content plus CSV-driven data tables live under `Content/`, and generated outputs stay inside `Intermediate/` and `Saved/`.

## Build, Test, and Development Commands
- `./Scripts/generate_project_files.sh -f` regenerates IDE files through `GenerateProjectFiles.sh`.
- `./Scripts/build_targets.sh Development` compiles C++ targets; pass `Shipping` or `Test` as needed.
- `./Scripts/run_editor.sh -Log --wayland` starts UnrealEditor with helpers from `env_common.sh`.
- `./Scripts/run_game.sh --no-build -- -windowed` stages/cooks via `RunUAT BuildCookRun` and launches the staged Linux binary; omit `--no-build` for a clean rebuild.
- The generated `Makefile` exposes direct targets like `make FlightProjectEditor-Linux-Development` or `make FlightProject-Linux-Test` for CI parity.

## Coding Style & Naming Conventions
- Follow Unreal’s conventions: PascalCase classes (`AFlightVehiclePawn`), camelCase methods, and `b`-prefixed booleans.
- Use four spaces (no tabs) and group includes with project headers before engine headers.
- Expose types via headers in `Public/` and prefer `TWeakObjectPtr`, `TObjectPtr`, or `TArray` containers instead of raw pointers.
- Register logging categories with `DEFINE_LOG_CATEGORY_STATIC` and emit actionable `UE_LOG` messages.

## Testing Guidelines
- The project has two primary types of tests: in-world visualizers (`AFlightSpatialTestEntity`) and standalone C++ unit tests.
- To run all project automation tests:
  `./Scripts/run_editor.sh -ExecCmds="Automation RunTests FlightProject.*; Quit" -unattended -nop4`
- To run only the core functional C++ unit tests:
  `./Scripts/run_editor.sh -ExecCmds="Automation RunTests FlightProject.Functional; Quit" -unattended -nop4`
- For cooked validation, run `make FlightProject-Linux-Test` or append `--test` to `run_game.sh` to drive `RunUAT`’s Test configuration.
- Document manual verification and open issues in `Docs/Troubleshooting.md` so staged builds stay reproducible.

## Commit & Pull Request Guidelines
- Git history favors short, imperative summaries ("Add testing section to README", "testing flight"); keep the subject <72 characters and mention the subsystem up front.
- Reference tracking issues in the body, describe gameplay/editor impact, and list the commands you ran (`build_targets`, `run_game`, tests).
- PRs should include screenshots or logs for visual or networking changes, link to doc updates, and call out config or data assets touched.

## Environment & Tooling Tips
- Export `UE_ROOT` if the engine is outside `$HOME/Unreal/UnrealEngine`; scripts read it through `Scripts/env_common.sh`.
- Prefer `rg` for code search and keep temp assets out of source—only committed `Content/` assets should live in git.
- When editing ini files, lean on Unreal’s merge semantics (avoid wholesale copy/paste) and explain risky tweaks inside the matching doc under `Docs/`.

## Linux Wayland Workflow
- Follow `Docs/LinuxWayland.md` when launching on CachyOS/Hyprland. Install `sdl2`, `lib32-sdl2`, `gamescope`, and `mangohud` so the wrapper flags work.
- Use `./Scripts/run_editor.sh --wayland --gamescope --gamescope-arg --fullscreen` for a native Wayland launch; append `--x11` to compare against the fallback path.
- Export `FP_SDL_DYNAMIC_API=/usr/lib/libSDL2-2.0.so.0` (or the path from `pacman -Ql sdl2`) when SDL cannot resolve the dynamic loader.
- Tweak display behavior with `--gamescope-arg`/`--gamescope-args` (e.g., `--gamescope-args=--prefer-vk,--hdr-enabled`) and keep compositor changes under `~/.config/hypr/config/*.conf`.
