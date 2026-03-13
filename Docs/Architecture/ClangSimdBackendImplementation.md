# Clang SIMD Backend Implementation

For the contract boundary between `VectorShape` and proven `HardwareSimd`, start with [SimdBackendContract.md](SimdBackendContract.md).
For the current host/runtime evidence surface, see [OrchestrationSubsystem.md](OrchestrationSubsystem.md).
For the broader SIMD/GPU rollout sequence, see [SIMD_Strategy_ImplementationPlan.md](SIMD_Strategy_ImplementationPlan.md).

This note turns the generic clang/SIMD discussion into an implementation structure for Unreal Engine and FlightProject.

## 1. Goal

FlightProject needs a CPU vector path that can eventually make all of these claims separately and truthfully:

- the behavior is `VectorShape`-legal
- the behavior is legal for a specific hardware SIMD backend
- the binary contains code compiled for that ISA
- the current host can dispatch that ISA
- the committed runtime path actually used that backend

This note focuses on how to structure that on clang within Unreal Build Tool.

## 2. Core Rule

Do not treat clang auto-vectorization as the backend contract.

Use these three tiers instead:

- `VectorShape` tier:
  portable 4-lane execution model, no hardware SIMD guarantee
- `HardwareSimd` tier:
  explicit ISA-backed lowering such as AVX2 or Neon
- `Fallback` tier:
  scalar/native path when legality, host support, or ratification fails

Important consequence:

- generating "vector-looking" C++ is not enough
- `-O3` and loop-vectorize remarks are useful diagnostics, not proof
- explicit intrinsics or target-specific builtins are the proof boundary for `HardwareSimd`

## 3. Recommended FlightProject Structure

Use a split architecture instead of compiling the whole module for one ISA.

### 3.1 Keep the Main Module Portable

`Source/FlightProject/FlightProject.Build.cs` should remain broadly portable.

Do not set:

- global `-march=native`
- global `-mavx2`
- global `-mavx512*`

for the entire `FlightProject` module.

Reason:

- Unreal modules contain unrelated gameplay/editor/runtime code
- global ISA flags overstate the binary contract
- they make reports like `compiledHardwareSimdTarget` less meaningful
- they reduce portability and make testing/fallback harder

### 3.2 Put ISA-Specific Code Behind Narrow Kernel Boundaries

Recommended source layout:

- `Source/FlightProject/Public/Vex/FlightHardwareSimdKernels.h`
- `Source/FlightProject/Private/Vex/SimdKernels/FlightVectorShapeKernels.cpp`
- `Source/FlightProject/Private/Vex/SimdKernels/FlightAvx2Kernels.cpp`
- `Source/FlightProject/Private/Vex/SimdKernels/FlightAvx512Kernels.cpp`
- `Source/FlightProject/Private/Vex/SimdKernels/FlightNeonKernels.cpp`

Recommended runtime layers:

- legality and selection:
  `FlightVexBackendCapabilities`
- lowering:
  SIMD/backend-specific lowering surface under `Vex/`
- dispatch:
  `UFlightVerseSubsystem` or a dedicated backend dispatcher
- reporting:
  compile artifacts plus orchestration/system verification report

### 3.3 Keep the Backend ABI Stable

Define a stable kernel ABI that does not expose ISA details to higher layers.

Example shape:

```cpp
struct FFlightSimdKernelArgs
{
	const void* ReadBase = nullptr;
	void* WriteBase = nullptr;
	int32 ElementCount = 0;
	int32 StrideBytes = 0;
};

enum class EFlightHardwareSimdBackend : uint8
{
	None,
	VectorShape4xFloat,
	Sse128x4,
	Avx256x8,
	Avx512x16,
	Neon128x4
};

using FFlightSimdKernelFn = void(*)(const FFlightSimdKernelArgs&);
```

The VEX compiler/runtime should choose a backend identity and bind a function pointer or kernel record.

Higher layers should not need to know whether the implementation uses:

- intrinsics
- vector builtins
- target attributes
- separate translation units

## 4. Clang Control Surfaces

FlightProject should use clang in two different ways depending on backend strength.

### 4.1 VectorShape Backends

For `NativeVector4xFloat` style execution:

- portable C++ or current executor path is acceptable
- clang auto-vectorization may happen
- vectorization remarks are diagnostic only

Useful tools:

- `-O2` or `-O3`
- `-Rpass=loop-vectorize`
- `-Rpass-missed=loop-vectorize`
- `#pragma clang loop vectorize(enable) vectorize_width(4)`

But this tier must still report as `VectorShape`, not proven `HardwareSimd`.

### 4.2 HardwareSimd Backends

For `NativeAvx256x8`, `NativeAvx512x16`, or `NativeNeon128x4`:

- use explicit intrinsics or target-specific builtins
- compile those functions with explicit ISA permission
- verify dispatch against runtime host support

