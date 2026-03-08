# Next-Gen VEX & Reflective RDG Rendering Architecture

This document formalizes the integration of the FlightProject "Shadow Engine" (Reflection, Functional, Schema) with the GPU Swarm simulation and VEX scripting pipeline.

## 1. Executive Recommendation

**Recommendation: The Typestate Render Graph & Custom RDG Splatting Renderer**

FlightProject has outgrown the imperative constraints of standard Unreal systems. To leverage the C++23 trait-based reflection and monadic functional flows (`TResult`), we will implement a **Custom RDG Render Pass for Swarm Splatting/Volumetrics**.

By moving the VEX compilation and RDG orchestration into a `TPhantomState` pipeline, we transform potential runtime failures into deterministic, schema-backed compile-time or load-time errors. The **Reflective HLSL Bridge** will ensure absolute memory layout parity between C++ and HLSL, eliminating a major category of GPU synchronization bugs.

---

## 2. Detailed Architecture

### 2.1 Unified Pipeline Integration
The architecture centers on the C++ struct (e.g., `FDroidState`) as the Single Source of Truth:
1.  **Reflection**: Parses the C++ struct at compile-time to identify fields and attributes.
2.  **Reflective HLSL Bridge**: Automatically generates HLSL struct/buffer definitions from reflection data.
3.  **Schema**: Defines allowed VEX symbols (e.g., mapping `@position` to reflected `Position`).
4.  **Typestate VEX Pipeline**: Uses `TPhantomState` to guarantee a script is Parsed -> TypeChecked -> Lowered before execution.
5.  **Functional RDG**: Orchestrates passes via `TValidateChain`, proving required resources exist before dispatch.

### 2.2 The Typestate VEX Pipeline
Lifecycle states for VEX scripts:
-   **`State::Unparsed`**: Raw VEX string.
-   **`State::ParsedIR`**: Abstract Syntax Tree (AST).
-   **`State::TypeChecked`**: AST validated against the VEX Schema (symbol detection).
-   **`State::Lowered`**: HLSL/Verse generated with layout hashes.
-   **`State::Bound`**: RDG SRV/UAVs securely mapped to the shader.

### 2.3 Reflective HLSL Bridge
-   **Auto-Generation**: Templates iterate over `TReflectionTraits<T>::Fields` to output HLSL code.
-   **Layout Verification**: A `constexpr` hash of size/alignment is computed on both sides. The CPU verifies this hash during binding.
-   **Migration Safety**: Schema validation detects mismatches between the Manifest and compiled HLSL if the C++ struct changes.

---

## 3. API & Data Model Sketches

### 3.1 Typestate Pipeline
```cpp
namespace Flight::Vex
{
    struct Unparsed {}; struct Parsed {}; struct TypeChecked {}; struct Lowered {};

    struct FVexProgramData {
        FString SourceCode;
        TSharedPtr<FVexAST> AST;
        FString GeneratedHLSL;
        uint32 LayoutHash;
    };

    template<typename... Tags>
    using TVexPipeline = Flight::Functional::TPhantomState<FVexProgramData, Tags...>;

    // Usage:
    auto Compile = Phantom(FVexProgramData{Source})
        .Transition<Parsed>(ParseToAST)
        .Transition<TypeChecked>(ValidateAgainstSchema)
        .Transition<Lowered>(LowerToHLSL);
}
```

### 3.2 Reflective Bridge
```cpp
namespace Flight::Reflection::HLSL
{
    template<CReflectable T>
    FString GenerateStructHLSL(); // Generates "struct FDroidState { ... };"

    template<CReflectable T>
    constexpr uint32 GetLayoutHash(); // Compile-time alignment/size check
}
```

---

## 4. Phased Implementation Backlog

### Phase 1: Reflective Bridge & VEX Pipeline
- [ ] **Task 1.1**: Extend `FlightReflection.h` with `THlslTypeMap` for C++ to HLSL type mapping.
- [ ] **Task 1.2**: Implement `constexpr` layout hashing in reflection traits.
- [ ] **Task 1.3**: Implement the `TVexPipeline` typestate flow in the `SSwarmOrchestrator`.
- [ ] **Task 1.4**: Update Python tooling to generate VEX schema contracts from C++ reflection.

### Phase 2: Functional RDG Orchestration
- [ ] **Task 2.1**: Refactor `TickSimulation` into a `TValidateChain` monadic flow.
- [ ] **Task 2.2**: Integrate Schema checks (CVar/RHI) at the start of the RDG chain.
- [ ] **Task 2.3**: Inject auto-generated HLSL into `FGlobalShaderMap` via virtual shader paths.

### Phase 3: Custom RDG Splatting Renderer
- [ ] **Task 3.1**: Implement `FFlightSwarmSplattingCS` for high-performance point rasterization.
- [ ] **Task 3.2**: Create an `FSceneViewExtension` to inject the splatting pass before Tonemapping.
- [ ] **Task 3.3**: Extend VEX with visual properties (`@color`, `@emissive`) routed to the splatter shader.

---

## 5. Validation Matrix

| Vector | Strategy | Gates & Assertions |
| :--- | :--- | :--- |
| **Correctness** | Python Header vs Vex Schema Validation | CI fails on `@symbol` type mismatch. |
| **Safety** | `TPhantomState` Compile-time check | Cannot call `LowerToHLSL` before `TypeChecked`. |
| **Memory** | `GetLayoutHash()` Runtime Check | `checkf` on binding if CPU/GPU layout diverges. |
| **Performance** | RDG GpuTrace & Unreal Insights | Target: < 2.5ms for 500k entity splatting. |
| **DX** | Live-Coding Orchestrator UI | Instant UI warnings for unknown VEX symbols. |
