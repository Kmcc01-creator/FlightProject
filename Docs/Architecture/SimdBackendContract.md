# SIMD Backend Contract

This note defines how FlightProject should talk about CPU vector execution, hardware SIMD guarantees, and the evidence required before the compiler or runtime may claim a SIMD backend.

For the target-detection layer, see [TargetCapabilitySchema.md](TargetCapabilitySchema.md).
For the mutation/storage contract behind symbol legality, see [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md).
For schema-bound vector packing on arbitrary symbols, see [VexArbitrarySymbolVectorContract.md](VexArbitrarySymbolVectorContract.md).
For the Unreal/clang implementation structure behind explicit ISA kernels, see [ClangSimdBackendImplementation.md](ClangSimdBackendImplementation.md).
For the older SIMD/HLSL rollout plan, see [SIMD_Strategy_ImplementationPlan.md](SIMD_Strategy_ImplementationPlan.md).

## 1. Problem

FlightProject currently has a useful 4-wide vector execution model, but that model is not yet the same thing as a guaranteed hardware SIMD backend.

Today, several ideas are still partially collapsed together:

- a symbol is "SIMD-friendly"
- a program is "vector-shaped"
- the compiler may be able to auto-vectorize the generated code
- the runtime can execute a 4-wide batch abstraction
- the emitted machine code is guaranteed to use a specific ISA such as SSE, AVX, or Neon

Those are different claims.
The project should report them separately.

## 2. Core Rule

FlightProject must not use "SIMD" as a vague synonym for "vector-capable."

Instead, use these terms:

- `Scalar`: ordinary scalar execution semantics
- `VectorShape`: an explicit N-lane execution model chosen by FlightProject, without claiming a specific machine ISA
- `HardwareSimd`: a backend that emits or binds to an explicit CPU SIMD ISA and whose legality is proven by compile-time and runtime checks

Practical interpretation:

- the current `FVexSimdExecutor` is closer to `VectorShape4xFloat`
- it should not be treated as a proven `HardwareSimd` backend until the ISA contract is explicit

## 3. Recommended Backend Naming

Replace the current overloaded idea of `NativeSimd` with more specific backend identities.

Recommended CPU vector hierarchy:

- `NativeScalar`
- `NativeVector4xFloat`
- `NativeSse128x4`
- `NativeAvx256x8`
- `NativeAvx512x16`
- `NativeNeon128x4`

Recommended reporting rule:

- `NativeVector4xFloat` means FlightProject selected a 4-lane vector execution model
- `NativeSse128x4` or `NativeNeon128x4` means the backend contract includes an explicit ISA guarantee

This keeps the ABI choice separate from the hardware proof.

## 4. Contract Layers

To claim a hardware SIMD backend, all four layers below must agree.

### 4.1 Symbol/Schema Contract

Schema must prove that every symbol use is legal for the requested SIMD lane.

Required facts per symbol use:

- residency
- affinity
- value type
- read/write permission
- SIMD read/write permission
- alignment requirement
- storage kind
- direct-load eligibility vs fallback-only

Important rule:

- hardware SIMD backends should not rely on accessor fallback

If a symbol requires accessor fallback, the backend may still be `VectorShape`, but it should not be reported as a guaranteed hardware SIMD lane.

### 4.2 Lowering Contract

The VEX lowerer must emit only operations that the backend explicitly supports.

Examples:

- supported arithmetic ops
- supported comparisons
- supported intrinsics such as `sin`
- supported assignment forms
- whether branches are legal
- whether mixed scalar/vector temporaries are legal

If the lowerer cannot prove that the program stays within the backend subset, it must reject or downgrade before code generation.

### 4.3 Build/ISA Contract

The backend must define a concrete ISA target or explicitly declare that it is ISA-agnostic.

For a true hardware SIMD backend, the contract should include:

- target ISA family
- lane width in bits
- float-lane count
- required compiler flags or function attributes
- whether lowering uses intrinsics, vector builtins, or target-specific codegen

Key rule:

- ordinary C++ auto-vectorization is not a proof obligation satisfier

Auto-vectorization may improve scalar or `VectorShape` code, but it should not be the reason the project claims `HardwareSimd`.

### 4.4 Runtime Dispatch Contract

Before dispatch, runtime must validate:

- host CPU feature availability
- OS-enabled support for that ISA
- storage host compatibility
- alignment guarantees
- any backend-specific pack-width requirement

If those checks fail, runtime must downgrade before commit and report the downgrade explicitly.

## 5. Ratification Rules

FlightProject should only mark a behavior as hardware-SIMD-committed when it can ratify the backend claim through evidence.

Required ratification surfaces:

### 5.1 Compile Artifact Evidence

Compile artifacts should report:

- selected vector backend identity
- whether the backend is `VectorShape` or `HardwareSimd`
- required ISA
- required pack width
- symbol uses rejected for SIMD
- symbol uses accepted only for non-guaranteed vector shape
- downgrade reason when the requested SIMD lane could not be proven

### 5.2 Runtime Evidence

Runtime commit/report surfaces should report:

- selected backend
- committed backend
- target feature fingerprint
- downgrade-from-selected reason
- whether the committed backend is guaranteed hardware SIMD or only vector-shaped execution

### 5.3 Validation Evidence

Validation should cover three different questions:

1. legality:
   did the compiler correctly reject unsupported symbol/storage/op combinations?
2. parity:
   does the vector backend match scalar results within tolerance?
3. backend proof:
   did the target-specific backend actually compile and dispatch under the expected ISA contract?

The third question may require disassembly inspection, object-code checks, or target-specific backend tests.

## 6. Current Project Implications

This note implies the following about the current codebase:

- current `NativeSimd` reporting is stronger than the current proof level
- the current executor should be treated as `NativeVector4xFloat` until ISA-specific lowering exists
- schema/backend legality must be brought into closer alignment with the concrete executor subset
- Mass-direct vector execution and AoS vector execution should be modeled as separate lane capabilities if they continue to differ

Recommended reporting split:

- `ScalarOnly`
- `VectorShapeLegal`
- `HardwareSimdLegal`
- `HardwareSimdCommitted`

That keeps execution-shape reporting honest even before explicit ISA lowerers land.

## 7. Near-Term Implementation Direction

### Phase 1

Rename the conceptual backend in docs and reports:

- current `NativeSimd` semantics -> `NativeVector4xFloat`

Keep the old string only as a compatibility alias if needed.

### Phase 2

Extend target capability/schema policy so the compiler can choose among:

- portable vector shape
- SSE/AVX/Neon-specific lanes

### Phase 3

Introduce explicit ISA-backed lowerers and runtime feature gates.

At that point, a committed backend may truthfully report:

- `NativeSse128x4`
- `NativeAvx256x8`
- `NativeNeon128x4`

### Phase 4

Add ratification tests and compile-artifact evidence so backend claims are auditable.

## 8. Exit Condition

This contract is in good shape when:

- "vector-capable" and "hardware-SIMD-guaranteed" are different reportable truths
- backend legality, runtime dispatch, and commit reporting use the same backend identities
- SIMD claims are backed by explicit ISA or backend evidence rather than hope for auto-vectorization
- downgrade paths are reported clearly instead of being hidden behind generic `NativeSimd` labeling
