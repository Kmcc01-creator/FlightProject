# Macrokid Behavioral Systems → Unreal Integration

## Overview

This document synthesizes patterns from the macrokid framework (`~/devMe/macrokid/`) for integration with Unreal Engine's StateTree + Mass Entity architecture. The goal is to port emergent complexity mechanisms from macrokid_learning and neon_void to drive AI behavioral systems in FlightProject.

**Status**: Design / Future Implementation
**Source Projects**:
- `~/devMe/macrokid/macrokid_learning` - Evolutionary learning with epigenetic memory
- `~/devMe/macrokid/neon_void` - ECS simulation with emergent Flora/Fauna/Creatures

## Architectural Parallels

| macrokid/neon_void | Unreal Mass + StateTree |
|-------------------|-------------------------|
| `ComponentStore<T>` (sparse-set) | `FMassFragment` (archetype storage) |
| `Automaton` states (Dormant→Active→Decaying) | StateTree state transitions |
| `AutomataRule` (Grazer, Seeker, Wanderer) | StateTree Tasks (FollowPath, PursueTarget) |
| `SpatialField` (flora pheromones) | `FFlightSteeringIntent` + perception |
| `InfluenceMap` (threat, safety) | MassAI perception queries |
| `Motion::Composite` | Layered StateTree (Parallel nodes) |
| `MentalState` (fear, curiosity, aggression) | Fragment for behavioral modulation |
| `EpigeneticMemory` (hot spots, protection) | Learning layer for adaptive behavior |
| `Plated` trait (tree traversal) | m2 Tree/Strategy (see M2FrameworkIntegration.md) |

## Source Framework Summaries

### macrokid_learning

Evolutionary learning infrastructure with:

- **Evolvable trait**: `mutate()`, `evaluate()`, `fitness()` interface
- **EpigeneticMemory**: Tracks success/failure at structural locations
  - Hot spots: High failure rate → aggressive mutation
  - Stable spots: High success rate → protected from mutation
  - Crystallization: >95% success → permanently protected
- **Pattern Detection**: Omega cycles (infinite loops), behavioral pathologies
- **MutationGuidance**: Adaptive rates based on observation history
- **Population Management**: Hall of Fame (diversity), Ensemble (specialists)

Key abstractions:
```rust
pub trait EpigeneticMemory {
    type Loc: Location;
    fn record_success(&mut self, loc: Self::Loc);
    fn record_failure(&mut self, loc: Self::Loc);
    fn hot_spots(&self, threshold: f32) -> Vec<Self::Loc>;
    fn stable_spots(&self, threshold: f32) -> Vec<Self::Loc>;
}
```

### neon_void

Three-layer emergence simulation:

**Layer 1: Flora** (10,000+ Physarum particles)
- Chemotaxis: sense → decide → move → deposit
- Writes pheromone trails to SpatialField
- Creates emergent transport networks without pathfinding

**Layer 2: Fauna** (100-500 automata entities)
- Rule-based: `Grazer`, `EnergySeeker`, `Wanderer`, `Predator`
- State machine: Dormant → Awakening → Active → Weakened → Decaying → Dead
- Reads/consumes from flora field

**Layer 3: Creatures** (flocking colonies)
- Boid rules: Cohere, Separate, Align, SeekAnomaly, AvoidVoid
- Golden ratio reproduction triggers
- Spatial grid for O(1) neighbor queries

**Shared Infrastructure**:
- `Substrate`: Unified grid topology + field layers
- `InfluenceMap`: 2D spatial heatmaps (threat, safety, resource)
- `Motion` composition: Orbit + Brownian + Attracted + Composite
- `MentalState`: fear, curiosity, aggression, alertness

## Key Patterns to Port

### 1. Field-Mediated Coordination

neon_void's Flora/Fauna coupling via `SpatialField` maps to intent-based architecture:

```
neon_void:                           Unreal:
┌─────────────┐                     ┌─────────────────────┐
│ Flora       │──writes──▶          │ StateTree Tasks     │──writes──▶
│ (particles) │         ┌───────┐   │ (FollowPath, etc.)  │         ┌──────────────────────┐
└─────────────┘         │ Field │   └─────────────────────┘         │ FFlightSteeringIntent│
                        └───────┘                                    └──────────────────────┘
┌─────────────┐              ▲      ┌─────────────────────┐              ▲
│ Fauna       │──reads───────┘      │ SteeringExecutor    │──reads───────┘
│ (automata)  │                     │ (Mass Processor)    │
└─────────────┘                     └─────────────────────┘
```

