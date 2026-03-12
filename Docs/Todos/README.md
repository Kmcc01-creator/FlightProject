# Todos

This directory is the explicit backlog surface for FlightProject documentation-adjacent TODO tracking.

Use it when a task should stay visible across sessions without being buried inside architecture prose, implementation plans, or dated build notes.

## Purpose

- keep active TODOs in one discoverable place
- separate durable architecture intent from unfinished execution work
- give reviews and investigations a clean landing zone for follow-up items
- make it easier to split cross-cutting backlog from domain-specific backlog

## File Roles

- [TODOS.md](TODOS.md): top-level backlog and current cross-cutting TODO index
- `TODO.<Area>.md`: area-specific backlog when one topic grows large enough to deserve its own page

## Active Area Files

- [TODO.VexVerse.md](TODO.VexVerse.md)
- [TODO.Orchestration.md](TODO.Orchestration.md)
- [TODO.Startup.md](TODO.Startup.md)
- [TODO.Testing.md](TODO.Testing.md)
- [TODO.Docs.md](TODO.Docs.md)

## Naming Convention

Use these names by default:

- `TODOS.md` for the shared backlog
- `TODO.<Area>.md` for one domain or system
- `TODO.<Area>.<Subarea>.md` only when a topic is large enough that one more split clearly improves readability

Recommended area labels:

- `Orchestration`
- `Startup`
- `Verse`
- `Vex`
- `Navigation`
- `Rendering`
- `Docs`
- `Testing`

## Content Rules

- keep items actionable and specific
- prefer linking to canonical docs or source files instead of repeating large explanations
- keep dated build and test outcomes in `Docs/Workflow/CurrentBuild.md`, not here
- keep active milestone framing in `Docs/Workflow/CurrentFocus.md`, not here
- move stale or completed items out instead of letting backlog files become historical journals

## Suggested Metadata

- `Priority`: `High`, `Medium`, or `Low`
- `Status`: `Active`, `Planned`, `Blocked`, or `Monitoring`
- `Owner/Surface`: the subsystem, doc surface, or workflow area that primarily owns the work

## Completed / Archived Convention

- move resolved items out of `Current TODOs` instead of leaving them with stale `Active` or `Planned` status
- keep recently completed items in a `Completed / Archived` section only if their closure is useful for future reference
- keep completed entries short:
  - what was finished
  - when it was closed
  - where the final implementation or canonical note lives
- archive items when they are no longer active planning inputs but still worth preserving as breadcrumbs
- delete or collapse entries that add no future reference value

Recommended completion markers:

- `Completed (YYYY-MM-DD)` for work that landed
- `Archived (YYYY-MM-DD)` for items intentionally removed from the active backlog without direct implementation

## Suggested Item Shape

Use a short entry with enough context to act on it:

- what is missing or incorrect
- why it matters
- where the relevant code or doc lives

If a TODO needs more than a short bullet, it probably wants its own `TODO.<Area>.md` file.
