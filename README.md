# FlightProject: High-Scale Field Simulation & Stylized Rendering

FlightProject is a C++23 "Shadow Engine" built within Unreal Engine 5.7, focusing on massive-scale GPU simulation and avant-garde, field-theoretic rendering.

## 🚀 Core Architecture: SCSL
The engine is built on the **SCSL** paradigm, treating the world as a unified set of interacting data fields rather than discrete actors.

*   **Structures (S)**: Physical constraints via Signed Distance Fields (SDF).
*   **Clouds (C)**: Persistent volumetric density fields for trails and atmosphere.
*   **Swarms (S)**: Discrete Lagrangian agents (2M+ entities) performing GPU logic.
*   **Light (L)**: 3D Radiance Lattice with cellular energy propagation.

[Read the SCSL Engine Specification](./Docs/Architecture/SCSL_Engine.md)

## 🎨 Field-Theoretic Stylization
We reject standard PBR in favor of a **LERP-based NPR (Non-Photorealistic Rendering)** pipeline. By resolving simulation data into an **F-Buffer**, we enable anime-style shading, custom depth falloffs, and unified motion/lighting governed by **Gradient Descent**.

[Read the Stylization & F-Buffer Paradigm](./Docs/Architecture/FieldTheoreticStylization.md)

## 🛠️ Unifying hallmark: VEX DSL
Behaviors and visuals are authored via a bespoke, Houdini-inspired **VEX DSL**.
*   **Functional Piping**: Advanced stream transforms using the `|` operator.
*   **Compile-Time Optimization**: Iterative AST folding, constant propagation, and identity simplification.
*   **Safety**: A **Phantom Capability Algebra** ensures scripts never outrun their GPU resource dependencies.
*   **Reactive UI Scaffold**: VEX-authored Slate panels for live diagnostics and list-driven tooling.
*   **Verse/Solaris Bridge**: Experimental `IAssemblerPass` scaffolding for eventual source-to-`VProcedure` emission.

[Read the Verse Assembler Scaffold Plan](./Docs/Scripting/VerseAssemblerScaffold.md)
[Read the VEX UI Scaffold Plan](./Docs/Scripting/VexUiScaffold.md)

## 🛰️ Massive Multi-Agent Orchestration
Scaling beyond singletons, the engine supports distributed execution and dynamic events.
*   **Sparse Fields**: Multi-instance Lattices and Clouds managed via `Texture3DArray`.
*   **Behavior Classes**: Data-driven multi-dispatch using VEX-generated Mega-Kernels.
*   **Event Topology**: Sub-millisecond reaction to dynamic C++/Verse events via transient GPU buffers.

[Read the Instanced Orchestration Specification](./Docs/Architecture/InstancedVexOrchestration.md)
[Read the Concurrency & Task Orchestration Specification](./Docs/Architecture/VexConcurrencyModel.md)

## 🏗️ Engineering Standards
*   **C++23 Traits**: Zero-UHT compile-time reflection for CPU/GPU data parity.
*   **Monadic RDG**: 12-pass simulation orchestrated via type-safe functional chains.
*   **Linux/Vulkan**: Optimized for `io_uring` and high-performance asynchronous execution.
*   **Code Standards Baseline**: Initial findings, policy, and vertical-slice validation plan.

[Read the Code Standards Baseline](./CodeStandards.md)

## ✅ Testing Quickstart
Use the headless runner as the default validation path:

```bash
export UE_ROOT=/home/kelly/Unreal/UnrealEngine

# 1) Full headless baseline
./Scripts/run_tests_headless.sh

# 2) Parser bucket
TEST_FILTER="FlightProject.Schema.Vex.Parser" \
TEST_LOG_PROFILE=focused TEST_STREAM_FILTER=errors \
./Scripts/run_tests_headless.sh

# 3) Extended mixed bucket (schema + Verse + parser + vertical slice)
TEST_FILTER="FlightProject.Integration.SchemaDriven+FlightProject.Schema.Vex.ManifestValidation+FlightProject.Verse.CompileContract+FlightProject.Verse.Subsystem+FlightProject.Schema.Vex.Parser+FlightProject.Integration.Vex.VerticalSlice" \
TEST_LOG_PROFILE=focused TEST_STREAM_FILTER=errors \
./Scripts/run_tests_headless.sh

# 4) GPU smoke/offscreen path
TEST_SCOPE=gpu_smoke ./Scripts/run_tests_full.sh
```

Current test snapshot and filters:
- [Current Build & Test Baseline](./Docs/Workflow/CurrentBuild.md)
- [VEX Schema/Parser Validation](./Docs/Scripting/VexSchemaValidation.md)

---
*Built with passion for data-oriented design and artistic opinion.*
