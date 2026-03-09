# VEX UI Scaffold

This document defines the initial scaffold for VEX-authored Slate interfaces and reactive data inspection.

## Goal

Provide a CPU-side path to:

- Author simple UI composition in a VEX-flavored DSL.
- Bind widgets to reactive symbols (`@symbol`) without widget boilerplate.
- Surface parser/compiler diagnostics in tabular form during iteration.

## Current Scope

### UI DSL (`Flight::VexUI::FVexUiCompiler`)

Supported directives:

- `tab("Name")`
- `section("Name") { ... }`
- `text("literal")`
- `text(@symbol)`
- `slider(@symbol, min, max)`
- `table(@rows, "colA,colB,...")`
- `schema_table("vexSymbolRequirements")`
- `schema_form("vexSymbolRequirements", "0")`
- `include("script_id")`

Behavior:

- Includes compose scripts from an in-memory library.
- Include cycles are rejected.
- Section nesting is tracked as a `SectionPath` for grouped rendering.
- `schema_table(...)` queries manifest data and emits auto-column table nodes.
- `schema_form(...)` binds editable controls for one schema row.

### Reactive Store (`Flight::VexUI::FVexUiStore`)

Signal types:

- `Number`: `float`
- `Text`: `FString`
- `Bool`: `bool`
- `Table`: `TArray<FVexUiTableRow>`

`SVexPanel` binds directly to these signals and updates in-place.
Rows may carry `ValidationIssues`, rendered as `[OK]` / `[!]` badges.
Schema form edits route through `TryApplySchemaEdit` and are validated before mutation.
`FVexUiStore::SetSchemaCommitHook(...)` can enforce persistence/audit at commit time.

### Orchestrator Integration

`SSwarmOrchestrator` now mounts an `SVexPanel` instance that shows:

- Compile status (`@status_text`)
- Issue count (`@issue_count`)
- A control signal slider (`@spawn_rate`)
- A diagnostics table (`@issues`)
- A schema-backed manifest table (`schema_table("vexSymbolRequirements")`)
- A first editable manifest row form (`schema_form("vexSymbolRequirements", "0")`)

Compile/validation results from `ParseAndValidate` are translated into typed table rows for reactive display.
In `SSwarmOrchestrator`, schema edits are committed to `Saved/Flight/Schema/ui_edits/<table>.json` and trigger a manifest export refresh.
The commit hook now emits three snapshot artifacts per accepted edit:

- `latest`: `Saved/Flight/Schema/ui_edits/<table>.json`
- `history`: `Saved/Flight/Schema/ui_edits/history/<table>_<timestamp>_<cycle>.json`
- `journal`: append-only NDJSON at `Saved/Flight/Schema/ui_edits/schema_edits.jl`

### Snapshot TODOs

- [ ] Add a replay tool that rebuilds table state from `schema_edits.jl` into `latest` snapshots.
- [ ] Add a retention policy for `history/` snapshots (for example, keep last N per table + prune older files).
- [ ] Add a restore flow that promotes a selected history snapshot back to `latest` and re-runs manifest export.

## What This Enables

- Fast UI iteration for tooling without UMG or bespoke Slate widget classes.
- A single source of truth for compile diagnostics that can be viewed in editor-time UI.
- A base for list-heavy interfaces (issues, symbols, tasks, behavior rows).

## Next Steps

1. Implement snapshot replay from `schema_edits.jl` and cover recovery behavior in automation.
2. Implement history retention/prune policy and verify deterministic file lifecycle in tests.
3. Add two-way symbol adapters (`@spawn_rate` -> `SimParams` writeback).
4. Add a `repeat(@rows) { ... }` directive for row templates.
5. Add schema-backed typed cell renderers (number/bool/enum widgets).
6. Add parser/lowerer integration so VEX behavior scripts can emit a paired UI manifest.
7. Add automation tests for section nesting + include composition + table updates in mounted orchestrator context.
