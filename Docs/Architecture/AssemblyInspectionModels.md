# Assembly Inspection Models

This document defines the analysis models FlightProject should use when inspecting compiler artifacts, bytecode, shader outputs, and native assembly.

The goal is not to fetishize machine code.
The goal is to extract stable signals that help us reason about optimization quality, backend routing, and execution cost.

For the artifact-emission and test-layer design, see `Docs/Architecture/CompilerArtifactTesting.md`.
For target-aware policy inputs, see `Docs/Architecture/TargetCapabilitySchema.md`.

## 1. Problem Statement

Assembly inspection is useful because it exposes optimization shape directly.
However, raw assembly text is often too unstable to use as the primary design surface.

What tends to matter more than the exact final instruction spelling is:

- register pressure
- spill risk
- temporary lifetime length
- call density
- memory traffic
- cache-locality shape
- warmup cost
- structural compression of the code path

These are the signals FlightProject should elevate into explicit models.

## 2. Core Principle

FlightProject should treat assembly and codegen inspection as a search for stable performance signals.

Recommended rule:

- inspect exact text when needed
- reason primarily in terms of normalized code-shape metrics

That gives the project a durable way to compare:

- unoptimized vs optimized lowering
- fallback vs specialized paths
- low-pressure vs high-pressure kernels
- cold-start vs warm execution behavior

## 3. Primary Analysis Models

Recommended first-class models:

1. register pressure and spill model
2. call density and control-shape model
3. cache-locality and working-set model
4. warmup and cold-start model
5. code compression model

These models should be reported alongside artifacts and used in structural tests.

## 4. Register Pressure And Spill Model

### Why It Matters

Unoptimized code often expands the live working set:

- too many temporaries
- too many extended live ranges
- too many call boundaries
- too much stack traffic

Optimized code typically compresses the live set:

- fewer active values at once
- shorter lifetimes
- fewer spills
- more direct dataflow

### What To Measure

Recommended metrics:

- peak live temporary count
- average live temporary count
- value-width mix (`scalar`, `vector`, `aggregate`, `handle`)
- call-site count
- barrier or sync-site count
- estimated spill-risk class
- observed spill indicators when available

Recommended spill-risk classes:

- `Low`
- `Medium`
- `High`
- `Critical`

### Stable Interpretation

FlightProject should not claim an exact physical spill count unless the backend exposes it reliably.

Instead, use:

- structural pressure estimates
- backend telemetry when available
- measured calibration in controlled runs

## 5. Call Density And Control Shape Model

Function calls are not automatically bad.
But high call density often correlates with:

- inflated register pressure across boundaries
- reduced inlining opportunities
- more stack traffic
- worse I-cache locality in tiny kernels

Recommended metrics:

- total call count
- indirect call count
- helper thunk count
- block count
- branch count
- loop backedge count
- suspend-capable operation count

Useful comparisons:

- optimized path eliminates helper calls
- fused path reduces thunk count
- direct bytecode lowering removes fallback entry thunks

## 6. Cache-Locality And Working-Set Model

FlightProject should not pretend it can predict exact cache misses from assembly alone.
It should model cache behavior as a locality and working-set heuristic.

### What To Measure

Recommended metrics:

- load/store count
- memory-access stride class
- gather/scatter presence
- repeated symbol lookup count
- estimated hot working-set size class
- number of distinct input streams touched
- shared-vs-local data access pattern

Recommended locality classes:

- `Streaming`
- `Strided`
- `GatherScatter`
- `HotScalar`
- `Mixed`

### Why This Fits FlightProject

This is especially relevant for:

- Mass fragment execution
- direct SIMD paths
- shared-symbol CPU/GPU handshakes
- register-backed symbol access vs fallback accessors

The important question is not "what is the exact L1 miss rate?"
The important question is "did this lowering become more locality-friendly or less locality-friendly?"

## 7. Warmup And Cold-Start Model

FlightProject should treat warmup as a first-class execution concern, not an afterthought.

Warmup includes more than CPU cache fill.

Examples:

- first compile or first assemble cost
- first VM procedure materialization
- first shader compilation or pipeline creation
- first symbol-registry or thunk-registry resolution
- first world/service lookup cost
- first-run branch predictor and instruction-cache cold state

### Recommended Warmup Metrics

- compile time
- assemble time
- first invocation time
- warmed steady-state invocation time
- first backend resource initialization cost
- first cache fill count
- first-run fallback path count

### Practical Interpretation

A path can be excellent when warm and still be a poor fit if:

- its cold-start cost is too high for gameplay use
- it forces too many one-time pipeline or registry touches
- it front-loads too much compilation work for small workloads

FlightProject should make that trade explicit.

## 8. Code Compression Model

Compression is the most general concept in this space.

Here, "compression" does not mean byte-level compression.
It means reducing the structural footprint of execution.

Examples of useful compression:

- fewer live values
- fewer instructions
- fewer calls
- fewer barriers
- fewer memory round-trips
- fewer state transitions
- narrower working set
- fewer backend representations for the same semantic work