**Benefit**: StateTree Tasks write *intent*, Processors *execute* - decoupling decision from physics.

### 2. Mental State → Behavior Modulation

neon_void's `MentalState` becomes a Mass fragment influencing StateTree conditions:

```cpp
USTRUCT()
struct FFlightMentalState : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    float Fear;        // Flee priority multiplier

    UPROPERTY()
    float Curiosity;   // Exploration drive

    UPROPERTY()
    float Aggression;  // Engagement drive

    UPROPERTY()
    float Alertness;   // Perception range scale

    // Derived helpers
    bool ShouldFlee() const { return Fear > 0.7f; }
    bool ShouldEngage() const { return Aggression > 0.5f && Fear < 0.3f; }
    bool IsCalm() const { return Fear < 0.2f && Aggression < 0.3f; }
};
```

StateTree conditions read mental state:
```cpp
UCLASS()
class UFlightCondition_HighFear : public UMassStateTreeConditionBase
{
    UPROPERTY(EditAnywhere)
    float FearThreshold = 0.7f;

    virtual bool TestCondition(...) override
    {
        // Read FFlightMentalState from entity
        return MentalState.Fear > FearThreshold;
    }
};
```

### 3. Motion Composition → StateTree Parallel

neon_void's `Motion::Composite` pattern becomes StateTree parallel execution:

```
neon_void:                              StateTree:
Motion::Composite([                     [Parallel: Always Running]
    Orbit { center, radius },               ├── Task: FollowPath (primary intent)
    Brownian { intensity },                 ├── Task: AvoidCollisions (reactive)
    Attracted { target, strength },         ├── Task: MaintainAltitude (constraint)
])                                          └── Task: AdjustForWind (environmental)
```

Each parallel task contributes to `FFlightSteeringIntent`:
- Primary task sets base velocity
- Reactive tasks add avoidance vectors
- Constraint tasks clamp altitude/speed

### 4. Epigenetic Memory → Learning Layer

macrokid_learning's `EpigeneticMemory` becomes a shared fragment for adaptive behavior:

```cpp
USTRUCT()
struct FFlightBehaviorMemory : public FMassSharedFragment
{
    GENERATED_BODY()

    // Per-behavior success/failure tracking (epigenetic pattern)
    TMap<FName, int32> Successes;
    TMap<FName, int32> Failures;

    void RecordSuccess(FName BehaviorId);
    void RecordFailure(FName BehaviorId);

    float SuccessRate(FName BehaviorId) const
    {
        int32 S = Successes.FindRef(BehaviorId);
        int32 F = Failures.FindRef(BehaviorId);
        return (S + F > 0) ? float(S) / float(S + F) : 0.5f;
    }

    // Hot spots: behaviors failing often → try alternatives
    TArray<FName> HotSpots(float FailureThreshold = 0.7f) const;

    // Stable spots: behaviors succeeding → protect/prefer
    TArray<FName> StableSpots(float SuccessThreshold = 0.8f) const;

    // Crystallized: >95% success over 10+ attempts → always use
    TSet<FName> Crystallized;
};
```

**Emergent specialization**: Entities that fail at interception become scouts; entities that succeed at patrol stay on patrol.

### 5. Three-Layer Emergence → Fleet Composition

neon_void's Flora/Fauna/Creatures layers map to fleet roles:

| neon_void Layer | FlightProject Role | Behavior |
|-----------------|-------------------|----------|
| Flora (10k particles, trails) | Scout Swarm | Perception, marking, early warning |
| Fauna (100-500 automata) | Patrol Units | Area control, consumption of threats |
| Creatures (flocking, reproduction) | Strike Groups | Coordinated engagement, formation |

**Interaction Pattern**:
1. Scouts detect and mark threats (write to perception field)
2. Patrol units respond to marked areas (read perception, write engagement)
3. Strike groups form when threat density exceeds threshold

