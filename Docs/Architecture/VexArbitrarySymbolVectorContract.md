# VEX Arbitrary Symbol Vector Contract

This note defines how FlightProject should support arbitrary VEX attributes without making CPU vectorization or GPU lowering depend on hardcoded symbols such as `@position` or `@velocity`.

For the mutation/storage contract behind schema legality, see [VexStateMutationSchemaFrame.md](VexStateMutationSchemaFrame.md).
For the current SIMD proof boundary, see [SimdBackendContract.md](SimdBackendContract.md).
For target-aware host/device policy, see [TargetCapabilitySchema.md](TargetCapabilitySchema.md).
For the Unreal/clang structure behind explicit ISA kernels, see [ClangSimdBackendImplementation.md](ClangSimdBackendImplementation.md).

## 1. Problem

FlightProject already has schema-aware symbol metadata and multiple execution lanes.
What it does not have yet is a stable contract that answers:

- how an arbitrary symbol packs across CPU vector lanes
- whether that symbol can execute on a direct SIMD path, a gather/scatter path, or only scalar
- how the same symbol projects into GPU-capable storage
- how kernel implementations should be selected without relying on symbol names

Today, those questions are only partially answered by:

- storage kind
- alignment requirement
- SIMD read/write permission
- backend-specific hardcoded symbol subsets

That is enough for prototyping and not enough for scale.

## 2. Core Rule

Arbitrary symbols are acceptable.
Arbitrary execution surfaces are not.

The compiler/runtime should treat symbol names as logical identifiers only.
Vector legality should come from a schema-bound packing contract.

Recommended interpretation:

- `@position` is not special because of its name
- it is special only if its schema proves a vector-capable storage class, value type, access pattern, and alignment contract

## 3. Design Goal

Add a per-symbol vector contract that sits between logical symbol meaning and concrete kernel dispatch.

Recommended flow:

```text
logical symbol
    -> schema storage contract
    -> vector pack contract
    -> backend legality
    -> adapter availability
    -> kernel variant availability
    -> selected backend
    -> committed backend
```

This keeps arbitrary symbols possible while preserving explicit legality and report truth.

## 4. Proposed Storage Classes

The vector contract should project symbol storage into a small set of reusable classes.

Recommended type:

```cpp
enum class EVexVectorStorageClass : uint8
{
    None,
    ScalarOnly,
    AosPacked,
    SoaContiguous,
    MassFragmentColumn,
    GpuBufferColumn,
    GatherScatterFallback
};
```

Recommended meaning:

- `ScalarOnly`
  no vector pack contract exists; legal only on scalar lanes
- `AosPacked`
  contiguous field access across fixed-stride structures
- `SoaContiguous`
  contiguous column access across lane elements
- `MassFragmentColumn`
  direct Mass fragment field access, potentially lane-packable
- `GpuBufferColumn`
  direct GPU buffer element or column access
- `GatherScatterFallback`
  vector semantics may exist, but contiguous direct loads/stores are not guaranteed

Important rule:

- storage class is not the same thing as existing `EVexStorageKind`
- `EVexStorageKind` describes where the symbol lives
- `EVexVectorStorageClass` describes how that storage can be packed across lanes

## 5. Proposed Per-Symbol Vector Contract

Recommended type:

```cpp
struct FVexVectorPackContract
{
    EVexVectorStorageClass StorageClass = EVexVectorStorageClass::ScalarOnly;
    EFlightVexAlignmentRequirement MinAlignment = EFlightVexAlignmentRequirement::Any;
    uint32 NaturalLaneStrideBytes = 0;
    bool bContiguousLaneAccess = false;
    bool bSupportsGather = false;
    bool bSupportsScatter = false;
    bool bSupportsDirectSimdRead = false;
    bool bSupportsDirectSimdWrite = false;
    bool bSupportsGpuColumnRead = false;
    bool bSupportsGpuColumnWrite = false;
    TArray<uint8> PreferredPackWidths;
    TArray<EVexBackendKind> SupportedVectorBackends;
};
```

Recommended placement:

- add this to `FVexLogicalSymbolSchema`
- derive it in schema orchestration/binding
- preserve it into backend compatibility reports and compile artifacts

This turns SIMD and GPU legality into explicit symbol facts instead of executor-local assumptions.

## 6. Proposed Adapter Layer

An adapter layer is useful, but it should be a binding-time kernel contract, not a runtime OO abstraction in the hot loop.

Recommended types:

```cpp
struct FVexSimdFieldAdapterKey
{
    EVexBackendKind Backend = EVexBackendKind::NativeScalar;
    EVexValueType ValueType = EVexValueType::Unknown;
    EVexVectorStorageClass StorageClass = EVexVectorStorageClass::ScalarOnly;
    bool bWritable = false;
};

struct FVexSimdFieldAdapter
{
    FVexSimdFieldAdapterKey Key;
    uint8 SupportedPackWidth = 0;
    bool bRequiresContiguousLanes = false;
    bool bRequiresDirectStore = false;
};
```

Practical rule:

- the adapter chooses load/store semantics
- it does not decide program legality
- legality belongs in schema binding and backend capability evaluation

Recommended implementation style:

- registry of adapter descriptors
- function pointers or templated helpers per adapter
- variant selected once at compile/bind time or kernel-build time

Avoid:

- per-op virtual dispatch in the execution loop
- symbol-name special cases inside ISA kernels

## 7. Proposed Kernel Variant Key

Kernel selection should key off storage family and value shape, not hardcoded symbol identity.

Recommended types:

```cpp
struct FVexVectorKernelVariantKey
{
    EVexBackendKind Backend = EVexBackendKind::NativeScalar;
    EVexValueType ValueType = EVexValueType::Unknown;
    EVexVectorStorageClass StorageClass = EVexVectorStorageClass::ScalarOnly;
    uint8 PackWidth = 1;
    bool bDirectWrite = false;
};

struct FVexVectorKernelRequirements
{
    bool bNeedsContiguousAccess = false;
    bool bNeedsAlign16 = false;
    bool bNeedsAlign32 = false;
    bool bAllowsGather = false;
    bool bAllowsScatter = false;
};
```

Example interpretations:

- `float + MassFragmentColumn + NativeAvx256x8 + PackWidth=8`
- `float3 + AosPacked + NativeVector4xFloat + PackWidth=4`
- `float4 + GpuBufferColumn + GpuKernel`

This is the seam that allows arbitrary symbols to reuse the same kernel family when their contracts match.

## 8. Legality Model

A symbol should not collapse into a single yes/no SIMD answer.

Recommended report model:

```cpp
enum class EVexVectorLegalityClass : uint8
{
    ScalarOnly,
    VectorShapeLegal,
    HardwareSimdLegal,
    GpuLegal
};

struct FVexVectorLegalityReport
{
    FString SymbolName;
    EVexVectorLegalityClass HighestLegalClass = EVexVectorLegalityClass::ScalarOnly;
    TArray<EVexBackendKind> LegalBackends;
    TArray<FString> Reasons;
};
```

Important distinction:

- vector-legal does not mean hardware-SIMD-ratified
- GPU-legal does not mean a GPU runtime host is currently available
- a symbol may be legal for `VectorShape` and illegal for explicit AVX2

## 9. How This Changes Arbitrary Struct Design

If a user wants arbitrary attributes to be SIMD-friendly, the struct should not be “a special SIMD struct” in an ad hoc sense.
It should instead satisfy a schema contract that can project to a legal vector storage class.

That usually means:

- stable field type
- explicit alignment
- known lane stride
- direct load/store eligibility or accepted gather/scatter fallback
- compatible residency and thread affinity

Examples:

- a reflected AoS struct with `float`, `float3`, `float4` fields and stable offsets can project to `AosPacked`
- a Mass fragment field can project to `MassFragmentColumn`
- a GPU buffer-backed field can project to `GpuBufferColumn`

This keeps the authoring surface general while making performance intent explicit.

## 10. Recommended Design Rules

1. Do not make vector legality depend on symbol names.
2. Do not make hardware SIMD depend on accessor fallback.
3. Do not hide gather/scatter behind a generic “SIMD supported” label.
4. Do not create a separate VEX language mode for SIMD structs.
5. Let arbitrary symbols participate if their schema contract proves the right storage class.
6. Keep GPU and CPU vector legality under the same symbol contract where possible.

## 11. First Vertical Slice

The first implementation slice should stay small and prove the seam rather than trying to generalize every executor at once.

Recommended slice:

1. Add `EVexVectorStorageClass` and `FVexVectorPackContract` to the schema layer.
2. Derive the contract for:
   - `AosOffset -> AosPacked`
   - `MassFragmentField -> MassFragmentColumn`
   - `GpuBufferElement` or `SoaColumn -> GpuBufferColumn`
   - everything else -> `ScalarOnly` or `GatherScatterFallback`
3. Extend backend-capability evaluation to consume vector storage class alongside `EVexStorageKind`.
4. Add a minimal `FVexSimdFieldAdapterKey` path for:
   - `float`
   - `AosPacked`
   - `MassFragmentColumn`
5. Retarget one explicit SIMD kernel path to use adapter-selected load/store rules instead of hardcoded symbol checks.
6. Emit per-symbol vector legality into compile artifacts and orchestration reports.

Exit condition for the slice:

- one arbitrary reflected float field can become vector-legal without adding a symbol-name special case
- one Mass fragment float field can reuse the same legality model
- reports explain whether the symbol is scalar-only, vector-shape-legal, hardware-SIMD-legal, or GPU-legal

## 12. Longer-Term Direction

Once the first slice is stable, the same model can extend to:

- `float2`, `float3`, `float4` pack contracts
- mixed-lane legality such as `VectorShape` but not `HardwareSimd`
- variant kernel families by ISA backend
- GPU resource contracts aligned with the same symbol legality model
- profitability heuristics separate from legality

The important outcome is not just “more SIMD.”
It is a cleaner contract where arbitrary symbols can participate in vector and GPU execution because the schema proves how they pack, not because the executor recognizes their names.