For clang on x86, preferred function-level model:

```cpp
#if defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
static void RunAvx2Kernel(const FFlightSimdKernelArgs& Args)
{
	// AVX2 intrinsics live here.
}
```

Possible x86 targets:

- `target("sse4.2")`
- `target("avx2,fma")`
- `target("avx512f,avx512bw,avx512vl")`

Possible ARM target approach:

- platform/UE build-gated Neon path
- explicit Neon intrinsics in a dedicated kernel file

Preferred rule for FlightProject:

- use per-function target attributes or narrow ISA-specific translation units
- do not rely on whole-module target flags unless a future dedicated backend module is split out

## 5. Unreal Build Tool Structure

### 5.1 Phase-One Recommendation

Keep everything in the existing `FlightProject` module and use:

- portable default module flags
- clang target attributes on hardware-SIMD helper functions
- normal preprocessor guards around ISA-specific includes

This keeps the first hardware-SIMD slice narrow and testable.

### 5.2 Optional Later Refactor

If hardware backends grow large, split them into focused modules such as:

- `FlightSimdKernels`
- `FlightGpuCompute`

That refactor becomes worthwhile when:

- ISA-specific code volume grows materially
- build times or platform conditionals become noisy
- separate compilation/reporting policy is needed

### 5.3 Build Definitions

Use module definitions only for stable contract flags, for example:

- `FLIGHT_WITH_EXPLICIT_AVX2_KERNELS=1`
- `FLIGHT_WITH_EXPLICIT_AVX512_KERNELS=1`
- `FLIGHT_WITH_EXPLICIT_NEON_KERNELS=1`

Do not use definitions to imply host support.

Those definitions mean only:

- the binary includes a backend implementation

Host dispatch still depends on runtime verification.

## 6. Recommended Code Organization

### 6.1 Backend Registry

Add a small backend registry that records:

- backend identity
- whether it is `VectorShape` or `HardwareSimd`
- required ISA features
- pack width
- supported VEX op subset
- function pointer or kernel entry

Suggested ownership:

- declaration in `Public/Vex/`
- implementation in `Private/Vex/SimdKernels/`

### 6.2 Separate Selection From Dispatch

Selection should happen at compile/lowering time:

- is the program legal for AVX2?
- is it legal only for VectorShape?
- what downgrade reason applies?

Dispatch should happen at runtime:

- does the host support AVX2?
- does the current storage host satisfy alignment and layout?
- did we actually commit AVX2, or downgrade to VectorShape/scalar?

### 6.3 Keep One Truth Path For Reports

The same backend record chosen by the compiler should feed:

- compile diagnostics
- orchestration behavior reports
- system verification reports
- commit truth

Do not invent a second ad hoc string path for reporting.

## 7. Ratification Strategy

FlightProject should ratify hardware-SIMD claims through four checks.

### 7.1 Codegen Permission

The function is compiled with explicit ISA permission.

Examples:

- clang target attribute
- narrow TU compiled with ISA-specific flags

### 7.2 Lowering Proof

The lowering path emits only ops supported by that backend.

### 7.3 Host Dispatch Proof

The host runtime report shows the required ISA features are available.

The current orchestration/system-verification surface is the right first seam for that proof.

### 7.4 Artifact Proof

For backend tests, inspect emitted artifacts.

Recommended verification modes:

- compile remarks for vectorization diagnostics
- assembly/object inspection for ISA-specific kernels
- runtime report asserting selected vs committed backend
- scalar/vector parity tests

## 8. Concrete FlightProject Recommendation

Use this rollout:

### Phase A

Keep current executor classified as:

- `NativeVector4xFloat`

and never call it guaranteed hardware SIMD.

### Phase B

Add a narrow explicit kernel slice for one backend:

- `NativeAvx256x8` on Linux/x86_64

Why:

- current host baseline already reports AVX2/FMA as the primary verification target
- AVX2 is a better portability baseline than AVX-512
- it gives a clean first ratified hardware-SIMD lane

### Phase C

Add compile/runtime/report alignment:

- compile artifact says `selectedBackend = NativeAvx256x8`
- runtime checks host AVX2/FMA support
- orchestration reports `compiledHardwareSimdTarget`
- commit truth says whether `NativeAvx256x8` actually ran

### Phase D

Add assembly-backed backend tests for the narrow kernel family.

## 9. What Not To Do

Avoid these traps:

- do not compile the whole module with `-march=native`
- do not call auto-vectorized scalar code a hardware-SIMD backend
- do not let backend names hide fallback
- do not let host capability imply build capability
- do not let build capability imply committed runtime path

## 10. Exit Condition

This note is satisfied when FlightProject can truthfully say:

- this VEX behavior is legal for `NativeAvx256x8`
- the binary contains an AVX2/FMA kernel for it
- the host supports AVX2/FMA
- runtime committed that backend
- reports and tests can prove each of those statements independently
