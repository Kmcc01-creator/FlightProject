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

### Categories
- **`Unit`**: Isolated logic tests (no world/render state required).
- **`Functional`**: Low-level engine integrations (UObjects, Subsystems).
- **`Integration`**: Multi-subsystem workflows (VEX -> Verse -> Task Graph).
- **`Gpu`**: Hardware-bound tests (RDG passes, io_uring, buffers).
- **`Perf`**: Benchmarking and scale stress tests.

## 2. Test Styles

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
