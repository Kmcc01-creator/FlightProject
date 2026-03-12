# FlightProject TODOs

This file is the shared backlog for current cross-cutting TODOs.

Use it for items that should remain visible across architecture, code review, and implementation planning without being tied to one dated session note.

For naming and split rules, see [README.md](README.md).

## Area Files

- [TODO.VexVerse.md](TODO.VexVerse.md): VEX compiler, Verse runtime, backend selection, compile policy, and execution commit TODOs.
- [TODO.Orchestration.md](TODO.Orchestration.md): world visibility, binding evidence, plan truthfulness, and registration-model TODOs.
- [TODO.Startup.md](TODO.Startup.md): startup policy, `GameMode` thinning, bootstrap/orchestration sequencing, and startup test-depth TODOs.
- [TODO.Testing.md](TODO.Testing.md): validation topology, regression coverage, and review-to-test follow-through TODOs.
- [TODO.Docs.md](TODO.Docs.md): documentation maintenance, status-labeling, and canonical-reference hygiene TODOs.

## Current Cross-Cutting Themes

- Execution truth should stay aligned across compile artifacts, orchestration reports, and real runtime commit.
- Startup policy should remain explicit, testable, and observable without spreading reusable setup logic back into `GameMode`.
- TODOs that grow beyond a few bullets should move into area files instead of expanding this shared index.

## Split Candidates

- If orchestration items continue growing, move them into `TODO.Orchestration.md`.
- If VEX and Verse backend work keeps dominating the backlog, move that slice into `TODO.VexVerse.md` or separate `TODO.Vex.md` and `TODO.Verse.md`.
