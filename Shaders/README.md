# FlightProject Shader Directory

Custom Unreal shaders live here. The runtime module registers `/Shaders` with `FShaderDirectories`, so any files added to this folder become visible to the shader compiler in both editor and cooked builds.

## Usage Notes
- Place `.usf`/`.ush` source under this directory or its subfolders.
- Reference your shaders in code with the `/Shaders/...` virtual path (matching the `ComputeShaderDirectory` developer setting).
- Remember to rebuild shader libraries (`Scripts/run_game.sh` without `--no-cook`) after adding or changing shader files so staged builds pick up the updates.
