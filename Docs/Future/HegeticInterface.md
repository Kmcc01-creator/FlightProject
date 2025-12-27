# Hegetic Data Interface Design

This document proposes a reusable “hegetic” interface framework—an extensible data-to-visual system that exploits Unreal Engine’s material graph, Niagara, and UI stack to render rich, dynamic feedback for arbitrary data sources.

## Vision & Use Cases
- **Mission telemetry dashboards** – Render drone swarm positions, route statuses, or congestion metrics as holographic overlays in cockpit or command-room scenes.
- **Spatial analytics** – Project heatmaps, flow lines, and volumetric cues directly into the world to depict density, hazards, or sensor confidence.
- **Diagnostic tooling** – Drive developer-focused overlays showing subsystem health, data throughput, or CSV ingestion status.
- **Interactive control surfaces** – Allow operators to scrub timelines, adjust parameters, or trigger scripts via tactile widgets that feed back into gameplay systems.

## Architectural Layers
1. **Data Adapters** – `UHegeticDataSource` subclasses pull or receive data (CSV, network telemetry, gameplay events). They normalize payloads into a common schema (`FHegeticDatum`, `FHegeticSeries`) with typed values, timestamps, and metadata tags.
2. **Data Fabric** – A `UHegeticDataHubSubsystem` (world or game-instance subsystem) buffers normalized series, handles subscriptions, and exposes query APIs. It allocates ring buffers, performs downsampling, and broadcasts change events to visual nodes.
3. **Presentation Nodes** – Actor/components that transform data into visuals:
   - **Material Nodes** (`UHegeticMaterialPresenter`) map scalar/vector data onto dynamic material instances, parameter collections, or render targets.
   - **Niagara Nodes** (`UHegeticNiagaraPresenter`) spawn and update particle systems, injecting per-particle attributes (e.g., velocity, intensity) sourced from data hubs.
   - **Widget Nodes** (`UHegeticWidgetPresenter`) feed Slate/UMG widgets for textual or chart-style representations.
4. **Interaction Layer** – `UHegeticInteractionComponent` captures user input (VR gestures, mouse picks, controller focus) and routes commands back to the data hub or mission scripting. Supports GOAP integration so interactions can trigger high-level goals.
5. **Orchestration** – `AHegeticInterfaceAnchor` actors bundle presenters, interactions, and layout transforms, acting as reusable prefabs. Designers compose anchors in the level or via Blueprints, assign data bindings, and preview in-editor.

## Data Flow
```
External Systems (CSV, HTTP, Gameplay events)
          │
          ▼
  UHegeticDataSource (adapter thread or async task)
          │ normalized FHegeticSeries
          ▼
  UHegeticDataHubSubsystem (buffering, filtering)
          │ multicast events / queries
          ├────────────┬────────────┐
          ▼            ▼            ▼
Material Presenter  Niagara Presenter  Widget Presenter
 (Dynamic MI)          (FX)               (UMG)
```

- Data sources can push updates on background threads; the hub marshals to game thread safe queues before notifying presenters.
- Presenters use non-blocking pulls or subscribe to change delegates to keep frame time predictable.

## Visualization Techniques
- **Material Parameter Collections** – Broadcast global scalars/vectors for quick shader modulation (e.g., change hue based on risk).
- **Render Targets** – Write data grids to floating textures that materials sample for heatmaps or line charts.
- **Niagara Data Interfaces** – Feed arrays of points/velocities into GPU emitters. Each particle can represent a drone, a data sample, or a vector field arrow.
- **Spline/Geometry Generation** – For time-series, spawn procedural meshes or splines (e.g., trailing ribbons showing velocity history).
- **Audio-Responsive Coupling** – Optional audio cues via modulation buses to complement visuals (ties into Unreal’s audio engine for “hegetic” multisensory feedback).

## Interaction Patterns
- **Probe & Inspect** – Hovering over a Niagara particle projects a widget with details (using widget components or 3D widgets).
- **Timeline Scrubbing** – Material-based sliders update the data hub’s playback cursor, repopulating presenters with historical frames.
- **Goal Injection** – Interaction component triggers GOAP actions (e.g., “dispatch drone to hotspot”) and the nav subsystem recalculates routes; the interface reflects new directives.
- **Alert States** – Data hub raises alerts (threshold breaches). Presenters transition materials to pulsing/bright states using Niagara bursts or emissive ramps.

## Tooling & Workflow
1. **Blueprint Wrappers** – Provide `BP_HegeticDataSource` and `BP_HegeticPresenter` templates so designers configure bindings without C++ changes.
2. **Editor Preview Mode** – Custom editor utility widget that simulates data streams, letting artists iterate on materials/Niagara systems in context.
3. **Schema Definitions** – Store adapter configuration in `Config/HegeticSources.ini` and expose developer settings (`UHegeticDeveloperSettings`) to map CSV/HTTP endpoints.
4. **Profiling Hooks** – `STAT_Hegetic` group tracks update latency, buffer sizes, and presenter tick costs. Logging macros record anomalies (e.g., dropped frames, queue overflow).

## Implementation Roadmap
1. **Foundation**
   - Define `FHegeticDatum`, `FHegeticSeries`.
   - Implement hub subsystem with thread-safe queues and delegates.
   - Create base data source class with async polling utilities.
2. **Visual MVP**
   - Material presenter driving a dynamic MI on a simple mesh.
   - Niagara presenter spawning particles from hub-provided positions/intensity.
   - Editor utility for injecting mock data.
   - Power the prototype with `UFlightNavGraphDataHubSubsystem` snapshots so nav graph nodes/edges can be visualized immediately.
3. **Interaction Layer**
   - Input component capturing ray hits, relaying commands.
   - Blueprint-accessible events for GOAP/mission scripts.
4. **Advanced Visuals**
   - Render target heatmaps, spline trails, layered Niagara effects.
   - Audio modulation for multi-sensory feedback.
5. **Polish & Tooling**
   - In-editor preview panel, profile visualization, preset library of materials/emitters.

## Open Topics
- **Data Volume Management** – Decide retention policies for high-frequency telemetry (downsampling vs. rolling averages).
- **Multiplayer Replication** – Determine which hegetic channels replicate to clients or remain server-only diagnostics.
- **Accessibility** – Explore colorblind-friendly palettes and haptics/audio cues for inclusive design.
- **Shader Optimization** – Identify hotspots for GPU cost and consider wave-intrinsic optimizations (e.g., subgroup reductions) once content profiles are available.

This framework keeps data ingestion, visualization, and interaction decoupled, letting teams reuse the system for both production HUDs and debugging utilities while showcasing Unreal’s rendering and FX strengths.
