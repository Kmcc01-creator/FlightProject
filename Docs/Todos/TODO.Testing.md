# TODO: Testing

This file tracks TODOs for validation topology, automation depth, and test-surface clarity.

## Current TODOs

### 1. Phased Runner Namespace Drift

Priority: High  
Status: Active  
Owner/Surface: test runner topology and script filters

Update the phased/headless validation filters so the recommended execution path targets current test namespaces rather than older prefixes that no longer match the active suite layout.

Relevant surfaces: [TestingValidationPlan.md](../Workflow/TestingValidationPlan.md), [run_tests_phased.sh](/home/kelly/Unreal/Projects/FlightProject/Scripts/run_tests_phased.sh), [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp), [FlightReactiveTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightReactiveTests.cpp), [FlightVerseAssemblerTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVerseAssemblerTests.cpp), [FlightVexSimdTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexSimdTests.cpp).

### 2. VEX Schema Fixture Deduplication

Priority: Medium  
Status: Planned  
Owner/Surface: VEX schema-provider test scaffolding

Replace hand-built fragment-backed schema providers in tests with a shared source-side builder or reusable helper so schema layout logic is not duplicated across production and automation fixtures.

Relevant surfaces: [FlightVexGeneralTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexGeneralTests.cpp), [FlightVexSchemaIrTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexSchemaIrTests.cpp), [FlightNavigationContracts.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Navigation/FlightNavigationContracts.cpp), [FlightVexSymbolRegistry.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/FlightVexSymbolRegistry.cpp).

### 3. Complex vs Generated Suite Taxonomy

Priority: Medium  
Status: Active  
Owner/Surface: automation suite organization

Clarify the difference between truly generated suites and fixed multi-case dispatch suites so phase ordering, suite names, and future test additions reflect actual topology instead of overloading the word `complex`.

Relevant surfaces: [TestingValidationPlan.md](../Workflow/TestingValidationPlan.md), [FlightGenerativeManifestTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightGenerativeManifestTests.cpp), [FlightSchemaDrivenTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightSchemaDrivenTests.cpp), [FlightVexVerticalSliceTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerticalSliceTests.cpp), [FlightReflectionComplexAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightReflectionComplexAutomationTests.cpp).

### 4. GPU-Required Coverage Usefulness

Priority: Medium  
Status: Monitoring  
Owner/Surface: GPU-required automation lane

Keep moving GPU lanes beyond initialization/discovery evidence toward meaningful behavioral assertions once the renderer pipeline is ready.

Relevant surfaces: [CurrentBuild.md](../Workflow/CurrentBuild.md), [TestingValidationPlan.md](../Workflow/TestingValidationPlan.md), [FlightGpuPerceptionTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightGpuPerceptionTests.cpp).

### 5. Phased Validation Maintenance

Priority: Medium  
Status: Active  
Owner/Surface: test runner topology and script defaults

Keep the complex/generated-first validation structure current as new automation groups are added so test ordering remains intentional rather than accidental.

Relevant surfaces: [TestingValidationPlan.md](../Workflow/TestingValidationPlan.md), [CurrentBuild.md](../Workflow/CurrentBuild.md), [run_tests_phased.sh](/home/kelly/Unreal/Projects/FlightProject/Scripts/run_tests_phased.sh).

### 6. Review-to-Test Follow-Through

Priority: Medium  
Status: Active  
Owner/Surface: review backlog to regression-coverage loop

When reviews identify parity gaps or regressions, capture the missing validation here until a concrete automation test or assertion lands.

Relevant surfaces: [Docs/Reviews/README.md](../Reviews/README.md), [TODOS.md](TODOS.md).

## Exit Condition

- major architecture seams have focused regression coverage
- phased/headless/GPU validation paths reflect the intended test topology
- review findings turn into explicit validation work instead of staying informal

## Completed / Archived

- Completed (2026-03-12): startup sequencing coverage now includes a dedicated `DefaultSandboxStartupSequenceWorldFixture` in [FlightStartupSequencingAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightStartupSequencingAutomationTests.cpp) that provides a GameInstance-backed automation world, drives the real `AFlightGameMode` startup path, and asserts post-spawn swarm/orchestration/cohort state.
- Completed (2026-03-12): scripting policy-context adoption and GPU terminal-commit regression coverage now exist in [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp), including `FlightProject.Functional.Verse.CompilePolicyIntegration` through the scripting compile surface and `FlightProject.Functional.Verse.GpuTerminalCommit` for submit-vs-terminal GPU commit truth. Verified with targeted headless coverage plus Phase 3 and Phase 4 reruns.
- Completed (2026-03-12): compile-policy and GPU-commitment regression coverage now exists in [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp) and [FlightVexGeneralTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexGeneralTests.cpp), including explicit policy-driven backend selection and GPU-policy generated-only commitment truth.
- Completed (2026-03-12): runtime dispatch gating now resolves struct, bulk, and direct execution lanes through shared backend-selection helpers in [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp), with focused regression coverage in [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp) and [FlightVexGeneralTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexGeneralTests.cpp). Verified with targeted headless Verse/VEX coverage plus Phase 3 and Phase 4 reruns.
- Completed (2026-03-12): current phased failure triage cleared the previously failing `FlightProject.Integration.Startup.Sequencing.OrchestrationRebuildAdvancesPlan`, `FlightProject.Unit.Vex.ControlFlow`, and `FlightProject.Unit.Vex.Parsing` surfaces after source fixes in [FlightOrchestrationSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Orchestration/FlightOrchestrationSubsystem.cpp), [FlightVexIr.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/FlightVexIr.cpp), and [VexParser.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Vex/Frontend/VexParser.cpp). Verified with a targeted headless run through [run_tests_headless.sh](/home/kelly/Unreal/Projects/FlightProject/Scripts/run_tests_headless.sh).
- Completed (2026-03-12): manifest asset validation depth now checks package existence, asset-registry presence, and loadability in [FlightGenerativeManifestTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightGenerativeManifestTests.cpp) instead of stopping at soft-path validity.
- Completed (2026-03-12): startup test utility extraction moved reusable world/subsystem/property/Mass helpers into [FlightTestUtils.h](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightTestUtils.h), and [FlightStartupSequencingAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightStartupSequencingAutomationTests.cpp) now consumes that shared helper surface.
- Completed (2026-03-12): logging-suite ownership cleanup removed overlapping buffer-bridge, const-ref reflective logging, and functional-result assertions so [FlightLoggingAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightLoggingAutomationTests.cpp) owns logger primitives, [FlightLoggingBoundaryAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightLoggingBoundaryAutomationTests.cpp) owns runtime bridge behavior, and [FlightFoundationAutomationTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightFoundationAutomationTests.cpp) keeps only composed scenarios. Verified with targeted logging-suite coverage and a Phase 3 rerun.
- Completed (2026-03-12): backend commit validation now proves direct runtime commitment explicitly in [UFlightVerseSubsystem.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Verse/UFlightVerseSubsystem.cpp) and [FlightVexVerseTests.cpp](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp), including a dedicated [FlightProject.Functional.Verse.BackendCommitTruth](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp) test and committed-backend assertions in [FlightProject.Functional.Vex.CompileArtifactReport.Core](/home/kelly/Unreal/Projects/FlightProject/Source/FlightProject/Private/Tests/FlightVexVerseTests.cpp). Verified with targeted headless Verse/VEX coverage.
