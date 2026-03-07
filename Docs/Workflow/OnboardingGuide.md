# Onboarding Guide for FlightProject

## 1. Welcome to FlightProject

Welcome! This guide is the starting point for all new developers joining the FlightProject.

**Our Mission:** To build a high-performance, large-scale autonomous flight simulation on Linux. We prioritize correctness, performance, and scalability.

**Our Philosophy:**
- **Safety & Correctness First:** We use modern C++ patterns inspired by Rust to catch errors at compile-time and make our code robust. Understanding `TResult` and `TPhantomState` is key.
- **Data-Oriented Design:** The core simulation is built on Unreal's **Mass Entity Component System (ECS)**. This allows us to simulate thousands of agents efficiently. We prefer data transformations in processors over traditional Actor-based logic.
- **Data-Driven by Default:** Gameplay and environment configurations are driven by simple CSV files. This allows for rapid iteration without recompiling.
- **Expert-Level Linux Platform:** We embrace Linux as a first-class platform, using advanced features like `io_uring` for high-performance GPU communication.

This guide will walk you through setting up your environment, understanding the architecture, and making your first contribution.

---

## 2. Step 1: Environment Setup

Your first step is to get the project building and running. We have detailed documentation for this, but here is the quick-start path:

1.  **Platform & Dependencies:** Ensure your environment matches our baseline. See `Docs/Environment/LinuxSetup.md` for required packages and OS configuration.
2.  **Set Environment Variable:** The build scripts depend on the `UE_ROOT` variable. Set it in your shell's configuration file (e.g., `~/.bashrc` or `~/.zshrc`):
    ```bash
    export UE_ROOT=/home/kelly/Unreal/UnrealEngine
    ```
3.  **Fetch Engine Dependencies:** The first time you check out the engine, you must run the `Setup.sh` script from the engine root:
    ```bash
    cd $UE_ROOT
    ./Setup.sh
    ```
4.  **Generate Project Files:** From the project root (`/home/kelly/Unreal/Projects/FlightProject/`), run the generation script:
    ```bash
    ./Scripts/generate_project_files.sh
    ```

For a deeper dive into the build system and configuration, see `Docs/Environment/BuildAndRegen.md`.

---

## 3. Step 2: An Architectural Tour

The project is built on four pillars. Understanding them is crucial.

| Pillar | Description | Key Document |
|---|---|---|
| **1. The Core Simulation** | We use Unreal's **Mass ECS** framework for the core flight simulation. Drones are lightweight `entities`, and their logic is handled in bulk by `Processors`. | `Docs/Architecture/MassECS.md` |
| **2. The Coding Paradigm** | We use a custom **Functional C++** library for safety and clarity. `TResult` for errors and `TPhantomState` for compile-time safety are central. | `Docs/Architecture/FunctionalPatterns.md` |
| **3. The Data Layer** | The world is configured at runtime via **CSV files**. This includes everything from obstacle placement to drone behavior. | `Docs/Architecture/DataPipeline.md` |
| **4. The Platform** | We use a high-performance **`io_uring` GPU Bridge** for efficient, Linux-native rendering and compute. | `Docs/Architecture/IoUringGpuIntegration.md` |

We strongly recommend reading the documentation for these four pillars before writing any code.

---

## 4. Step 3: Your First Build & Run

With the environment set up, you can build and run the editor.

1.  **Build the Project:**
    ```bash
    # From Projects/FlightProject/
    ./Scripts/build_targets.sh Development
    ```

2.  **Run the Editor:**
    ```bash
    # We recommend running via Wayland for the best experience
    ./Scripts/run_editor.sh --wayland
    ```
This will launch the Unreal Editor. The main project map should be located in `Content/Maps/`.

---

## 5. Step 4: A Contributor's Workflow

Here is how to approach common tasks in FlightProject. This is not exhaustive but should give you a feel for the workflow.

#### How do I... add a new behavior to a drone?
- **Answer:** You will most likely be working with the Mass ECS.
- 1. **Identify Data:** Does the behavior require new data? If so, define a new `FFragment` in `Source/FlightProject/Public/Mass/FlightMassFragments.h`.
- 2. **Implement Logic:** Create a new `UMassProcessor` that queries for entities with the required fragments. Implement your logic in the processor's `Execute` function.
- 3. **Configure:** Add your new processor to the correct processing phase in `Config/DefaultMass.ini`.
- **See Also:** `Docs/Architecture/MassECS.md`

#### How do I... add a new type of obstacle?
- **Answer:** This is a data-driven task.
- 1. **Update Data:** Add a new row to `Content/Data/FlightSpatialLayout.csv` with the appropriate type and coordinates.
- 2. **Update Logic (if needed):** If this new obstacle type requires unique logic, you may need to modify the `AFlightSpatialLayoutDirector` which is responsible for parsing this CSV and spawning actors.
- **See Also:** `Docs/Architecture/DataPipeline.md`

#### How do I... fix a bug in a fallible operation?
- **Answer:** This likely involves our functional error-handling patterns.
- 1. **Trace the Error:** Find the function that is failing. It should be returning a `TResult<T, E>`.
- 2. **Check Callers:** Look at how the function is being called. Is the caller ignoring the error? Is it using `FLIGHT_TRY` to propagate the error upwards?
- 3. **Debug:** A common pattern is to `.Then()` a logging function onto a `TValidateChain` to inspect intermediate values without interrupting the flow.
- **See Also:** `Docs/Architecture/FunctionalPatterns.md`

---

## 6. Step 5: Testing and Debugging

- **Running Tests:** The `AGENTS.md` file contains a quick reference for running our automation tests. All new logic should be accompanied by a test.
- **Mass Debugger:** Use the console command `mass.debug` or press `Shift+F2` to toggle the Mass Entity debugger. This is invaluable for inspecting the state of fragments on any entity.
- **Unreal Insights:** This is our primary tool for performance profiling. Learn how to launch a session and profile the CPU and GPU to analyze Mass Processor performance and rendering costs.
- **Troubleshooting:** For common environment and build issues, please consult `Docs/Environment/Troubleshooting.md`.

---

## 7. Where to Find Help

This project is complex. If you are stuck after reading the relevant documentation, please reach out.
*(This section can be updated with key contacts, Slack channels, or meeting times.)*
