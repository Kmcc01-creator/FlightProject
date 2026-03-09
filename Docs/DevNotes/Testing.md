# FlightProject Testing Standards

This document defines the architecture and conventions for automation testing within the FlightProject workspace.

## 0. Core Principle: Future-Proofing & Discovery
Testing in FlightProject is not just about preventing regressions; it is a **discovery and future-proofing mechanism**.
- **Schema-Driven Validation (SDV)**: Prefer tests that auto-generate their own test cases based on the C++ Reflection Schema. If a developer adds a new field or symbol, the test suite should automatically expand to cover it without manual test authoring.
- **Architectural Invariant Enforcement**: Tests should verify structural promises (e.g., "All symbols mapped to GPU must lower to valid HLSL", "All Verse thunks must return a VPlaceholder").
- **Discovery Testing**: When integrating deep engine features (like VerseVM or AutoRTFM), write tests that define our assumptions about the engine. If an Unreal Engine update changes how these systems work, the discovery tests will flag the shifted boundary.

## 1. Naming Taxonomy

Tests must follow a hierarchical naming convention to allow for precise filtering in the Automation Front-end:

`FlightProject.<Category>.<Subsystem>.<Feature>`

## 2. Hardware Requirements & Bucketing

To streamline CI and local development, tests are bucketed by their hardware requirements.

| Category | Requirement | Headless (`-NullRHI`) | Notes |
| :--- | :--- | :--- | :--- |
| **`Unit`** | CPU Only | **Supported** | No `UWorld` or RHI access. |
| **`Functional`** | CPU + `UWorld` | **Supported** | May use `IsRunningCommandlet()` guards for rendering components. |
| **`Integration`** | CPU + Subsystems | **Supported** | Multi-subsystem logic. |
| **`Gpu`** | RHI SM6+ | **Skipped** | Direct RDG/Vulkan/io_uring hardware verification. |
| **`Perf`** | Stable HW | **Discouraged** | Results in `-NullRHI` are non-representative. |

### Graceful Skipping
Tests requiring GPU/RHI should use the `Flight::Test::ShouldSkipGpuTest()` helper to skip execution in headless environments without failing the test suite.

## 3. Test Styles

### Spec-Based Testing (Preferred)
Use `BEGIN_DEFINE_SPEC` for all behavioral and semantic testing. It provides better readability through nesting and shared setup logic.

**Example**: `FlightVexParserSpec.cpp`

### Simple Automation Tests
Use `IMPLEMENT_SIMPLE_AUTOMATION_TEST` only for very basic, one-off logic checks that don't benefit from grouping.

### Complex (Data-Driven) Tests
Use `IMPLEMENT_COMPLEX_AUTOMATION_TEST` for running the same validation logic over multiple assets (e.g., a directory of `.vex` files).

## 3. Advanced Testing Techniques

### Latent Commands (Async Verification)
For `io_uring` and `@async` behaviors, use Latent Commands to wait for frame signals or callbacks without stalling the test runner.

```cpp
ADD_LATENT_AUTOMATION_COMMAND(FWaitForGpuSignal(StatusValue));
```

### Mocking Symbols
Use `FlightTestUtils.h` to generate mock symbol tables for VEX validation tests to avoid dependency on the full schema manifest in every unit test.

## 4. Benchmarking
Performance tests should be tagged with `EAutomationTestFlags::PerfFilter` and should emit results to the JSON summary via `FAutomationTestBase::AddAnalyticsItem`.

## 5. Current Baseline (March 9, 2026)

### Verified Headless (Unit/Integration)
- `FlightProject.Unit.Vex.Parser.*` (Full AST validation)
- `FlightProject.Unit.Reflection.*` (Schema traits)
- `FlightProject.Unit.Reactive.Core.*` (Primitive streams)
- `FlightProject.Integration.Vex.SimdParity` (CPU SIMD path)
- `FlightProject.Integration.Concurrency` (Task Graph / Async)

### Verified Full RHI (Gpu/Perf)
- `FlightProject.Gpu.Spatial.Perception` (RDG / io_uring)
- `FlightProject.Gpu.Swarm.Pipeline.*` (Full RDG simulation)
- `FlightProject.Perf.GpuPerception` (Scalability benchmark)

### Known Limitations
- GPU/Vulkan automation still depends on local driver/device availability; treat Vulkan initialization failures (e.g. `VK_ERROR_INITIALIZATION_FAILED`) as environment issues.
- `-NullRHI` mode currently crashes during engine analytics initialization; use the `ShouldSkipGpuTest()` helper to bypass hardware-bound logic in headless CI.