## Proposed Fragment Architecture

### Layer 1: Atomic Intent

What the entity wants NOW (single frame):

```cpp
USTRUCT()
struct FFlightSteeringIntent : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FVector DesiredVelocity;    // Direction + magnitude

    UPROPERTY()
    FVector DesiredFacing;      // Where to look

    UPROPERTY()
    float UrgencyScale;         // 0=relaxed smoothing, 1=immediate response

    UPROPERTY()
    EFlightManeuver Maneuver;   // Cruise, Evasive, Aggressive, Formation
};

UENUM()
enum class EFlightManeuver : uint8
{
    Cruise,      // Energy-efficient, smooth
    Evasive,     // Sharp turns, speed variation
    Aggressive,  // Direct intercept, max thrust
    Formation,   // Maintain relative position
};
```

### Layer 2: Goal State

What the entity is trying to achieve:

```cpp
USTRUCT()
struct FFlightGoal : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    EFlightGoalType Goal;    // Patrol, Intercept, Return, Survive, Escort

    UPROPERTY()
    FGuid TargetId;          // Path GUID, entity handle, or location marker

    UPROPERTY()
    float Priority;          // For conflict resolution

    UPROPERTY()
    float TimeInGoal;        // Duration tracking for stuck detection
};

UENUM()
enum class EFlightGoalType : uint8
{
    Patrol,       // Follow assigned path
    Intercept,    // Pursue threat
    Return,       // Navigate to base
    Survive,      // Self-preservation
    Escort,       // Protect target entity
    Investigate,  // Check marked location
};
```

### Layer 3: Mental/Emotional State

Modulates behavior selection:

```cpp
USTRUCT()
struct FFlightMentalState : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    float Fear;           // 0-1, flee priority

    UPROPERTY()
    float Curiosity;      // 0-1, exploration drive

    UPROPERTY()
    float Aggression;     // 0-1, engagement drive

    UPROPERTY()
    float Alertness;      // 0-1, perception multiplier

    // State transitions (from neon_void Automaton)
    void TakeDamage(float Amount) { Fear += Amount * 0.3f; Aggression -= Amount * 0.1f; }
    void DetectThreat() { Alertness = FMath::Min(1.0f, Alertness + 0.2f); }
    void Decay(float DeltaTime) { Fear *= (1.0f - DeltaTime * 0.1f); }
};
```

### Layer 4: Automaton State (from neon_void)

Lifecycle management:

```cpp
USTRUCT()
struct FFlightAutomatonState : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    EFlightLifecycleState State;  // Dormant, Awakening, Active, Weakened, Returning

    UPROPERTY()
    float Energy;                  // Fuel/health resource

    UPROPERTY()
    float StateBlend;              // 0-1, for smooth visual transitions

    UPROPERTY()
    float TimeInState;
};

UENUM()
enum class EFlightLifecycleState : uint8
{
    Dormant,     // Inactive, minimal processing
    Awakening,   // Transitioning to active
    Active,      // Full behavior execution
    Weakened,    // Low energy, reduced capability
    Returning,   // Heading to resupply
    Disabled,    // Damaged, awaiting recovery
};
```

### Layer 5: Behavior Memory (Epigenetic)

Adaptive learning (shared across entity type):

```cpp
USTRUCT()
struct FFlightBehaviorMemory : public FMassSharedFragment
{
    GENERATED_BODY()

    // Success/failure counts per behavior
    TMap<FName, int32> Successes;
    TMap<FName, int32> Failures;

    // Crystallized behaviors (>95% success, protected)
    TSet<FName> Crystallized;

    // Recent behavior sequence (for omega cycle detection)
    TArray<FName> RecentBehaviors;
    static constexpr int32 MaxRecent = 20;

    void RecordOutcome(FName Behavior, bool bSuccess);
    float GetSuccessRate(FName Behavior) const;
    bool IsCrystallized(FName Behavior) const;
    bool DetectOmegaCycle(int32 MaxCycleLength = 4) const;

    // Mutation guidance (from macrokid_learning)
    TArray<FName> GetHotSpots(float FailureThreshold = 0.7f) const;
    TArray<FName> GetStableSpots(float SuccessThreshold = 0.8f) const;
};
```

