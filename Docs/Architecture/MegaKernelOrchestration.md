# Mega-Kernel Orchestration: GPU System Coordination

This document defines the Mega-Kernel as a first-class coordinating abstraction in FlightProject.
It moves the GPU execution layer from a static shader implementation to a dynamic, orchestration-driven coordination surface.

For the foundational VEX state mutation concepts, see `Docs/Architecture/VexStateMutationSchemaFrame.md`.
For the world-level coordination surface, see `Docs/Architecture/OrchestrationSubsystem.md`.
For the current SchemaIR direction, see `Docs/Workflow/SchemaIrImplementationExploration.md`.

## 1. The Mega-Kernel Concept

The Mega-Kernel is the "Execution Plan" for the GPU domain.
It is a unified HLSL compute shader that encapsulates multiple VEX behaviors and dispatches them against entity cohorts using a shared memory layout.

Rather than being a "fixed shader that runs scripts," the Mega-Kernel is a **Synthesized Coordination Layer** that reconciles high-level VEX abstractions with the actual GPU architecture (RHI, RDG, and Structured Buffers).

## 2. Role in Orchestration

The Mega-Kernel is the primary mechanism for **Target-Aware Orchestration** on the GPU.

### 2.1 Coordination Responsibilities
- **Cohort Dispatch**: Reconciling `BehaviorID` (from Radix Sort) with specific VEX behavior cases.
- **Attribute Hoisting**: Consolidating global memory loads into local variables or shared memory to minimize bandwidth.
- **Contract Enforcement**: Ensuring that only behaviors bound to the `Gpu` domain are included and that they satisfy their schema contracts.
- **Storage Reconciliation**: Mapping logical VEX symbols (`@position`) to physical storage (`GpuBufferElement`, `SoaColumn`).

## 3. The Mega-Kernel Pipeline

The generation of the Mega-Kernel follows the standard FlightProject compiler flow, integrated with the orchestration subsystem.

### 3.1 Input: The Execution Plan
The `UFlightOrchestrationSubsystem` provides the `FFlightExecutionPlan`, which identifies:
- Active behaviors for the `EFlightExecutionDomain::Gpu`.
- Entity cohorts bound to those behaviors.
- Required symbol contracts for each behavior.

### 3.2 Synthesis: Unified Source Generation
The Mega-Kernel generator (e.g., `LowerMegaKernel`) performs the following:
1.  **Union of Used Symbols**: Identifies every symbol used across all GPU-active behaviors.
2.  **Global Hoisting**: Generates HLSL to load these symbols from global buffers into local aliases exactly once per thread.
3.  **Behavior Dispatch**: Generates a `switch(BehaviorID)` block where each case contains the IR-lowered HLSL for a specific behavior.
4.  **Local Store-Back**: Generates HLSL to write modified local aliases back to global buffers.

### 3.3 Realization: Shader Injection
The resulting HLSL is injected into the GPU pipeline:
- **Virtual Shader Device**: Using `IVirtualShaderSourceDevice` to provide the generated source to the Unreal shader compiler at runtime.
- **Shader Permutations**: Managing shader recompilation when the execution plan or schema layout changes significantly.

## 4. Reconciling Abstractions to Architecture

The Mega-Kernel is where the "Abstract Script" meets the "Concrete Hardware."

### 4.1 Schema-Driven Symbol Mapping
Instead of hardcoded strings, the Mega-Kernel uses `FVexTypeSchema` to resolve:
- **Logical ID** -> **HLSL Identifier** (e.g., `@position` -> `DroidStates[EntityIndex].Position`).
- **Residency** -> **Visibility** (Ensuring `@cpu` only symbols are not attempted in the GPU kernel).
- **Storage Kind** -> **Access Pattern** (Structured buffer vs. SoA column).

### 4.2 Optimal Hoisting Strategy
The Mega-Kernel can optimize memory access patterns based on the aggregate needs of all active behaviors:
- **Shared Memory Hoisting**: Frequently accessed shared symbols can be hoisted into `groupshared` memory for inter-thread cooperation.
- **Sparse Writeback**: Only symbols that were actually modified by a behavior need to be written back to global memory.

## 5. First-Class Coordinating Abstraction

By making the Mega-Kernel a first-class concept, we enable:

- **Actual System Coordination**: The kernel is not just "running code"; it is reconciling the needs of multiple independent behaviors into a single, efficient hardware dispatch.
- **Zero-Cost Abstractions**: The VEX-to-HLSL lowering ensures that high-level DSL constructs result in optimal, hand-tuned HLSL inside the unified kernel.
- **Visible Execution Policy**: The orchestration reports can explain *why* the Mega-Kernel looks the way it does, which behaviors were included, and what optimization strategies were applied.

## 6. Implementation Strategy

### Phase 1: Subsystem Integration
Introduce `UFlightMegaKernelSubsystem` to observe the `UFlightOrchestrationSubsystem` and trigger kernel synthesis when the execution plan is updated.

### Phase 2: Schema-Automated Mapping
Refactor `LowerMegaKernel` to consume `FVexSchemaBindingResult` and `FVexTypeSchema` directly, removing the need for manual `SymbolMap` construction.

### Phase 3: Dynamic Shader Injection
Implement the virtual shader source bridge to allow the generated Mega-Kernel to be consumed by `FFlightSwarmForceCS` and other GPU passes without manual `.usf` edits.

## 7. Success Criteria

1.  **Orchestration Driven**: Changing a behavior binding in the `UFlightOrchestrationSubsystem` results in a new, optimized Mega-Kernel being synthesized.
2.  **Automated Reconciliation**: No manual HLSL identifier mapping is required to add new reflected symbols to the GPU swarm.
3.  **Optimal Performance**: Generated HLSL parity with hand-written kernels, including efficient hoisting and dispatch.
4.  **Truthful Reporting**: The orchestration report includes the Mega-Kernel source, optimization metrics, and contract validation results.
