# Functional Differentials & Derivative Pipes

This document explores the intersection of functional programming (via `FlightFunctional.h`) and differential equations within the FlightProject simulation framework.

## 1. Philosophical Foundation: State as a Function of Time

In our architecture, we treat the state of an entity (drone, wind particle, projectile) not as a collection of variables, but as a **transformation pipeline**. 

An Ordinary Differential Equation (ODE):
$$\frac{dy}{dt} = f(y, t)$$

Is represented in our C++ layer as a **Pipe Operation**:
```cpp
// A single integration step is a transformation of state over DeltaTime
auto NextState = CurrentState | Pipe(Integrate(dt));
```

---

## 2. The "Derivative Pipe" Pattern

To prevent logical errors (like adding a Force to a Position), we utilize **Phantom Types** to encode the order of the derivative in the type system.

### Derivative Orders
| Order | Physical Meaning | Typestate Tag |
| :--- | :--- | :--- |
| **0th** | Position ($r$) | `Order::Position` |
| **1st** | Velocity ($v$) | `Order::Velocity` |
| **2nd** | Acceleration ($a$) | `Order::Acceleration` |
| **3rd** | Jerk ($j$) | `Order::Jerk` |

### Type-Safe Force Application
In this paradigm, a **Force** is an operator that can *only* bind to a 2nd Order state. 

```cpp
// Functional Representation
auto Result = State<Order::Position>(Pos)
    .Derive<Order::Velocity>()      // Valid: r -> dr/dt
    .Derive<Order::Acceleration>()  // Valid: v -> dv/dt
    .ApplyForce(WindForce)          // Valid: Force applies to 2nd order
    .Integrate(dt)                  // Returns 1st order (Velocity)
    .Integrate(dt);                 // Returns 0th order (Position)
```

---

## 3. SPH as a Functional PDE Solver

Our 5-pass GPU pipeline is functionally a **discrete solver for the Navier-Stokes equations**. By viewing it through `FlightFunctional`, we can identify each pass as a specific operator:

1.  **Spatial Operator ($\nabla$)**: The Spatial Binning pass provides the gradient context.
2.  **Density Mapping ($\rho = \int W$)**: The Density pass is a functional convolution of the position field.
3.  **Pressure Operator ($-\frac{\nabla P}{\rho}$)**: The Force pass applies the pressure gradient to the 2nd-order state (Acceleration).

---

## 4. Speculative Horizons

### Jerk-Limited Simulation (3rd Order)
To achieve "organic" movement, we can extend the pipe to 3rd-order **Jerk**.
- **The Rule**: Accelerations cannot change instantly.
- **The Math**: $j = \frac{da}{dt}$.
- **Computation**: We add a "Jerk Blackboard" where SPH forces are compared against the *previous* frame's forces to calculate a smooth transition.

### Automatic Differentiation (AD)
By reflecting our math traits, we can implement functional AD.
- **Goal**: Allow the swarm to "know" the derivative of its own cost function.
- **Impact**: Drones can perform gradient descent on the potential field in real-time, finding the "path of least resistance" through the Gauntlet.

### Computational Agile-ness
Because every step is a decoupled pipe, we can swap solvers (Euler vs. Runge-Kutta) or physics models (Standard SPH vs. Divergence-Free SPH) simply by changing the function bound to the pipe, with zero changes to the underlying Mass ECS or RDG infrastructure.