### Compression Dimensions

Recommended dimensions:

- instruction compression
- live-range compression
- call-graph compression
- memory-traffic compression
- state-transition compression
- artifact compression

Examples in FlightProject terms:

- direct VVM lowering instead of generated-source + fallback chain
- fused `EnterVM` batches instead of repeated transitions
- direct fragment SIMD instead of gather/scatter staging
- one shared codegen report instead of scattered diagnostics

### Why This Is Useful

Compression provides a unifying lens across:

- compiler internals
- runtime overhead
- cache locality
- orchestration cost
- backend routing

It also matches the intuitive difference you described between unoptimized and optimized assembly.

## 9. Suggested Report Types

To make these ideas testable, FlightProject should define small normalized summaries.

### 9.1 Code Shape Metrics

```text
FFlightCodeShapeMetrics
```

Recommended fields:

```text
InstructionCount
BlockCount
BranchCount
CallCount
IndirectCallCount
PeakLiveTemps
AverageLiveTemps
PressureClass
EstimatedWorkingSetClass
LoadCount
StoreCount
GatherScatterCount
SuspendCapableOpCount
```

### 9.2 Warmup Metrics

```text
FFlightWarmupMetrics
```

Recommended fields:

```text
CompileTimeUs
AssembleTimeUs
FirstInvokeTimeUs
WarmInvokeTimeUs
FirstResourceInitTimeUs
RegistryMissCount
CacheFillCount
ColdStartClass
```

### 9.3 Compression Summary

```text
FFlightCompressionSummary
```

Recommended fields:

```text
InstructionCompressionRatio
CallCompressionRatio
LiveRangeCompressionRatio
MemoryTrafficCompressionRatio
StateTransitionCompressionRatio
```

The exact ratios can compare:

- unoptimized vs optimized lowering
- baseline vs specialized path
- prior artifact vs current artifact

## 10. Assembly Inspection Questions

When inspecting any artifact, the project should ask:

1. did the live working set compress or expand?
2. did call density compress or expand?
3. did memory traffic compress or expand?
4. did the cold-start cost improve or worsen?
5. did locality improve or worsen?
6. did the backend route become more direct or more fragmented?

These are more reusable than asking:

- "is the assembly shorter?"
- "did the compiler use register X?"

## 11. Test Strategy

These models should drive assertions in the artifact-testing lane.

Good assertions:

- pressure class did not regress from `Low` to `High`
- helper thunk count decreased
- gather/scatter count is zero on direct SIMD path
- warm invoke time is within expected band
- compression ratio improved for optimized lowering

Bad default assertions:

- exact host register names match a golden file
- exact native asm text is identical across machines
- exact instruction order is frozen for broad codepaths

## 12. VEX And Verse VM Guidance

For the current Verse VM effort, prioritize:

- code-shape metrics from `FVexIrProgram`
- opcode-shape metrics from `VProcedure`
- transition count into the VM
- thunk count
- suspend-capable operation count

This will tell you whether the Verse bridge is becoming more compressed and direct over time.

Recommended immediate signals:

- direct procedure vs VM entry thunk path
- native thunk count per behavior
- peak live temporary estimate in IR
- bytecode block and jump count
- fused-batch transition count

## 13. SIMD And Native Guidance

For native backends, use assembly inspection to answer focused questions:

- was vectorization preserved?
- did stack traffic increase?
- did helper calls disappear?
- did live-range pressure improve?

Do not use it as a broad universal correctness oracle.

## 14. GPU Guidance

For GPU work, map the same concepts into GPU-friendly language:

- occupancy pressure
- subgroup coherency
- shared-memory pressure
- register-pressure estimate
- synchronization density
- memory divergence shape

This keeps CPU and GPU inspection conceptually aligned.

## 15. Recommended Rollout

### Phase 1: Normalize Code-Shape Metrics

1. Add `FFlightCodeShapeMetrics` to compile artifact reports.
2. Populate it first from VEX IR and VVM bytecode.
3. Add simple regression assertions on pressure class and call count.

### Phase 2: Add Warmup Metrics

1. Measure compile, assemble, first invoke, and warm invoke timings.
2. Report cold-start vs warm-path classes.
3. Add thresholds only after collecting baseline history.

### Phase 3: Add Compression Comparisons

1. Compare baseline and optimized lowering outputs.
2. Report compression ratios for instructions, calls, and transitions.
3. Use these ratios in optimization-oriented tests.

### Phase 4: Add Backend-Specific Extensions

1. Add native spill and stack-traffic heuristics where available.
2. Add GPU occupancy and divergence heuristics.
3. Restrict exact native assembly checks to pinned environments.

## 16. Decision Summary

FlightProject should inspect assembly and backend artifacts through explicit analysis models, not only through raw text snapshots.

The most important models are:

- register pressure and spill risk
- call density and control shape
- cache locality and working-set behavior
- warmup and cold-start cost
- structural compression

These models give the project a durable language for understanding why one lowering is better than another, even when the exact assembly text is unstable.
