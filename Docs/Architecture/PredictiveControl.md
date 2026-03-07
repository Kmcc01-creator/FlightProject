# Predictive Control & Functional Trajectories

This document outlines the transition from Reactive SPH to Model Predictive Control (MPC) utilizing functional programming abstractions in FlightProject.

## 1. Concept: The Internal Model

In reactive simulation, an entity responds to the *instantaneous* gradient:
$$a_t = -\nabla P(r_t)$$

In predictive control, an entity responds to the *projected* cost over a horizon $H$:
$$u_t = \arg\min_{u} \sum_{k=0}^{H} J(r_{t+k}, u_{t+k})$$
Where $J$ is a cost function (distance to goal, proximity to obstacles).

---

## 2. Functional Abstractions

### TProjectedState<Order, Horizon>
A wrapper that extends our `TState` to include temporal depth.
```cpp
auto Future = CurrentState 
    | Pipe(Project(Model, dt, Horizon(5))) // Project 5 steps into the future
    | Pipe(EvaluateCost(TargetArea));       // Returns TResult<float>
```

### The "Trajectory Pipe"
A high-order function that maps a control sequence to a state sequence.
- **Input**: `Sequence<ControlInput>`
- **Output**: `Sequence<State>`
- **Optimization**: We use **Automatic Differentiation (AD)** traits to find the derivative of the Trajectory Pipe, allowing drones to "steer" their future.

## 3. Fused Swarm Intelligence

Total control intent is a weighted fusion of two functional layers:

### The "Reactive" Layer (Reptilian Brain)
- **Primary Goals**: Collision avoidance, Swarm separation (SPH).
- **Temporal Horizon**: $H = 0$.
- **Responsiveness**: Immediate.

### The "Predictive" Layer (Frontal Cortex)
- **Primary Goals**: Path smoothing, Energy efficiency, Swarm formation cohesion.
- **Temporal Horizon**: $H = 10...50$.
- **Responsiveness**: Anticipatory.

### Fusion Logic (The Subsumption Pipe)
Using the `Fuse` combinator in `FlightFunctional.h`, the swarm modulates its intelligence based on **Environmental Risk** ($\alpha$):
$$u_{fused} = \alpha(\text{risk}) \cdot u_{reactive} + (1 - \alpha(\text{risk})) \cdot u_{predictive}$$

- **High Risk**: Drones prioritize local safety (reactive).
- **Low Risk**: Drones prioritize long-term trajectory efficiency (predictive).

---

## 4. GPU Implementation: Parallel Rollouts

With 500,000 entities, we cannot solve a complex optimization for every drone on the CPU.

### Pass 5: The "Rollout" Pass
1. **Input**: `FDroidState` (Current).
2. **Logic**: Each thread performs $K$ mini-steps of Euler integration.
3. **Collision Prediction**: If a drone's "Future Self" hits an SDF obstacle, it writes a "Virtual Threat" back to the **Force Blackboard**.
4. **Result**: The "Real" drone feels a force from its own future collision, causing it to steer away *before* it actually reaches the obstacle.

---

## 4. Testing Framework for Predictive Control

To verify "Predictive Intelligence," we will implement a **Counter-Factual Test**:
1. **Scenario**: A drone is on a direct collision course with a thin wall at high speed.
2. **Reactive Result**: Drone hits the wall because the SPH pressure only builds up at the moment of impact.
3. **Predictive Result**: Drone begins its banking maneuver 50 units *before* the wall, as its "Horizon Pipe" detected the collision in the projection pass.

## 5. Algorithmic Roadmap
- **Step 1**: Implement `FSwarmGlobalCommand::PredictionHorizon` reactive parameter.
- **Step 2**: Add `Pass 5: Future Rollout` to the HLSL pipeline.
- **Step 3**: Integrate `TProjectedState` into `FlightFunctional.h`.
