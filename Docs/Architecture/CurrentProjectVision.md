# Current Project Vision

This document formalizes the current architectural vision for FlightProject as of March 10, 2026.

It is not a task list.
It is a reference frame for understanding what the project is becoming, what has already landed, and what deeper pattern seems to be repeating across the codebase.

For project organization, see `Docs/Architecture/ProjectOrganization.md`.
For world-scoped coordination, see `Docs/Architecture/OrchestrationSubsystem.md`.
For the VEX mutation-centered compiler frame, see `Docs/Architecture/VexStateMutationSchemaFrame.md`.
For the current direction on composite behaviors as behavior-owned execution graphs, see `Docs/Architecture/BehaviorComposition.md`.
For active implementation work, see `Docs/Workflow/CurrentFocus.md`.

## 1. Short Version

FlightProject is converging on a schema-bound execution architecture.

The project’s center of gravity is not:

- only Unreal gameplay framework
- only ECS or rendering
- only a scripting compiler
- only a collection of runtime subsystems

The center of gravity is this:

- authored intent becomes a typed contract
- the contract becomes a bound executable plan
- the plan is lowered into domain-specific execution
- execution produces observable state and reports

That shape is beginning to repeat everywhere.

## 2. The Core Essence

The current best statement of the project’s essence is:

> FlightProject is a reflective, schema-bound execution system that turns authored intent into legal state transformations across multiple runtimes.

That statement matters because it explains why the same ideas keep reappearing in:

- reflection descriptors
- schema rows
- VEX symbol binding
- backend lowering
- orchestration bindings
- runtime reports

The project is not only "about drones," "about shaders," or "about Verse."
Those are execution surfaces.
The deeper pattern is contract formation, semantic binding, specialization, and observation.

## 3. The Repeatability Paradigm

One useful way to think about the project is as a repeatability paradigm:

1. describe something abstractly
2. bind it against a concrete context
3. preserve its meaning while lowering it
4. execute it in a specialized domain
5. report the result in a form that higher layers can reason about

This pattern appears repeatedly:

- field metadata becomes schema symbols
- schema symbols become bound compiler facts
- bound facts become IR and backend code
- behaviors become cohort bindings
- cohort bindings become world execution plans
- world execution plans become reports and rebuild decisions

That repeatability is likely not accidental.
It is a real architectural trait.

## 4. Self-Similar Trees

The thought experiment of self-similar trees is useful here.

The project increasingly looks like a family of trees where each level has the same basic structure:

```text
Description
    -> Contract
        -> Binding
            -> Lowering
                -> Execution
                    -> Report
```

The tree changes scale, but not shape.

Examples:

### 4.1 Field Scale

- a reflected field is described by traits and attributes
- those attributes form a schema contract
- the contract binds to storage
- storage lowers to offset, accessor, fragment, buffer, or provider
- runtime performs the read/write
- the system can report legality, residency, or layout

### 4.2 Compiler Scale

- source syntax describes an intended state operation
- schema facts form the legality contract
- SchemaIR binds symbol meaning to concrete typed state
- IR lowering specializes it for Verse, SIMD, native, or GPU paths
- execution mutates or observes state
- compile artifacts and diagnostics report what happened

### 4.3 Orchestration Scale

- authored world assets and runtime services describe available capability
- orchestration forms world contracts
- behaviors bind to cohorts and participants
- plans lower into execution steps
- runtime executes those steps through domain systems
- the orchestration report summarizes availability, bindings, and gaps

This is why "tree" is a useful metaphor.
The project keeps re-expressing the same structural relation at different levels of abstraction.

## 5. Functional-Programming Resonance

The project is not being forced into a purely academic functional style.
Still, some functional ideas are clearly useful.

The most relevant one is not "everything must be immutable."
It is this:

- each stage should enrich context without destroying semantic identity

That is why bind-like operations are showing up naturally.

Examples:

- reflection binds field identity to type metadata
- schema orchestration binds logical symbols to policy and storage facts
- SchemaIR binds syntax to legal semantic state operations
- lowering binds semantic operations to execution domains
- orchestration binds behaviors to world participation context

That is monad-adjacent in the practical engineering sense:

