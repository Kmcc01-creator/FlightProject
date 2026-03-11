# Dev Notes

This directory contains dated engineering notes and one-off technical writeups.

These are useful for context and historical reasoning, but they are not canonical starting points.

## Status

Treat this folder as `Historical / Reference`.

Use these notes when you need:

- dated migration context
- older local design snapshots
- specialized onboarding context that is still useful

Prefer these current docs first:

- [../Architecture/Overview.md](../Architecture/Overview.md)
- [../Architecture/CurrentProjectVision.md](../Architecture/CurrentProjectVision.md)
- [../Workflow/CurrentFocus.md](../Workflow/CurrentFocus.md)
- [../Workflow/CurrentBuild.md](../Workflow/CurrentBuild.md)
- [../Workflow/TestingValidationPlan.md](../Workflow/TestingValidationPlan.md)

## File Guide

- [001_SystemArchitecture_Dec2025.md](001_SystemArchitecture_Dec2025.md)
  Historical snapshot of the earlier swarm/data-flow model.
- [002_LinuxOptimization_Dec2025.md](002_LinuxOptimization_Dec2025.md)
  Historical Linux/rendering optimization note; verify against current `Environment/` docs before acting on it.
- [003_MigrationGapAnalysis_Dec2025.md](003_MigrationGapAnalysis_Dec2025.md)
  Historical migration-gap record; useful for tracing earlier documentation drift.
- [004_RustToUnreal_Guide.md](004_RustToUnreal_Guide.md)
  Still useful as niche onboarding/reference material for Rust-oriented reasoning.
- [Testing.md](Testing.md)
  Testing reference with some dated status notes; prefer current workflow/build docs for live suite state.

## Usage

Read these when:

- a current plan refers back to an older engineering decision
- you need migration context
- you want a dated snapshot of earlier assumptions

Prefer `Docs/README.md`, `Architecture/`, and `Workflow/` for current direction.
