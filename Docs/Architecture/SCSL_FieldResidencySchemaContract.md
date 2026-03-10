# SCSL Field Residency Schema Contract (Draft)

Status: historical draft (planning contract only).
This document reflects older SCSL-era field residency concepts. For current GPU resource and contract direction, prefer [GpuResourceSchemaContract.md](GpuResourceSchemaContract.md) and [TargetCapabilitySchema.md](TargetCapabilitySchema.md).

## 1) Purpose

Define a single schema contract for how SCSL field residency is described and validated across:

- C++ reflection and manifest generation
- VEX symbol/type contracts
- Runtime field handle ownership
- HLSL sampling/injection addressing

This contract is intended to remove ambiguity between legacy `*Array` naming and current `Texture3D` runtime bindings.

## 2) Current Baseline (As-Implemented)

- VEX symbol contracts are code-first via `FFlightVexSymbolRow` and manifest generation.
- Runtime swarm field resources currently bind as `Texture3D` / `RWTexture3D` and are allocated with `Create3D(...)`.
- Helper signatures expose `LatticeID`/`CloudID` parameters but current helper bodies do not remap by ID.
- Current reflected swarm state (`FDroidState`) does not include explicit field page IDs.

## 3) Contract Layers

## 3.1 Layer A: Manifest Schema Rows

Existing row type:

- `FFlightVexSymbolRow` (already live): symbol name, type, lowering identifiers, required/writable policy.

Proposed row types for field residency:

1. `FFlightFieldPageRequirementRow`
   - Purpose: declarative definition of field pages (atlas pages or equivalent logical instances).
   - Required fields (draft):
     - `Owner` (`FName`)
     - `RequirementId` (`FName`)
     - `ProfileName` (`FName`)
     - `FieldKind` (`Lattice` or `Cloud`)
     - `PageId` (`int32`, `>= 0`)
     - `GridResolution` (`int32`, expected `32..256`)
     - `GridCenter` (`FVector3f`)
     - `GridExtent` (`float`, `> 0`)
     - `bPersistentAcrossFrames` (`bool`)
     - `bRequired` (`bool`)
2. `FFlightFieldBindingRow`
   - Purpose: bind swarm instance/class to field pages.
   - Required fields (draft):
     - `Owner` (`FName`)
     - `RequirementId` (`FName`)
     - `SwarmInstanceId` (`FName`)
     - `BehaviorClassId` (`int32`, optional sentinel `-1`)
     - `LatticePageId` (`int32`, sentinel `-1` allowed)
     - `CloudPageId` (`int32`, sentinel `-1` allowed)
     - `bRequired` (`bool`)

Validation invariants (manifest-level):

- (`ProfileName`, `FieldKind`, `PageId`) must be unique.
- Every non-sentinel page ID in `FFlightFieldBindingRow` must resolve to one declared page.
- At least one default page (`PageId = 0`) must exist per (`ProfileName`, `FieldKind`) when `bRequired=true`.

## 3.2 Layer B: VEX Symbol Contract

Required symbols (existing):

- `@position`, `@velocity`, `@shield`, `@status`

Proposed symbol additions:

- `@lattice_id` (`int`)
- `@cloud_id` (`int`)

Draft policy:

- Default these to writable in schema, but allow profile-specific read-only restrictions.
- If absent from source script, lowering/runtime must use fallback page `0`.

## 3.3 Layer C: Runtime Ownership Contract

Source of truth boundaries:

- `UFlightSwarmSubsystem` owns persistent field textures.
- Manifest/profile data owns page descriptor truth and ID validity.
- Per-entity page selection ownership is defined by one canonical source:
  - either `FDroidState`,
  - or a sideband structured buffer,
  - or per-instance command descriptors.

Contract rule:

- Only one owner path may define page IDs at runtime. Other paths must be read-only mirrors.

## 3.4 Layer D: Shader Addressing Contract

For any sampling/injection helper that accepts page ID:

- Input: `WorldPos`, `PageId`, `DescriptorBuffer`
- Output: deterministic UVW mapping and bounds behavior
- Failure mode: invalid page ID resolves to fallback page `0` (or drop write if configured)

Bounds policy (draft default):

- Read: clamp to page bounds.
- Write: reject out-of-bounds writes.

## 4) Persistence Semantics Contract

Per page (`bPersistentAcrossFrames`):

- `true`: propagation/read stages must seed from prior extracted persistent texture content.
- `false`: page must be explicitly cleared or rebuilt each frame.

No-ambiguity rule:

- If prior-frame content is not read, behavior must be documented as stateless for that page/profile.

## 5) Backward Compatibility Rules

Legacy compatibility mode:

- If field page/binding rows are absent, runtime behaves as single-page (`PageId=0`) for both lattice and cloud.
- Existing scripts that do not reference `@lattice_id`/`@cloud_id` continue to compile and execute.

## 6) Testing and Diagnostics Contract

Required schema tests (draft names):

- `FlightProject.Schema.Manifest.FieldPageContract`
- `FlightProject.Schema.Manifest.FieldBindingContract`
- `FlightProject.Schema.Vex.Symbol.FieldIds`

Required runtime tests (draft names):

- `FlightProject.Swarm.Pipeline.Lattice.Persistence`
- `FlightProject.Swarm.Pipeline.Cloud.Persistence`
- `FlightProject.Swarm.Pipeline.FieldAddressing.PageFallback`

Required diagnostics:

- Emit page selection + descriptor ID in failure paths tied to `LogAutomationController` assertions.
- Emit profile + page counts at simulation startup for reproducible captures.

## 7) Versioning Policy

- Next manifest version target: `0.3` when field page/binding rows are introduced.
- `0.2` remains valid compatibility mode (single-page default path).
