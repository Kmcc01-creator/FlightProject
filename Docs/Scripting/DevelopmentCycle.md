# Development Cycle: Python → Blueprint → C++

This document describes the iterative development workflow that maximizes design time while minimizing friction.

## The Iteration Ladder

New features climb from rapid prototyping to optimized implementation:

```
┌─────────────────────────────────────────────────────────────────┐
│  DESIGN PHASE (Minutes)                                         │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Python + CSV                                            │   │
│  │  - Tweak parameters in CSV, hot-reload                   │   │
│  │  - Run Python scripts to test spawn patterns             │   │
│  │  - Zero compilation, instant feedback                    │   │
│  └─────────────────────────────────────────────────────────┘   │
│                            ↓                                    │
│  GAMEPLAY PHASE (Seconds to compile)                            │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Blueprints                                              │   │
│  │  - Wire up events and UI                                 │   │
│  │  - Prototype new behaviors quickly                       │   │
│  │  - Visual debugging with breakpoints                     │   │
│  └─────────────────────────────────────────────────────────┘   │
│                            ↓                                    │
│  OPTIMIZATION PHASE (Minutes to compile)                        │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  C++                                                     │   │
│  │  - Move hot paths to native code                         │   │
│  │  - Implement Mass processors for scale                   │   │
│  │  - Expose new functionality back to Python/BP            │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## When to Use Each Layer

### Python (The Assistant)

**Use for:**
- Testing spawn configurations before committing to code
- Bulk asset operations (rename, move, configure)
- Scene setup automation
- Data validation and preprocessing
- One-off experiments

**Example: Testing swarm density**
```python
# Content/Python/FlightProject/SwarmExperiment.py
import unreal

def test_swarm_densities():
    """Spawn swarms with varying densities to find visual sweet spot."""
    densities = [50, 100, 200, 500]
    for count in densities:
        # Call exposed C++ function
        unreal.FlightMassSpawning.spawn_test_swarm(
            count=count,
            path_id="TestPath_01",
            speed=1500.0
        )
        unreal.log(f"Spawned {count} entities - check visual density")
        # Wait for user input before next test
```

### Blueprints (The Game)

**Use for:**
- Event handling (OnBeginPlay, OnOverlap, Input)
- UI logic and widget binding
- Connecting C++ systems together
- Rapid gameplay iteration
- Designer-facing configuration

**Blueprint Rules:**
1. **Glue, don't calculate** - Complex math belongs in C++
2. **Events, not ticks** - Avoid Tick in BP; use events or timers
3. **Thin wrappers** - BP should call C++ functions, not reimplement them

**Example: Swarm spawn trigger**
```
Event BeginPlay
    → Get SwarmSpawnerSubsystem
    → Call SpawnInitialSwarm(Config: DA_DefaultSwarm)
    → Bind OnSwarmDepleted → Handle UI Update
```

### C++ (The Engine)

**Use for:**
- Performance-critical code (Mass processors, physics)
- Core systems that rarely change
- Complex algorithms
- Network replication logic
- Base classes for Blueprint extension

**C++ Rules:**
1. **Expose generously** - Use `BlueprintCallable`, `BlueprintType`
2. **Data-drive defaults** - Read from Data Assets, not hardcoded values
3. **Document intent** - Comments for "why", not "what"

## The Promotion Path

When a prototype proves valuable, promote it up the stack:

### Stage 1: Python Experiment
```python
# Quick test: Does path-following with banking look good?
def test_banking():
    for entity in get_swarm_entities():
        velocity = entity.get_velocity()
        bank_angle = calculate_bank(velocity)
        entity.set_rotation(bank_angle)
```

### Stage 2: Blueprint Prototype
Once the concept works, create a BP component:
- `BP_SwarmBankingComponent`
- Uses Timeline for smooth interpolation
- Tweakable curve assets

### Stage 3: C++ Implementation
When scaling to 10,000 entities:
```cpp
// UFlightBankingProcessor : UMassProcessor
void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
    {
        // Vectorized banking calculation for all entities
    });
}
```

## Practical Workflow

### Daily Development Loop

```
1. MORNING: Review CSV configs, tweak parameters
   └── No compilation needed

2. ITERATION: Test in editor with Python scripts
   └── Instant feedback

3. GAMEPLAY: Wire up in Blueprints as needed
   └── Quick recompile (~5 seconds)

4. END OF DAY: Promote proven patterns to C++
   └── Full rebuild if needed (~2-5 minutes)
```

### Decision Tree

```
Is it a one-off task?
├── YES → Python script
└── NO → Is it gameplay logic?
          ├── YES → Does it need 60fps with 1000+ entities?
          │         ├── YES → C++ (Mass Processor)
          │         └── NO → Blueprint
          └── NO → Is it editor tooling?
                   ├── YES → Python + Editor Utility Widget
                   └── NO → C++ subsystem
```

## Integration Points

### C++ → Blueprint
```cpp
UFUNCTION(BlueprintCallable, Category = "Flight")
void SpawnSwarm(UMassEntityConfigAsset* Config, int32 Count);

UFUNCTION(BlueprintImplementableEvent, Category = "Flight")
void OnSwarmDepleted();  // BP implements the response
```

### C++ → Python
```cpp
// Any BlueprintCallable function is automatically available in Python
UFUNCTION(BlueprintCallable, Category = "Python Tools")
static void RebuildNavGraph();
```

```python
unreal.FlightNavGraphSubsystem.rebuild_nav_graph()
```

### Blueprint → Python
Via Editor Utility Widgets:
1. Create `EUW_FlightTools` (Editor Utility Widget Blueprint)
2. Add Button: "Spawn Test Swarm"
3. OnClick → Execute Python Script node
4. Code: `import FlightProject.SwarmSetup; FlightProject.SwarmSetup.run()`

## Anti-Patterns

| Don't | Do Instead |
|-------|------------|
| Complex math in Blueprints | C++ function, expose to BP |
| Tick-based BP logic for many actors | Mass Processor |
| Hardcoded spawn counts in C++ | CSV or Data Asset |
| Manual asset setup repeated daily | Python automation script |
| Recompiling C++ to tweak a number | Expose to Data Asset or CSV |