## StateTree Integration

### Task Design Pattern

```cpp
UCLASS()
class UFlightTask_FollowPath : public UMassStateTreeTaskBase
{
    UPROPERTY(EditAnywhere, Category = "Input")
    FStateTreeStructRef PathIdRef;

    UPROPERTY(EditAnywhere, Category = "Parameters")
    float CruiseSpeed = 1500.f;

protected:
    virtual EStateTreeRunStatus EnterState(...) override
    {
        // Initialize path sampling
        // Record behavior start for memory
        BehaviorMemory->RecordBehaviorStart(TEXT("FollowPath"));
        return EStateTreeRunStatus::Running;
    }

    virtual EStateTreeRunStatus Tick(...) override
    {
        // Sample path → compute desired velocity
        // Write to FFlightSteeringIntent
        SteeringIntent.DesiredVelocity = PathTangent * CruiseSpeed;
        SteeringIntent.Maneuver = EFlightManeuver::Cruise;

        if (PathComplete)
        {
            BehaviorMemory->RecordOutcome(TEXT("FollowPath"), true);
            return EStateTreeRunStatus::Succeeded;
        }
        return EStateTreeRunStatus::Running;
    }

    virtual void ExitState(...) override
    {
        // If interrupted (not succeeded), record as context-dependent
    }
};
```

### Condition Design Pattern

```cpp
UCLASS()
class UFlightCondition_BehaviorFailing : public UMassStateTreeConditionBase
{
    UPROPERTY(EditAnywhere)
    FName BehaviorToCheck;

    UPROPERTY(EditAnywhere)
    float FailureThreshold = 0.6f;

    virtual bool TestCondition(...) override
    {
        // If this behavior has high failure rate, try alternative
        float SuccessRate = BehaviorMemory->GetSuccessRate(BehaviorToCheck);
        return SuccessRate < (1.0f - FailureThreshold);
    }
};
```

## Migration Path

| Phase | Focus | macrokid Source | Deliverable |
|-------|-------|-----------------|-------------|
| 1 | Intent Fragment | `Motion` composition | `FFlightSteeringIntent` + executor processor |
| 2 | Mental State | `MentalState` struct | `FFlightMentalState` + decay/trigger logic |
| 3 | Automaton Lifecycle | `Automaton` states | `FFlightAutomatonState` + state machine |
| 4 | StateTree Tasks | `AutomataRule` enum | Task library (FollowPath, PursueTarget, etc.) |
| 5 | Behavior Memory | `EpigeneticMemory` | `FFlightBehaviorMemory` shared fragment |
| 6 | Fleet Roles | Flora/Fauna/Creatures | Scout/Patrol/Strike StateTree assets |

## File References

### macrokid_learning
- `~/devMe/macrokid/macrokid_learning/src/memory/mod.rs` - EpigeneticMemory trait
- `~/devMe/macrokid/macrokid_learning/src/memory/ghost.rs` - Full implementation
- `~/devMe/macrokid/macrokid_learning/src/evolvable.rs` - Evolvable trait
- `~/devMe/macrokid/macrokid_learning/src/location.rs` - Location abstractions

### neon_void
- `~/devMe/macrokid/neon_void/src/ecs/components.rs` - MentalState, Automaton
- `~/devMe/macrokid/neon_void/src/fauna.rs` - AutomataRule, state transitions
- `~/devMe/macrokid/neon_void/src/flora.rs` - Physarum particles, field writing
- `~/devMe/macrokid/neon_void/src/motion.rs` - Motion composition
- `~/devMe/macrokid/neon_void/src/field/mod.rs` - SpatialField
- `~/devMe/macrokid/neon_void/src/creatures.rs` - Flocking rules

### Related Documentation
- `M2FrameworkIntegration.md` - Tree/Pattern abstractions for tooling
- `../Architecture/MassECS.md` - Current Mass Entity architecture
- `../Future/` - Roadmap items

## Open Questions

1. **Perception**: Use MassAI perception or custom spatial queries?
2. **Communication**: Explicit messaging vs field-mediated (pheromone style)?
3. **Learning Scope**: Per-entity memory vs per-archetype shared memory?
4. **Golden Ratio**: Apply neon_void's φ-based formation triggers?
