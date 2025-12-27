# M2 Framework Integration - Future Exploration

## Overview

This document captures exploratory work on integrating the m2 compiler infrastructure framework (`~/m2`) with Unreal Engine tooling development. The m2 framework provides abstractions for AST manipulation, pattern matching, and temporal effects that could potentially support custom debugging utilities for UE development.

**Status**: Exploratory / Thought Exercise
**Related Project**: `~/m2` (Rust compiler infrastructure)

## M2 Framework Summary

M2 is built on the principle: **"All computation is compilation."**

### Core Primitives (Sacred Kernel)

| Primitive | Purpose |
|-----------|---------|
| **Shape** | Grammar/IR definition (what can be expressed) |
| **Operation** | Semantic pass (what expressions mean) |
| **Effect** | Impure context (what resources are needed) |
| **Route** | Entry point (where compilation begins) |

### Key Abstractions

#### Tree Trait
Self-similar recursive structures with iterative traversal (stack-safe to depth 10,000+):
```rust
pub trait Tree: Sized {
    fn children(&self) -> Vec<&Self>;
    fn map_children<F>(self, f: F) -> Self;
    fn take_children(self) -> (Self, Vec<Self>);  // Iterative support
    fn with_children(self, children: Vec<Self>) -> Self;

    // Derived
    fn transform<F>(self, f: F) -> Self;  // Bottom-up
    fn rewrite<F>(self, f: F) -> Self;    // Top-down fixpoint
}
```

#### Pattern + Strategy (Stratego-inspired)
Separates *what* matches from *how* to traverse:
```rust
// Pattern: Fn(&T) -> Option<O>
pub trait Pattern<T> {
    type Output;
    fn apply(&self, input: &T) -> Option<Self::Output>;
}

// Strategy primitives: Id, Fail, Rule, Seq, Choice, Try, Repeat
// Traversal: All, One, Some_, TopDown, BottomUp, Innermost, Outermost
```

#### Temporal System
Unified async model for I/O, GPU, audio:
```rust
Moment     // Logical time point
Timeline   // Clock with sparse completion tracking
Fence      // Completion marker
Pending<T> // Future with temporal metadata
Deferred   // Lambda awaiting context (enables chaining without cloning)
```

#### Tiered Regex Engine
Pattern complexity analysis with automatic tier selection:
- **Tier 1**: Literals, alternations (SIMD-accelerated, O(n))
- **Tier 2**: DFA-compatible patterns (O(n*m))
- **Tier 3**: Full NFA with backrefs/lookaround (budgeted to prevent ReDoS)

## Potential Unreal Applications

### 1. UHT Macro Analyzer

Model Unreal Header Tool macros as a Tree for static analysis:

```rust
enum UhtNode {
    Class { name: String, specifiers: Vec<Specifier> },
    Property { name: String, type_: Type, meta: Vec<Meta> },
    Function { name: String, params: Vec<Param>, meta: Vec<Meta> },
}

impl Tree for UhtNode { /* ... */ }

// Example: Find BlueprintCallable functions missing Category
let missing_category = rule(|n: &UhtNode| {
    if let UhtNode::Function { meta, .. } = n {
        let has_callable = meta.iter().any(|m| m.name == "BlueprintCallable");
        let has_category = meta.iter().any(|m| m.name == "Category");
        if has_callable && !has_category {
            return Some(n.clone());
        }
    }
    None
});

let violations = collect_matches(&module_tree, &missing_category);
```

**Use Cases**:
- Lint for missing metadata specifiers
- Detect inconsistent property flags
- Analyze class hierarchies for anti-patterns

### 2. Blueprint-C++ Correlation Debugging

Map the temporal system to Unreal's async execution:

```rust
struct BlueprintExecution {
    timeline: Timeline,
    // Each BP node execution = tick()
    // Native call completion = fence
}

// Track latent action lifecycle
struct LatentActionTracker {
    timeline: Timeline,
    pending_actions: HashMap<FenceHandle, LatentActionInfo>,
}
```

**Parallels**:
- `Moment` ↔ Frame number / logical tick
- `Fence` ↔ `FLatentActionInfo` completion
- `Timeline` ↔ `FTimerManager` handle space
- `Deferred` ↔ Blueprint latent node continuation

### 3. Mass Entity Debugging (FlightProject)

Apply bounded search abstractions to swarm behavior analysis:

```rust
enum SwarmState { Seeking, Avoiding, Converging, Idle }

impl Tree for SwarmState { /* state transitions as children */ }

// Debug: find agents stuck in pathological states
let stuck_seekers = rule(|s: &SwarmState| {
    match s {
        SwarmState::Seeking { duration } if *duration > MAX_SEEK_TIME => Some(s.clone()),
        _ => None
    }
});

// Use bounded search for convergence analysis
let convergence_check = innermost(swarm_rule).apply(swarm_state);
```

**Relevant m2 modules**:
- `optics/bounded_search.rs` - Resource-bounded search with tiered escalation
- `optics/self_healing.rs` - Fixpoint convergence for self-repair
- `systems/shard/` - Actor-shard system for concurrent graph mutation

### 4. Reflection Metadata Pattern Matching

Use tiered regex for parsing UPROPERTY specifier strings:

```rust
// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight", meta=(ClampMin=0.0))
// This is Tier 2 DFA-compatible
let specifier_pattern = TieredMatcher::new(r"(\w+)\s*(?:=\s*([^,)]+))?");
```

## Architecture Considerations

### Rust ↔ C++ Interop

Options for integration:
1. **FFI Layer**: Expose m2 functions via `extern "C"` for UE module consumption
2. **Code Generation**: Use m2 to generate C++ source that UE compiles
3. **Standalone Tool**: Build separate analysis binary that reads UE source

### Data Flow

```
UE Source (.h/.cpp)
       │
       ▼ Parse (regex tier or custom)
    m2 Tree IR
       │
       ▼ Pattern + Strategy
   Analysis Results
       │
       ▼ Report / Transform
  Diagnostics / Modified Source
```

## File References

### m2 Core Modules
- `~/m2/core/src/` - Shape, Operation, Effect, Route traits
- `~/m2/optics/src/tree.rs` - Tree trait with iterative traversal
- `~/m2/optics/src/pattern.rs` - Pattern matching with combinators
- `~/m2/optics/src/strategy.rs` - Stratego-inspired traversal strategies
- `~/m2/systems/src/temporal.rs` - Moment, Timeline, Fence, Deferred

### m2 Regex Engine
- `~/m2/proto/regex/src/tiered.rs` - Auto-selecting tiered matcher
- `~/m2/proto/regex/src/pattern.rs` - Pattern complexity analysis
- `~/m2/proto/regex/src/tier3_compiler.rs` - Full regex compiler

### m2 Search/AI
- `~/m2/optics/src/bounded_search.rs` - Tiered escalation, behavior trees
- `~/m2/optics/src/self_healing.rs` - Convergence toward invariants

## Next Steps

1. **Prototype UHT Parser**: Implement `Tree` for a subset of UHT constructs
2. **Benchmark Pattern Matching**: Compare m2 pattern/strategy vs manual traversal
3. **Temporal Mapping**: Formalize correspondence between m2 temporal and UE async
4. **Mass Integration**: Apply bounded search to FlightProject swarm diagnostics

## Related Documentation

- `../Architecture/` - FlightProject architecture docs
- `~/m2/docs/design/critical/` - m2 critical planning documents
- `~/m2/README.md` - m2 framework overview
