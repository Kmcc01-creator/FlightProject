# Field-Theoretic Stylization & The F-Buffer Paradigm

This document specifies the artistic and mathematical extension of the SCSL engine, moving beyond Physically Based Rendering (PBR) toward an opinionated, stylized visual framework driven by **Field Theory** and **Non-Linear Interpolation (LERP)**.

## 1. The F-Buffer (Field Buffer)
Rather than a traditional G-Buffer storing physical material properties, the SCSL engine resolves simulation data into an **F-Buffer**. This buffer stores the topological "potential" of the scene.

| Channel | Data | Description |
| :--- | :--- | :--- |
| **$\nabla S$** | Structure Gradient | Represents the geometric "Normal" derived from the SDF. |
| **$\nabla L$** | Light Gradient | Represents the dominant direction of radiance flow in the Lattice. |
| **$|L|$** | Light Magnitude | The total accumulated irradiance at a given spatial point. |
| **$C_d$** | Cloud Density | The local volumetric density, used for stylized depth falloff. |
| **$\Phi$** | Phase Field | A VEX-definable scalar used for hatching, halftones, or temporal jitter. |

---

## 2. Shading as Parameterized LERP
In this paradigm, we reject the standard Phong/Material model. Lighting is treated as a **mapping function** that interpolates between abstract color states.

### 2.1 The LERP Layer
Using the VEX pipe operator, designers author "Flow Alignments" which act as LERP parameters:
```vex
// Toon Shading Logic
float alignment = dot(gradient(@Structure), gradient(@Light));
float threshold = alignment | smoothstep(0.4, 0.45);

// The engine LERPs between artistic themes rather than calculating bounces
@RenderColor = lerp(@Theme.Shadow, @Theme.Highlit, threshold);
```

### 2.2 Stylized Depth & Atmosphere
Instead of linear fog, we use the **Cloud Field** to modulate the complexity of the rendered signal. Distant entities sample the density field to LERP toward simplified "silhouettes," mimicking background art in traditional animation.

---

## 3. Gradient Descent: The Unified Law
SCSL collapses the distinction between **Logic (AI)** and **Visuals (Shading)** into a single mathematical law: sliding along the gradient of a field.

*   **Motion**: Swarms navigate by calculating the negative gradient of the Structure SDF (Avoidance) and the positive gradient of the Cloud Pheromones (Attraction).
*   **Lighting**: Shading is calculated by the interaction of the Structure gradient and the Radiance gradient.

---

## 4. VEX Post-Processing & Memory Folding
The functional core allows us to "fold" multiple LERP layers into a single GPU pass. 

1.  **Algebraic Pruning**: The VEX compiler identifies if a LERP branch is multiplied by 0.0 (e.g., an entity completely occluded by a Structure) and prunes the entire branch from the HLSL.
2.  **Texture-Sampled Ramps**: Complex anime-style color transitions are stored as 1D LERP ramps, sampled using the field alignment scalar as a UV coordinate.

---

## 5. Architectural Goals
*   **Bypassing "Sameyness"**: Avoid the visual convergence inherent in standard UE5 PBR.
*   **Artistic Opinion**: Provide a framework where the "math" of the simulation is the "brush" of the artist.
*   **Performance**: Replace expensive multi-bounce GI with $O(1)$ field lookups via the Light Lattice.