- carry context explicitly
- compose transformations stage by stage
- avoid hidden ambient assumptions
- keep side effects at the edges where execution becomes real

This is a better fit for FlightProject than shallow OO indirection or hardcoded lookup chains.

## 6. State Mutation As The Concrete Anchor

The project’s abstractions become more legible when state mutation is treated as the anchor.

This does not reduce the project to "just writes."
It clarifies what each layer is responsible for:

- source expresses intended state change
- schema determines whether that state change is legal
- binder determines what the change refers to
- lowering determines how the change is realized
- runtime determines when the change becomes authoritative

This is why the recent VEX work matters.
The move toward `FVexTypeSchema`, `FVexSchemaOrchestrator`, and SchemaIR is not an isolated compiler refactor.
It is the compiler becoming consistent with the deeper project architecture.

## 7. What Has Actually Landed

Several important pieces of this vision are already real.

### 7.1 Reflection To Schema Direction

- reflected fields can already describe VEX symbols and backend naming
- `FVexTypeSchema` now carries logical symbols, type identity, and layout metadata
- `FVexSchemaOrchestrator` centralizes schema construction and merge policy

That means the project has already moved beyond hardcoded field tables as the only way to express compiler-visible state.

### 7.2 Schema-Bound Compiler Direction

- VEX is no longer only a parser-to-lowering pipeline
- the compiler now has a first SchemaIR binder slice
- schema-backed `CompileVex(...)` can project symbol definitions from schema and preserve backend bindings
- backend capability profiles can now evaluate schema-bound programs and emit explicit backend-selection reports

That means symbol meaning is starting to be resolved from contracts rather than rediscovered from ad hoc backend code.

Near-term follow-through:

- the next slice should use that reported backend selection to gate actual runtime dispatch
- `CompileVex(...)` and `ExecuteBehavior(...)` should honor the chosen backend when it is both legal and executable today
- when runtime must downgrade to another backend, the report should say why

### 7.3 Orchestration Direction

- orchestration now exists as the world-scoped coordination surface
- world visibility, bindings, plans, and reports are becoming first-class
- project direction already treats orchestration as the coordination nexus rather than a second simulation engine

That means the world itself is being described through contracts and bindings, not only through imperative startup code.

## 8. What This Vision Suggests

If this self-similar-tree reading is correct, then future work should generally prefer:

- explicit contract surfaces over hidden assumptions
- binding stages over direct hardcoded execution
- schema generation over duplicated lookup logic
- semantic lowering boundaries over backend-local reinterpretation
- reporting surfaces that preserve the contract/execution relationship

And it should generally avoid:

- embedding meaning only in runtime codepaths
- using one-off special cases as the main integration mechanism
- re-resolving legality separately in each backend
- making orchestration or compiler layers infer facts that schemas could state directly

## 9. The Practical Design Heuristic

When designing a new system in FlightProject, ask:

1. What is the described thing?
2. What contract defines its legal meaning?
3. What binding resolves it into a concrete context?
4. What lowering specializes it for execution?
5. What report or artifact makes the result visible to higher layers?

If those answers are unclear, the design is probably still too implicit.

## 10. A Candidate Unifying Motto

This is a useful working motto for the current direction:

> Describe clearly. Bind semantically. Lower late. Execute explicitly. Report meaningfully.

That captures the repeating pattern more precisely than a technology-specific slogan would.

## 11. Near-Term Implication

The near-term implication is straightforward:

- keep pushing VEX toward explicit SchemaIR
- keep pushing reflection toward richer contract metadata
- keep pushing orchestration toward first-class bindings and reports
- keep reducing hardcoded runtime assumptions that bypass schemas and contracts

If the project stays aligned with that direction, the different domains should continue to become more coherent rather than more entangled.

## 12. Final Framing

The project’s essence is not one subsystem.
It is the recurring relation between description, contract, binding, lowering, execution, and report.

That relation is appearing in enough places now that it should be treated as intentional architecture, not coincidence.

The self-similar tree idea is therefore useful not as a poetic metaphor, but as a practical design test:

- if a new system fits the pattern cleanly, it is probably aligned with the project
- if it bypasses the pattern with hidden assumptions, it is probably creating future friction
