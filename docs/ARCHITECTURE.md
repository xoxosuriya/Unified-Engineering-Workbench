# System Architecture: Unified Engineering Workbench

**PASS Science Works — R&D Internship Assessment 2026–2027**  
**Candidate:** Suriya Prakash S (RA2311047010144)  
**Institution:** SRM Institute of Science and Technology, Kattankulathur, Chennai, India  
**Repository:** [https://github.com/xoxosuriya/Unified-Engineering-Workbench](https://github.com/xoxosuriya/Unified-Engineering-Workbench)  
**Live Application:** [https://xoxosuriya.github.io/Unified-Engineering-Workbench/](https://xoxosuriya.github.io/Unified-Engineering-Workbench/)  

---

## 1. Architectural Overview & Evolution

The Unified Engineering Workbench (UEW) enforces a clean, modular, multi-tier computational architecture that strictly decouples numerical calculation from presentation and transport protocols:

```
+-----------------------------------------------------------------------------------------+
|                                    PRESENTATION TIER                                    |
|   Static Web Client (HTML5 / Vanilla ES6 / WebGL / Three.js r128 / Chart.js v4.4.7)   |
|   - 3D Viewports: Multi-plane voxel slicing (X/Y/Z), voxel gap control, camera presets  |
|   - Interactive Views: 3D Thermal FVM, 1D FEA Rod, STL Geometry, ANSYS Points, Hardware |
|   - Hosting: GitHub Pages (https://xoxosuriya.github.io/Unified-Engineering-Workbench/) |
+--------------------------------------------+--------------------------------------------+
                                             | HTTPS / JSON REST API + CORS Preflight
+--------------------------------------------v--------------------------------------------+
|                                      TRANSPORT TIER                                     |
|   Cross-Platform C++17 Non-Blocking REST Server                                         |
|   - Windows: WinSock2 transport + desktop system tray launcher (Launch.exe)             |
|   - Linux / Cloud: POSIX BSD sockets, SIGINT/SIGTERM handling, PORT env binding         |
|   - Security: Origin allowlist, Host validation, Session tokens, Concurrency guard      |
|   - Cloud Hosting: Render Docker Web Service (0.0.0.0:$PORT)                            |
+--------------------------------------------+--------------------------------------------+
                                             | In-Memory C++ Function Dispatch
+--------------------------------------------v--------------------------------------------+
|                                   NUMERICAL COMPUTING CORE                              |
|                                                                                         |
|  [Round 1 Historical Models]               [Round 2 High-Performance 3D FVM Engine]     |
|  - 1D Linear Galerkin FEA (Thomas TDMA)    - Cell-Centred Finite Volume Discretization  |
|  - ASCII STL Parser & Boundary Integrator  - Model B: Analytical Cube ([0, 0.1]^3 m^3)  |
|  - ANSYS CSV Nodal Temperature Validator   - Model C: 3D Finned Heat Sink Solid (52% V) |
|  - Bounded ThreadPool Multicore Benchmark  - Compressed Sparse Row (CSR) Representation |
|  - Embedded Hardware Telemetry Machine     - Profile Cholesky + Reverse Cuthill-McKee   |
|  - Handwritten Backpropagation Engine      - Eigen 3.4.0 Jacobi Preconditioned CG (PCG) |
|                                            - Custom Successive Over-Relaxation (SOR)    |
|                                            - Spectral Radius Estimation & Omega Sweep   |
+-----------------------------------------------------------------------------------------+
```

---

## 2. Round 1 vs. Round 2 Physical Domain Demarcation

| Feature / Domain | Historical Round 1 Implementation | Round 2 High-Performance Extension |
| :--- | :--- | :--- |
| **Physical Geometry** | 1D Uniform Rod ($L=1\text{ m}, A=10^{-4}\text{ m}^2$) | **3D Continuum Solid:** Model B (Cube) & Model C (Finned Sink) |
| **Governing PDE** | $-d/dx(kA\, dT/dx) = q'''A$ | $-\nabla \cdot (k \nabla T) = q'''$ in $\Omega \subset \mathbb{R}^3$ |
| **Boundary Conditions**| Dirichlet $T(0)=100^\circ\text{C}, T(L)=20^\circ\text{C}$ | Fixed $T_b=100^\circ\text{C}$, insulated adiabatic, Robin convection $h(T-T_\infty)$ |
| **Discretization** | 1D 2-Node Linear Finite Element Method | **Cell-Centred 3D Finite Volume Method (FVM)** |
| **Linear System Stencil**| Tridiagonal Matrix ($B=1, NNZ=3M-2$) | **7-Point Symmetric Stencil** ($B=N^2, NNZ \le 7M$) |
| **Numerical Solvers** | Direct Thomas Algorithm (TDMA, $O(M)$) | **Profile Cholesky (RCM)**, **Jacobi PCG**, **Custom SOR** |
| **Active DOFs ($M$)** | $M \in [10, 80]$ nodes | **$M \in [520, 27,000]$ 3D continuum cells** |
| **Linear Algebra** | Native scalar arrays | **Custom CSR Matrix** + **Bundled Eigen 3.4.0** |
| **Runtime Target** | Local Windows desktop WinSock2 | **Cross-Platform:** Windows desktop + Linux POSIX Cloud Service |

---

## 3. Discretization & Mathematical Stencil

For cell $(i, j, k)$ in a structured Cartesian grid with spacing $\Delta x, \Delta y, \Delta z$, integrating the energy equation over control volume $V_P = \Delta x \Delta y \Delta z$:

$$\sum_{f \in \{e, w, n, s, t, b\}} Q_f = q''' V_P$$

Conduction across internal face between cell $P$ and cell $E$:

$$Q_e = -k A_e \frac{T_E - T_P}{\Delta x} = \frac{k \Delta y \Delta z}{\Delta x} (T_P - T_E)$$

Robin convection boundary condition on external fluid-cooled faces ($h = 50\text{ W/(m}^2\cdot\text{K)}, T_\infty = 20^\circ\text{C}$):

$$Q_{\text{conv}} = h A_{\text{face}} (T_P - T_\infty)$$

This yields a symmetric, strictly diagonally dominant, positive-definite linear system $\mathbf{A} \mathbf{T} = \mathbf{b}$, where:
- Diagonal entry: $A_{P,P} = \sum a_{\text{nb}} + h A_{\text{ext}}$
- Off-diagonal entries: $A_{P,\text{nb}} = -k A_f / \Delta x$
- Source vector: $b_P = q''' V_P + h A_{\text{ext}} T_\infty + T_{\text{Dirichlet}} \cdot (2kA_f/\Delta x)$

---

## 4. Sparse Storage & Solver Mechanics

### 4.1 Compressed Sparse Row (CSR) Storage
* `row_ptr`: Array of length $M+1$ (`size_t`, 8 bytes)
* `col_idx`: Array of length $NNZ_A$ (`int`, 4 bytes)
* `values`: Array of length $NNZ_A$ (`double`, 8 bytes)
* Total CSR Memory: $\text{Mem}_A = (M + 1)\times 8 + NNZ_A \times 12\text{ bytes}$. At $M=27,000, NNZ_A=183,600 \implies \mathbf{2.307\text{ MiB}}$.

### 4.2 Profile / Skyline Cholesky Decomposition
* Stores lower envelope under profile pointer array `profile_ptr` of size $M+1$.
* Bandwidth minimized via Reverse Cuthill–McKee (RCM) degree-adjacency reordering.
* Factored in-place: $\mathbf{A} = \mathbf{L} \mathbf{L}^T$.
* At $N=30$ Cube, envelope entries reach $N_{\text{env}} = 13,573,161 \implies \mathbf{103.967\text{ MiB}}$, illustrating catastrophic $O(M^{5/3})$ fill-in.

### 4.3 Preconditioned Conjugate Gradient (PCG)
* Uses diagonal Jacobi preconditioner $\mathbf{M} = \text{diag}(\mathbf{A})$.
* 6 vector buffers ($48M\text{ bytes} \implies 1.236\text{ MiB}$ at $M=27,000$).
* Matrix-free residual contractions: convergence verified to $10^{-8}$ relative tolerance within 108 iterations ($N=30$ Cube).

### 4.4 Successive Over-Relaxation (SOR)
* Direct component-wise forward substitution:
  $$T_i^{(k+1)} = (1 - \omega) T_i^{(k)} + \frac{\omega}{A_{i,i}} \left( b_i - \sum_{j < i} A_{i,j} T_j^{(k+1)} - \sum_{j > i} A_{i,j} T_j^{(k)} \right)$$
* Optimal relaxation parameter $\omega^* = 1.70$ validated against David Young's theoretical optimum $\omega_{\text{opt}} \approx 1.695$.

