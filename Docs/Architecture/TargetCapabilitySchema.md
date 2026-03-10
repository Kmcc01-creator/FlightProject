# Target Capability Schema

This document formalizes how FlightProject should detect host and device capabilities and turn them into stable compiler/runtime policy.

The goal is not to reverse-engineer the real register allocator of a CPU or GPU.
The goal is to create a normalized target description that can guide VEX lowering, Verse VM specialization, GPU kernel configuration, and orchestration reporting.

For project organization and ownership rules, see `Docs/Architecture/ProjectOrganization.md`.
For the world-scoped coordination surface that should report active target state, see `Docs/Architecture/OrchestrationSubsystem.md`.
For the current tiered execution model, see `Docs/CurrentProjectSynthesis.md` and `Docs/Architecture/VexVerseOrchestration.md`.

## 1. Problem Statement

FlightProject is already target-aware in concept:

- Tier 1: CPU SIMD
- Tier 2: GPU compute
- Tier 3: Verse VM / async CPU

What is currently missing is a clean boundary between:

- raw system detection facts
- normalized target capability data
- derived compilation and execution policy

Without that boundary, target-aware decisions tend to become:

- hardcoded feature probes in backend code
- scattered heuristics for SIMD eligibility
- GPU subgroup assumptions that are not explicit
- vague "register pressure" language with no stable policy surface

## 2. Design Goal

Introduce two explicit layers:

1. `TargetCapabilitySchema`
2. `CompilationPolicy`

Recommended interpretation:

- `TargetCapabilitySchema` answers: what does this host/device reliably expose?
- `CompilationPolicy` answers: given those capabilities, how should FlightProject compile and schedule work?

This keeps detection separate from optimization strategy.

## 3. Core Principle

System detection can expose ISA, shader, and execution-shape capabilities.
It usually cannot expose the true microarchitectural details required for exact register allocation strategy.

That means FlightProject should:

- use detection to choose code shape, tier routing, SIMD width, subgroup assumptions, and pressure heuristics
- avoid pretending it knows hidden scheduler or allocator internals
- use measurement to refine decisions where detection is insufficient

In short:

- capabilities are facts
- policy is inference
- calibration is evidence

## 4. What Detection Can Reliably Tell Us

### 4.1 CPU

Reliable inputs:

- architecture family (`x86_64`, `arm64`)
- normalized CPU name / target name
- ISA features (`sse4.2`, `avx2`, `avx512f`, `fma`, `neon`)
- OS-enabled execution support for those features
- vector width classes that are legal to target
- cache-line and page-size facts when available

Useful examples of detection interfaces:

- x86 `CPUID`
- Linux `getauxval(AT_HWCAP/AT_HWCAP2)`
- Windows processor feature queries
- LLVM host CPU name / feature enumeration

### 4.2 GPU

Reliable inputs:

- active graphics or compute API
- vendor and device identifiers
- shader model or equivalent capability level
- subgroup or wave size bounds
- supported subgroup operations
- workgroup size limits
- shared memory or LDS limits when exposed
- device memory model traits such as UMA vs discrete

Backend-specific additions may exist:

- CUDA can expose occupancy-relevant limits such as registers per multiprocessor
- generic Vulkan and D3D12 paths usually expose execution shape and capability level, not exact register allocator limits

## 5. What Detection Does Not Reliably Tell Us

The capability schema must not claim more certainty than the platform provides.

Examples of information FlightProject should not model as "detected fact":

- exact spill cost on a specific CPU microarchitecture
- reorder buffer size
- rename register file size
- issue-port contention details
- real GPU VGPR and SGPR allocation behavior for a graphics driver path
- actual live-range splitting decisions a driver compiler will make

Those belong to:

- heuristics
- offline tuning knowledge
- optional runtime calibration

## 6. Proposed Layering

### Layer A: Raw Detection

Platform and RHI specific code gathers host/device facts from OS, CPU, and GPU interfaces.

Recommended ownership:

- `Platform/` for CPU and OS feature detection
- render backend bridge for GPU/RHI capability detection

### Layer B: Target Capability Schema

A normalized, serializable, engine-facing representation of active target facts.

Recommended ownership:

- `Schema/` for the plain contract and versioning

### Layer C: Compilation Policy

A derived policy object built from capability data plus FlightProject heuristics.

Recommended ownership:

- `Vex/` for lowering and backend routing policy
- `Verse/` for VM specialization policy

### Layer D: Orchestration Visibility

The active schema and policy should be reportable at world scope, but orchestration should not own detection logic.

Recommended ownership:

- `Orchestration/` for report export and active-profile visibility

## 7. Proposed Schema Shape

The schema should remain capability-oriented, not allocator-oriented.

Illustrative shape:

```json
{
  "schemaVersion": 1,
  "targetFingerprint": "hostcpu-gpu-device-driver",
  "cpu": {
    "arch": "x86_64",
    "cpuName": "znver5",
    "features": ["avx2", "bmi2", "fma"],
    "osEnabledFeatures": ["avx2", "xsave"],
    "maxLegalSimdWidthBits": 256,
    "preferredVectorWidthBits": 256
  },
  "gpu": {
    "api": "Vulkan",
    "vendor": "AMD",
    "deviceName": "Radeon",
    "subgroup": {
      "defaultSize": 32,
      "minSize": 32,
      "maxSize": 64,
      "supportedOps": ["basic", "ballot", "shuffle", "vote"]
    },
    "maxThreadsPerGroup": 1024,
    "supportsWaveOps": true,
    "supportsSubgroupSizeControl": false,
    "uma": false
  }
}
```

## 8. Proposed Policy Shape

`CompilationPolicy` should be a derived artifact.
It is allowed to contain heuristics and preferred defaults.

Illustrative shape:

