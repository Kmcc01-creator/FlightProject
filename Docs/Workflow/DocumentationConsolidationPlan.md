# Documentation Consolidation Plan

This note tracks the current status of the less-frequently-updated documentation areas:

- `Docs/DevNotes`
- `Docs/Extensions`
- `Docs/Future`
- `Docs/Reviews`

The main issue in these folders is not low-quality content.
The issue is discoverability and status labeling.

## Current Assessment

### DevNotes

Status:

- mostly historical
- still useful for dated context
- should not compete with `Architecture/` or `Workflow/` as current guidance

Notable items:

- `004_RustToUnreal_Guide.md` still has niche onboarding value
- `Testing.md` still has reference value, but the live suite status belongs in `CurrentBuild.md` and `TestingValidationPlan.md`

### Extensions

Status:

- mixed reference and exploratory material
- a few practical guides remain useful
- several broader concept docs should stay clearly non-canonical

Likely keep as reference:

- `EditorServiceArchitecture.md`
- `EditorToolsGuide.md`
- `PythonReflectionGuide.md`

Likely keep as exploratory:

- `M2FrameworkIntegration.md`
- `MacrokidBehavioralSynthesis.md`
- parts of `ArchitectureAndWorkflow.md`

### Future

Status:

- purely exploratory
- should remain an idea parking area unless promoted into `Workflow/` or `Architecture/`

### Reviews

Status:

- useful as review scaffolding
- should be dated and explicitly archival when tied to a resolved implementation problem

## Current Cleanup Applied

- added directory `README.md` entrypoints for all four folders
- updated those entrypoints to classify files by status
- clarified that these folders are reference/exploratory/supporting material rather than canonical direction
- marked the reflection-system review prompt as archival supporting material via directory guidance

## Recommended Next Steps

1. add short archival/status banners to the older `DevNotes/*.md` files
2. decide whether `DevNotes/Testing.md` should be merged into `Workflow/TestingValidationPlan.md` or kept as a narrow reference
3. decide whether `Extensions/ArchitectureAndWorkflow.md` should be split into:
   - current workflow guidance
   - archived broader concept material
4. if more review prompts are added, split `Docs/Reviews` into current vs archived prompts
5. periodically prune or relabel extension/future docs that are no longer relevant to the active architecture
