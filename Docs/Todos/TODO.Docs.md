# TODO: Docs

This file tracks TODOs for documentation maintenance, status labeling, and canonical-reference hygiene.

## Current TODOs

### 1. Testing Validation Plan Refresh

Priority: High  
Status: Active  
Owner/Surface: testing topology documentation

Refresh `TestingValidationPlan.md` so its declared test counts, generated-suite framing, and recommended filter examples match the current automation surface instead of older topology snapshots.

Relevant surfaces: [TestingValidationPlan.md](../Workflow/TestingValidationPlan.md), [TODO.Testing.md](TODO.Testing.md), [run_tests_phased.sh](/home/kelly/Unreal/Projects/FlightProject/Scripts/run_tests_phased.sh).

### 2. Documentation Consolidation Follow-Through

Priority: Medium  
Status: Planned  
Owner/Surface: older `DevNotes` status labeling

Add short archival/status banners to older `DevNotes` files so they do not compete with `Architecture/` and `Workflow/` as current guidance.

Relevant surfaces: [DocumentationConsolidationPlan.md](../Workflow/DocumentationConsolidationPlan.md), [Docs/DevNotes](/home/kelly/Unreal/Projects/FlightProject/Docs/DevNotes).

### 3. Testing-Doc Placement Decision

Priority: Medium  
Status: Planned  
Owner/Surface: testing documentation organization

Decide whether [Testing.md](/home/kelly/Unreal/Projects/FlightProject/Docs/DevNotes/Testing.md) should be merged into [TestingValidationPlan.md](../Workflow/TestingValidationPlan.md) or retained as a narrow reference file with clearer status labeling.

### 4. Extensions Split Decision

Priority: Low  
Status: Planned  
Owner/Surface: `Docs/Extensions` canonical-vs-archival boundary

Decide whether `Extensions/ArchitectureAndWorkflow.md` should be split into current workflow guidance vs archived concept material.

Relevant surfaces: [DocumentationConsolidationPlan.md](../Workflow/DocumentationConsolidationPlan.md), [ArchitectureAndWorkflow.md](/home/kelly/Unreal/Projects/FlightProject/Docs/Extensions/ArchitectureAndWorkflow.md).

### 5. Review Archive Hygiene

Priority: Low  
Status: Monitoring  
Owner/Surface: `Docs/Reviews` organization

If more review prompts or resolved review artifacts accumulate, split `Docs/Reviews` into current vs archival materials instead of letting one folder serve both roles implicitly.

Relevant surfaces: [DocumentationConsolidationPlan.md](../Workflow/DocumentationConsolidationPlan.md), [Docs/Reviews](/home/kelly/Unreal/Projects/FlightProject/Docs/Reviews).

### 6. Canonical Link Hygiene

Priority: Medium  
Status: Active  
Owner/Surface: docs entrypoints and cross-link discipline

Keep using `Docs/README.md` as the main entrypoint and prefer linking to canonical docs instead of repeating the same architectural story across multiple files.

Relevant surfaces: [Docs/README.md](/home/kelly/Unreal/Projects/FlightProject/Docs/README.md), [Workflow/CurrentFocus.md](../Workflow/CurrentFocus.md), [Architecture/CurrentProjectVision.md](../Architecture/CurrentProjectVision.md).

### 7. TODO-Surface Discipline

Priority: Medium  
Status: Active  
Owner/Surface: document-role hygiene across `Todos/` and `Workflow/`

Keep active unfinished work in `Docs/Todos` and keep dated execution evidence in `Workflow/CurrentBuild.md` / `Workflow/CurrentFocus.md` so the roles of each document class remain clear.

Relevant surfaces: [README.md](README.md), [TODOS.md](TODOS.md), [CurrentBuild.md](../Workflow/CurrentBuild.md), [CurrentFocus.md](../Workflow/CurrentFocus.md).

## Exit Condition

- older/reference docs are clearly labeled
- canonical entrypoints stay short and current
- TODO tracking, dated status, and architecture intent remain separated by document role

## Completed / Archived

- None yet.