```json
{
  "tierRouting": {
    "preferSimdTier": true,
    "preferGpuTier": true,
    "allowVerseVmTier": true
  },
  "simdPolicy": {
    "packWidthLanes": 8,
    "preferFma": true,
    "strictParityMath": false
  },
  "gpuPolicy": {
    "preferredSubgroupSize": 32,
    "preferredThreadGroupSize": 64,
    "allowWaveIntrinsics": true
  },
  "registerPressurePolicy": {
    "maxLiveTempsSoft": 24,
    "maxLiveTempsHard": 40,
    "splitHighPressureBlocks": true,
    "preferRematerialization": true
  }
}
```

This policy is where FlightProject should express:

- soft limits
- preferred workgroup sizes
- vector pack widths
- when to avoid aggressive inlining
- when to split blocks or shorten live ranges

## 9. Register Strategy Guidance

This architecture should guide register management without overfitting to hidden hardware details.

### 9.1 Keep VM Registers Abstract

Verse VM and VEX IR should retain virtual register identity independent of hardware target.

Recommended rule:

- IR owns virtual values and live ranges
- backend policy decides how aggressively to compress, split, rematerialize, or spill

Do not encode physical host register assumptions into the semantic IR.

### 9.2 Use Pressure Classes, Not Fake Physical Counts

Instead of claiming "this machine has N usable registers for this workload", classify code into pressure bands:

- `Low`
- `Medium`
- `High`
- `Critical`

Pressure should be estimated from:

- live temporary count
- vector width
- value category (`scalar`, `vector`, `handle`, `aggregate`)
- call and barrier density
- control-flow fanout

These classes can then drive:

- block splitting
- rematerialization preference
- temporary lifetime shortening
- forced materialization barriers at call boundaries
- fallback from aggressive SIMD/GPU lowering to safer paths

### 9.3 Separate Generic And Backend-Specific Hints

Generic hints:

- `maxLiveTempsSoft`
- `splitHighPressureBlocks`
- `preferRematerialization`
- `avoidWideVectorizationUnderPressure`

Backend-specific hints:

- preferred subgroup size
- preferred threadgroup size
- occupancy-sensitive kernel class
- CUDA-only occupancy/register limits when that backend is active

The generic policy should always remain valid even when backend-specific data is absent.

## 10. Detection Provenance and Confidence

Every normalized capability fact should track where it came from.

Recommended provenance classes:

- `Direct`
- `Derived`
- `Measured`
- `ConfiguredOverride`

Examples:

- CPU ISA flag from `CPUID`: `Direct`
- preferred vector width derived from legal ISA set: `Derived`
- subgroup size override from calibration cache: `Measured`
- developer-forced fallback profile: `ConfiguredOverride`

This prevents policy from silently mixing facts and guesses.

## 11. Calibration Layer

Detection should be the baseline, not the end state.

Add an optional calibration phase that runs small benchmark kernels and records:

- SIMD throughput classes
- preferred subgroup or threadgroup sizes
- kernels that become occupancy-limited under high temporary count
- numeric parity constraints that force stricter math modes

Recommended rule:

- first compile from capability schema
- refine with measurement when available
- cache calibrated overrides by target fingerprint

This is the correct place to discover practical limits that detection interfaces do not expose.

## 12. Caching and Fingerprinting

Compiled outputs should key off a stable target fingerprint rather than ad hoc feature strings.

Recommended fingerprint inputs:

- schema version
- platform triple
- CPU name
- normalized CPU feature set
- GPU vendor and device identifiers
- graphics/compute API family
- relevant driver/runtime version when it affects generated code
- FlightProject compiler policy version

This allows:

- reusable compiled artifacts
- explicit invalidation when the environment changes
- per-target policy caches

## 13. Observability

The active world should be able to report:

- detected capability schema
- derived compilation policy
- calibration state
- selected execution tier per behavior
- pressure class per compiled program
- fallback reasons when a more aggressive tier was rejected

Recommended surfaces:

- orchestration report JSON
- scripting export path
- compile diagnostics in `UFlightVerseSubsystem`
- per-script telemetry in VEX compile outputs

## 14. Recommended FlightProject Ownership

Folder ownership should align with existing project rules.

| Concern | Recommended Owner |
| --- | --- |
| raw CPU/OS detection | `Platform/` |
| raw GPU/RHI detection | render backend bridge |
| normalized capability contract | `Schema/` |
| policy derivation | `Vex/` |
| Verse VM specialization | `Verse/` |
| world-visible reporting | `Orchestration/` |

Do not place reusable target-schema or policy derivation logic directly in `GameMode`.

## 15. Initial Rollout

### Phase 1: Schema Contract

1. Add plain structs for target capability schema and compilation policy.
2. Populate CPU facts first.
3. Expose schema serialization for diagnostics.

### Phase 2: GPU Capability Integration

1. Normalize subgroup, wave, and workgroup facts from the active backend.
2. Feed those facts into HLSL and GPU-tier routing decisions.

### Phase 3: Policy-Driven Lowering

1. Replace hardcoded backend heuristics with policy queries.
2. Add pressure-class diagnostics to compile outputs.
3. Make SIMD and GPU routing decisions reportable.

### Phase 4: Calibration

1. Add optional benchmark-driven overrides.
2. Cache calibrated profiles by target fingerprint.
3. Compare measured behavior against static policy defaults.

## 16. Decision Summary

FlightProject should formalize target awareness through a capability schema and a derived compilation policy.

It should not attempt to infer exact physical register allocation behavior from runtime detection alone.

The practical model is:

- detect facts
- normalize them into schema
- derive policy
- calibrate where needed
- report the result through orchestration and compile diagnostics

That gives the project a defensible path for register-pressure strategy, backend routing, and future VM/JIT specialization without building on false architectural certainty.
