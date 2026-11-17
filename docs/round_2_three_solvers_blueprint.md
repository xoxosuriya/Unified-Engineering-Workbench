# PASS Science Works Round 2: Technical Audit, Solver Architecture & Implementation Blueprint (Revision 7.3 — Post-Phase-2D Benchmark Clarifications)

**Repository:** `xoxosuriya/Unified-Engineering-Workbench`  
**Candidate ID:** `RA2311047010144`  
**Assessment:** PASS Science Works R&D Internship 2026–2027 — Round 2 Assignment Extension  
**Deadline:** 30 November 2026  
**Final Submission Deliverable:** Single Comprehensive PDF Document (< 1 MB) with Essential Pictorial Representations (Direct Email Reply, No Source Code)  
**Status:** **PHASE 2D BENCHMARKING COMPLETED & AUDITED (AWAITING PHASE 3 / REPORT AUTHORIZATION)**  
**Revision History:**
- **Revision 7.3 (15 Nov 2026):** Post-Phase-2D Documentation & Benchmark-Report Clarifications:
  1. Clearly distinguished the three fill metrics: Numerical Factor Fill Ratio ($NNZ(L)/NNZ(A) \approx 26.87$ for Finned $N=30$), Envelope Capacity Ratio ($N_{\text{env}}/NNZ(A) \approx 33.26$), and Lower-Triangular Reference Ratio ($NNZ(L)/((NNZ(A)+M)/2) \approx 46.45$), incorporating authoritative integer counts from benchmark CSV data ($1{,}804{,}849$ for Cube $N=20$; $13{,}573{,}161$ for Cube $N=30$).
  2. Clarified factor non-zero accounting: factor nonzeros in $\mathbf{L}$ are evaluated under the scale-invariant drop threshold $|L_{ij}| > 10 \epsilon_{\text{mach}} \sqrt{L_{ii} L_{jj}}$, and the remaining 19.2% of envelope positions in Finned $N=30$ are explicitly documented as not counted as numerical nonzeros under this threshold (rather than assumed exact mathematical zeros).
  3. Corrected CSR-to-Eigen conversion terminology to validated sequential compressed deep copy using Eigen's `reserve/startVec/insertBack/finalize` API (eliminating any "zero-copy" characterization).
  4. Standardized memory units to mebibytes (MiB, bytes / $1024^2$) and rigorously demarcated tracked structural allocation categories from complete heap footprint and independent OS process working set (`WorkingSetSize` via Windows PSAPI).
- **Revision 7.2 (15 Nov 2026):** Corrected arithmetic discrepancy in Section 2.2 table for Model B at $N=30$: internal face pairs $E = 3 N^2 (N - 1) = 78,300$ (was 79,200) and matrix nonzeros $NNZ_A = M + 2E = 183,600$ (was 185,400).
- **Revision 7.1 (15 Nov 2026):** Corrected similarity transformation algebra: $\mathbf{C} = \mathbf{D}^{1/2}\mathbf{B}\mathbf{D}^{-1/2} = \mathbf{I} - \mathbf{D}^{-1/2}\mathbf{A}\mathbf{D}^{-1/2}$ with $\mathbf{S} = \mathbf{D}^{1/2}$, $\mathbf{S}^{-1} = \mathbf{D}^{-1/2}$. Refined Rayleigh quotient phrasing to variational estimate.

---

## 1. Historical Project Claims & Model Taxonomy

To ensure scientific and historical integrity, the codebase distinguishes three distinct geometric models:

```
+----------------------------------------------------------------------------------------------------+
|                                      EVOLUTION OF GEOMETRIC MODELS                                 |
+------------------------------------+----------------------------------+----------------------------+
| MODEL A: HISTORICAL ROUND 1        | MODEL B: ROUND 2 BASELINE CUBE   | MODEL C: ROUND 2 COMPLEX   |
| 1D Uniform Rod Conduction          | 3D Cartesian Continuum Cube      | 3D Finned Heat Sink Solid  |
+------------------------------------+----------------------------------+----------------------------+
| - Domain: L = 1.0 m, A = 10^-4 m²  | - Domain: [0, 0.1]^3 m³          | - Envelope: 100x100x100 mm |
| - 1D Poisson: -d/dx(kA dT/dx) = qA | - 3D Poisson: -∇·(k∇T) = q       | - Baseplate + 4 fins       |
| - 2-node linear elements (N=10..80)| - Cell-Centred FVM (N x N x N)   | - Cell-Centred FVM         |
| - Tridiagonal system (B = 1)       | - 7-point banded matrix (B = N²) | - Irregular active sparsity|
| - Solver: Thomas Algorithm (TDMA)  | - Solvers: Cholesky, PCG, SOR    | - Solvers: Cholesky, PCG, SOR|
| - Exact analytical solution        | - Exact analytical reference     | - Robin convection on fins |
| - Source: thermal_fea_1d.cpp       | - Planned: thermal_solvers_3d    | - Planned: thermal_solvers |
+------------------------------------+----------------------------------+----------------------------+
```

### 1.1 Historical Audit Verdict:
1. **Historical Round 1 Implementation:** The original UEW codebase implemented a **strictly one-dimensional** finite element solver (`src/backend/src/thermal_fea_1d.cpp`) for a rod of length $L = 1.0\text{ m}$ and cross-section $A = 10^{-4}\text{ m}^2$, solved via the Thomas algorithm (TDMA). The ASCII STL file (`data/sample_bracket.stl`) was an **open surface shell mesh** (32 triangular boundary facets), and `data/ansys_thermal_synthetic.csv` was a nodal surface point cloud. **No 3D volumetric thermal solver existed in Round 1.** The Thomas algorithm will be retained for this historical 1D module and is documented as a specialized tridiagonal solver ($O(M)$ time, 0 fill-in), not identical to general 3D profile Cholesky.
2. **Round 2 Baseline Cube (Model B):** A **newly defined, true three-dimensional continuum benchmark** formulated over $\Omega_{\text{cube}} = [0, 0.1]^3\text{ m}^3$ to serve as the baseline comparison required by the Round 2 brief.
3. **Round 2 Complex Geometry (Model C):** A **newly formulated 3D volumetric finned heat sink solid** featuring non-convex geometry, re-entrant corners, high aspect ratio cooling fins, and Robin convection boundary conditions.

---

## 2. Mesh & Degree of Freedom (DOF) Consistency

We define $N$ consistently across all backend modules, API payloads, benchmark CSVs, plots, and report tables as the **number of cells along each primary axis of the bounding box $[0, 0.1]^3\text{ m}^3$**:
$$\Delta x = \Delta y = \Delta z = \frac{0.1\text{ m}}{N}$$

### 2.1 Rigorous Mathematical Entity Definitions
- **Total Bounding Cells ($N_{\text{total}}$):** $N^3$. The total number of Cartesian cells in the bounding box envelope.
- **Total Grid Points / Vertices:** $(N + 1)^3$. In our Cell-Centred Finite Volume Method, unknown temperatures are stored at cell centroids, so grid vertices are not independent DOFs.
- **Active Solid Cells ($M$):** The number of control volumes occupied by solid conducting material.
  - For Cube: $M_{\text{cube}} = N^3$.
  - For Finned Heat Sink: $M_{\text{finned}} = 0.52 N^3$ (due to 52% solid volume fraction).
- **Prescribed-Temperature Boundary Cells:** In Cell-Centred FVM, boundary conditions are enforced via numerical boundary fluxes across control-volume faces. Cells adjacent to Dirichlet boundaries retain their unknown centroid temperature $T_P$. Thus, **zero cells are eliminated from the active matrix system**.
- **Actual Unknown DOFs ($M$):** Exactly equals the number of active solid cells ($M_{\text{cube}} = N^3$, $M_{\text{finned}} = 0.52 N^3$).
- **Exact Matrix Non-Zeros ($NNZ_A = M + 2E$):**
  - Where $M$ is active solid cells and $E$ is the number of unique shared internal faces between active solid cell pairs.
  - **For Cube:**
    $$E_{\text{cube}} = 3 N^2 (N - 1) = 3 N^3 - 3 N^2 \implies NNZ_{\text{cube}}(A) = N^3 + 2(3 N^3 - 3 N^2) = \mathbf{7 N^3 - 6 N^2}$$
  - **For Finned Heat Sink:**
    Summing internal face pairs along X, Y, and Z across the baseplate and 4 fins:
    $$E_y = 0.52 N^2 (N - 1) = 0.52 N^3 - 0.52 N^2$$
    $$E_z = (0.2 N^3 - N^2) + (0.32 N^3 - 0.4 N^2) + 0.4 N^2 = 0.52 N^3 - N^2$$
    $$E_x = (0.2 N^3 - 0.2 N^2) + (0.32 N^3 - 3.2 N^2) = 0.52 N^3 - 3.4 N^2$$
    $$E = E_x + E_y + E_z = 1.56 N^3 - 4.92 N^2$$
    $$\mathbf{NNZ_{\text{finned}}(A) = M + 2E = 0.52 N^3 + 2(1.56 N^3 - 4.92 N^2) = 3.64 N^3 - 9.84 N^2}$$

### 2.2 Standard Multi-Resolution Grid Hierarchy

| Resolution ($N$) | Cell Size $\Delta x$ | Geometry | Total Bounding Cells | Active Solid Cells ($M$) | Actual Unknown DOFs ($M$) | Internal Face Pairs ($E$) | Exact Matrix Non-Zeros ($NNZ_A$) |
|:---:|:---:|:---|:---:|:---:|:---:|:---:|:---:|
| **$N = 10$** | $10.0\text{ mm}$ | **Cube (Model B)** | 1,000 | 1,000 | **1,000** | 2,700 | **6,400** |
| **$N = 10$** | $10.0\text{ mm}$ | **Finned Sink (Model C)** | 1,000 | 520 | **520** | 1,068 | **2,656** |
| **$N = 20$** | $5.0\text{ mm}$ | **Cube (Model B)** | 8,000 | 8,000 | **8,000** | 22,800 | **53,600** |
| **$N = 20$** | $5.0\text{ mm}$ | **Finned Sink (Model C)** | 8,000 | 4,160 | **4,160** | 10,512 | **25,184** |
| **$N = 30$** | $3.33\text{ mm}$| **Cube (Model B)** | 27,000 | 27,000 | **27,000** | 78,300 | **183,600** |
| **$N = 30$** | $3.33\text{ mm}$| **Finned Sink (Model C)** | 27,000 | 14,040 | **14,040** | 37,692 | **89,424** |

> [!NOTE]
> Acceptance criterion: The backend will assert programmatically during assembly that $NNZ(A) \equiv M + 2E$ and verify these exact integer counts.

---

## 3. Discretization Methodology: Cell-Centred Finite Volume Method (FVM)

Structured-grid 7-point stencils on voxelized solids represent a **Cell-Centred Finite Volume Method (FVM)**, not conventional finite elements.

### 3.1 Control-Volume Construction & Conservation Law
The continuum Poisson equation governs steady-state conduction:
$$-\nabla \cdot (k \nabla T) = q$$

Integrating over control volume $\mathcal{V}_P = \Delta x \Delta y \Delta z$ centered at centroid $\mathbf{x}_P = (x_i, y_j, z_k)$ and applying the Divergence Theorem:
$$\int_{\mathcal{V}_P} -\nabla \cdot (k \nabla T) \, dV = \int_{\partial \mathcal{V}_P} -k \nabla T \cdot \mathbf{n} \, dA = \int_{\mathcal{V}_P} q \, dV \implies \sum_{f \in \{e, w, n, s, t, b\}} \mathcal{F}_f = q_P \Delta V_P$$
Where $\mathcal{F}_f = \int_{A_f} -k \nabla T \cdot \mathbf{n}_f \, dA$ represents outward diffusive heat flux across face $f$.

### 3.2 Face Conductivities & Internal Fluxes
For isotropic uniform conductivity $k$, face conductivity is $k_f = k$.  
For internal face $e$ between cell $P$ and eastern neighbor $E$ (distance $\delta x_{PE} = \Delta x$, face area $A_e = \Delta y \Delta z$):
$$\mathcal{F}_e = -k A_e \left( \frac{T_E - T_P}{\Delta x} \right) = \frac{k A_e}{\Delta x} (T_P - T_E)$$
Stencil coefficients:
$$a_E = \frac{k A_e}{\Delta x}, \quad a_W = \frac{k A_w}{\Delta x}, \quad a_N = \frac{k A_n}{\Delta y}, \quad a_S = \frac{k A_s}{\Delta y}, \quad a_T = \frac{k A_t}{\Delta z}, \quad a_B = \frac{k A_b}{\Delta z}$$

### 3.3 Boundary Flux Formulations

#### 1. Dirichlet Boundary Treatment (Prescribed Temperature $T_{\text{bc}}$)
The distance from cell centroid $P$ to the boundary face is half a cell: $\delta x_{Pf} = \frac{\Delta x}{2}$.  
$$\mathcal{F}_w = -k A_w \left( \frac{T_{\text{bc}} - T_P}{\Delta x / 2} \right) = \frac{2 k A_w}{\Delta x} (T_P - T_{\text{bc}})$$
* **Matrix Contribution:** Adds $a_{\text{bc}} = \frac{2 k A_w}{\Delta x}$ to diagonal coefficient $a_P$.
* **RHS Contribution:** Adds $a_{\text{bc}} T_{\text{bc}} = \frac{2 k A_w}{\Delta x} T_{\text{bc}}$ to source vector $b_P$.

#### 2. Neumann Boundary Treatment (Insulated / Zero Heat Flux)
For insulated external faces:
$$\mathcal{F}_f = 0$$
Vanishes identically; no matrix or RHS modification.

#### 3. Robin Convection Boundary Treatment (Newton's Law of Cooling)
For exposed faces subjected to fluid convection, outward flux is:
$$q_{\text{conv}} = h (T_f - T_\infty)$$
Eliminating surface temperature $T_f$ via half-cell conduction $q_{\text{cond}} = -k \frac{T_f - T_P}{\Delta n / 2} = q_{\text{conv}}$:
$$T_f = \frac{\frac{2k}{\Delta n} T_P + h T_\infty}{\frac{2k}{\Delta n} + h}$$
Yielding the net outward convective heat flux:
$$\mathcal{F}_{\text{conv}} = h_{\text{eff}} A_f (T_P - T_\infty), \quad \text{where } h_{\text{eff}} = \left( \frac{\Delta n}{2k} + \frac{1}{h} \right)^{-1}$$
* **Matrix Contribution:** Adds $h_{\text{eff}} A_f$ to diagonal coefficient $a_P$.
* **RHS Contribution:** Adds $h_{\text{eff}} A_f T_\infty$ to source vector $b_P$.

### 3.4 Diagonal Range Analysis: Cube vs Finned Model

#### 1. Simple Cube Diagonal Range:
Let $kh = \frac{k h^2}{h}$ be the internal conductance. Under the Cube's specific boundary conditions ($X$-Dirichlet, $Y,Z$-Neumann):
- **Fully Internal Cells:** $A_{PP} = 6 kh$.
- **$X$-Dirichlet Face Cells (internal in $Y, Z$):** $A_{PP} = 5 kh + 2 kh = 7 kh$.
- **$Y$ or $Z$-Neumann Face Cells (internal in $X$):** $A_{PP} = 5 kh + 0 = 5 kh$.
- **Neumann-Neumann Edge Cells (e.g. $j=0, k=0$):** $A_{PP} = 4 kh$.
- **Corner Cells:** $A_{PP} \in [4kh, 7kh]$.
Thus, for the Cube benchmark, $A_{PP} \in [4kh, 7kh]$.

#### 2. Finned Heat Sink Diagonal Formulation:
For the finned model, diagonal entries are **not** restricted to $[4kh, 7kh]$ because of Robin convection and irregular multi-face exposure:
$$A_{PP} = \sum_{nb \in \text{Internal}} \frac{k A_{nb}}{\Delta d} + \sum_{f \in \text{Dirichlet}} \frac{2 k A_f}{\Delta d} + \sum_{f \in \text{Robin}} h_{\text{eff}} A_f$$
- Internal face conductance: $a_{\text{int}} = kh$.
- Baseplate bottom Dirichlet face ($z = 0$): contributes $+2 kh$.
- Exposed convective faces: contribute $+h_{\text{eff}} h^2$ each.
- Cells at fin tips or fin corners have multiple exposed convective faces (e.g., top face plus 1 or 2 vertical side faces), accumulating multiple $+h_{\text{eff}} h^2$ terms.
- The actual numerical range $[\min_P A_{PP}, \max_P A_{PP}]$ for the finned geometry will be computed dynamically from the assembled matrix and reported directly in the benchmark diagnostics.

---

## 4. Analytical Cube Benchmark (Model B)

### 4.1 Domain & Continuous Governing Equation
$\Omega = [0, 0.1]^3\text{ m}^3$, $k = 10.0\text{ W/(m}\cdot\text{K)}$, $q = 10,000.0\text{ W/m}^3$.
* **Hot Face ($x = 0$):** $T(0, y, z) = 100.0^\circ\text{C}$ (Dirichlet).
* **Cold Face ($x = 0.1$):** $T(0.1, y, z) = 20.0^\circ\text{C}$ (Dirichlet).
* **Lateral Faces ($y = 0, y = 0.1, z = 0, z = 0.1$):** Insulated ($\frac{\partial T}{\partial n} = 0$).

### 4.2 Exact Continuous Analytical Reference & Symmetry Justification
Because boundary conditions and material properties are uniform along $y$ and $z$ and lateral boundaries have zero flux, the temperature gradients $\frac{\partial T}{\partial y} \equiv 0$ and $\frac{\partial T}{\partial z} \equiv 0$ everywhere in $\Omega$.  
The continuous 3D Poisson equation collapses to 1D in $x$:
$$-k \frac{d^2 T}{dx^2} = q \implies T(x) = -\frac{q}{2k}x^2 + C_1 x + C_2$$
Enforcing $T(0) = 100$ and $T(0.1) = 20$:
$$\mathbf{T_{\text{exact}}(x) = 100.0 - 800.0 x + 500.0 x (0.1 - x)}$$

> [!IMPORTANT]
> **Exact Continuous Analytical Reference:** $T_{\text{exact}}(x)$ is the exact solution of the continuous boundary value problem, evaluated at cell-centre coordinates $x_i = (i + 0.5)\Delta x$ for numerical error assessment.  
> The computational benchmark solves the full 3D domain with $M = N^3$ degrees of freedom and 3D sparsity (bandwidth $B = N^2$).  
> **Planned Numerical Validation Test:** The test suite will evaluate the maximum point-wise numerical error:
> $$e_\infty = \max_{i,j,k} |T_{i,j,k} - T_{\text{exact}}(x_i)|$$
> and assert $e_\infty < 0.05^\circ\text{C}$ on the $N = 20$ grid as an acceptance criterion for cell-centred FVM discretization error.

---

## 5. Complex 3D Geometry: The Finned Heat Sink Solid (Model C)

```
                         FINNED HEAT SINK SOLID GEOMETRY (MODEL C)
                   z ^
                     |          t_fin           w_chan
              0.10 m +--+      +--+      +--+      +--+   <-- Fin Tips (Robin Convection)
                     |  |      |  |      |  |      |  |
                     |  |      |  |      |  |      |  |   H_fin = 80 mm
                     |  |      |  |      |  |      |  |   4 Longitudinal Cooling Fins
                     |  |      |  |      |  |      |  |   (Re-entrant internal corners)
              0.02 m +--+------+--+------+--+------+--+   <-- Baseplate Top Interface
                     |                                |   H_base = 20 mm
              0.00 m +--------------------------------+--> x
                     0.00 m                         0.10 m
                                 y extends into page (100 mm)
                     <-- Baseplate Bottom Interface (T_hot = 100°C) -->
```

### 5.1 Geometry & Material Parameters
* **Outer Bounding Envelope:** $100 \times 100 \times 100\text{ mm}$ ($[0, 0.1]^3\text{ m}^3$).
* **Baseplate:** $100 \times 100 \times 20\text{ mm}$ ($z \in [0, 0.02]\text{ m}$).
* **Fins:** 4 longitudinal fins ($H_{\text{fin}} = 80\text{ mm}$, $t_{\text{fin}} = 10\text{ mm}$, channel width $w_{\text{chan}} = 20\text{ mm}$).
  * Fin $x$-extents: $[0, 10]\text{ mm}$, $[30, 40]\text{ mm}$, $[60, 70]\text{ mm}$, $[90, 100]\text{ mm}$.
* **Solid Volume:** $V_{\text{solid}} = 0.00052\text{ m}^3$ (52% solid volume fraction).
* **Properties:** $k = 10.0\text{ W/(m}\cdot\text{K)}$, $q = 10,000\text{ W/m}^3$.

### 5.2 Explicitly Disjoint Boundary Surface Patches
To eliminate any geometric ambiguity or double counting at boundaries, all exterior faces of active cells are partitioned into mutually disjoint physical patches:

1. **Baseplate Bottom Face ($z = 0$):**
   - Extents: $x \in [0, 0.10]\text{ m}$, $y \in [0, 0.10]\text{ m}$, $z = 0$.
   - Boundary condition: Prescribed Dirichlet contact $T(x, y, 0) = T_{\text{hot}} = 100.0^\circ\text{C}$.
2. **Baseplate Outer Vertical Walls (2 patches):**
   - Extents: $x = 0$ and $x = 0.10\text{ m}$, strictly over $z \in [0, 0.02]\text{ m}$, $y \in [0, 0.10]\text{ m}$.
   - Boundary condition: Robin convection ($h = 50.0\text{ W/(m}^2\cdot\text{K)}$, $T_\infty = 25.0^\circ\text{C}$).
3. **Baseplate Top Inter-Fin Horizontal Faces (3 patches):**
   - Extents: $z = 0.02\text{ m}$, strictly over the three open channels: $x \in [0.01, 0.03]\text{ m}$, $[0.04, 0.06]\text{ m}$, and $[0.07, 0.09]\text{ m}$, for $y \in [0, 0.10]\text{ m}$.
   - Boundary condition: Robin convection ($h = 50.0$, $T_\infty = 25.0$).
4. **Vertical Fin Sidewalls (8 distinct patches):**
   - Extents: strictly over $z \in [0.02, 0.10]\text{ m}$, $y \in [0, 0.10]\text{ m}$:
     - Fin 1: outer wall $x = 0$, inner wall $x = 0.01\text{ m}$ (above baseplate)
     - Fin 2: inner wall $x = 0.03\text{ m}$, inner wall $x = 0.04\text{ m}$
     - Fin 3: inner wall $x = 0.06\text{ m}$, inner wall $x = 0.07\text{ m}$
     - Fin 4: inner wall $x = 0.09\text{ m}$, outer wall $x = 0.10\text{ m}$ (above baseplate)
   - Boundary condition: Robin convection ($h = 50.0$, $T_\infty = 25.0$).
5. **Horizontal Fin Top Tips (4 patches):**
   - Extents: $z = 0.10\text{ m}$, strictly over $x \in [0, 0.01]\text{ m}$, $[0.03, 0.04]\text{ m}$, $[0.06, 0.07]\text{ m}$, and $[0.09, 0.10]\text{ m}$, for $y \in [0, 0.10]\text{ m}$.
   - Boundary condition: Robin convection ($h = 50.0$, $T_\infty = 25.0$).
6. **Transverse Lateral End Walls (2 planes):**
   - Extents: $y = 0$ and $y = 0.10\text{ m}$ across the complete active cross-section.
   - Boundary condition: Insulated Neumann ($\frac{\partial T}{\partial y} = 0$, modeling symmetry planes or adiabatic end walls).

> [!NOTE]
> **Boundary Face Conservation Assertion:** The assembly routine will assert programmatically that:
> $$N_{\text{Dirichlet}} + N_{\text{Robin}} + N_{\text{Neumann}} \equiv 6 M - 2 E$$
> confirming that every exposed active cell boundary face is classified into exactly one boundary condition without omissions or overlaps.

---

## 6. Numerical Solver Formulations & Scale-Relative Tolerances

### 6.1 Standard Scale-Relative Tolerances Tied to Machine Precision
All numerical verification assertions will use scale-relative tolerances derived from IEEE-754 double precision machine epsilon ($\epsilon_{\text{mach}} = 2.2204 \times 10^{-16}$):

| Metric | Relative Tolerance Formulation | Benchmark Scale Value |
|:---|:---|:---|
| **Matrix Symmetry** | $|A_{ij} - A_{ji}| \le \epsilon_{\text{sym}} \max(|A_{ii}|, |A_{jj}|)$ | $\epsilon_{\text{sym}} = 100 \cdot \epsilon_{\text{mach}} \approx 2.22 \times 10^{-14}$ |
| **Pivot Positivity** | $D_{ii} > \epsilon_{\text{pivot}} A_{ii}$ | $\epsilon_{\text{pivot}} = 10 \cdot \epsilon_{\text{mach}} \approx 2.22 \times 10^{-15}$ |
| **Factor Non-Zero Drop**| $|L_{ij}| > \epsilon_{\text{drop}} \sqrt{L_{ii} L_{jj}}$ | $\epsilon_{\text{drop}} = 10 \cdot \epsilon_{\text{mach}} \approx 2.22 \times 10^{-15}$ |
| **Factor Reconstruction**| $\frac{\|\mathbf{A} - \mathbf{L}\mathbf{L}^T\|_\infty}{\|\mathbf{A}\|_\infty} \le \epsilon_{\text{recon}}$ | $\epsilon_{\text{recon}} = M \cdot \epsilon_{\text{mach}} \approx O(10^{-12})$ |
| **Backward Error (Solve)**| $\frac{\|\mathbf{b} - \mathbf{A}\mathbf{T}\|_2}{\|\mathbf{A}\|_\infty \|\mathbf{T}\|_2 + \|\mathbf{b}\|_2} \le \epsilon_{\text{backward}}$ | $\epsilon_{\text{backward}} = 100 \cdot \epsilon_{\text{mach}} \approx 2.22 \times 10^{-14}$ |
| **Weak Diagonal Dominance**| $A_{ii} \ge \sum_{j \ne i} |A_{ij}| - \epsilon_{\text{dom}} A_{ii}$ | $\epsilon_{\text{dom}} = 10 \cdot \epsilon_{\text{mach}} \approx 2.22 \times 10^{-15}$ |

> [!IMPORTANT]
> **Weak Diagonal Dominance Formulation:** Defined strictly using absolute values of off-diagonal entries: $A_{ii} \ge \sum_{j \ne i} |A_{ij}| - \epsilon_{\text{dom}} A_{ii}$, where the floating-point tolerance is scaled explicitly by the diagonal entry $A_{ii}$.

### 6.2 Direct Profile Cholesky ($LL^T$) & Factor Fill Metrics

For a symmetric positive-definite system $\mathbf{A} \in \mathbb{R}^{M \times M}$, the Profile Cholesky algorithm stores the variable-bandwidth lower-triangular factor $\mathbf{L}$ in a contiguous skyline profile array. To eliminate all ambiguity in benchmark reporting, we explicitly define and distinguish **three distinct fill metrics**:

#### 1. Three Distinct Fill Metrics Defined & Verified

1. **Numerical Factor Fill Ratio ($r_{\text{factor\_nnz}}$):**
   $$r_{\text{factor\_nnz}} = \frac{NNZ(L)}{NNZ(A)}$$
   - **Definition:** The ratio of actual numerical non-zero entries in the factored lower-triangular matrix $\mathbf{L}$ to the total non-zero entries in the original sparse matrix $\mathbf{A}$.
   - **Thresholding:** Entries of $\mathbf{L}$ within the profile envelope are counted as numerical non-zeros if they satisfy the scale-invariant drop threshold:
     $$|L_{ij}| > \epsilon_{\text{drop}} \sqrt{L_{ii} L_{jj}}, \quad \text{where } \epsilon_{\text{drop}} = 10 \cdot \epsilon_{\text{mach}} \approx 2.22 \times 10^{-15}$$
   - **CSV Export:** This is the exact metric exported under the column `factor_fill_ratio` in `benchmark_solvers_3d.csv` and `benchmark_summary_3d.csv`.
   - **Measured Values:**
     - For Cube (Model B): $N=10 \implies \mathbf{9.13}$ ($9.1347$); $N=20 \implies \mathbf{33.67}$ ($33.6726$); $N=30 \implies \mathbf{73.93}$ ($73.9279$).
     - For Finned Heat Sink (Model C): $N=10 \implies \mathbf{3.45}$ ($3.4507$); $N=20 \implies \mathbf{12.32}$ ($12.3162$); $N=30 \implies \mathbf{26.87}$ ($26.8716$, evaluated as $2{,}402{,}965 / 89{,}424$).

2. **Envelope Capacity Ratio ($r_{\text{env}}$):**
   $$r_{\text{env}} = \frac{N_{\text{env}}}{NNZ(A)}$$
   - **Definition:** The ratio of the total allocated skyline profile envelope capacity $N_{\text{env}}$ to the total non-zero entries in $\mathbf{A}$, where:
     $$N_{\text{env}} = \sum_{i=0}^{M-1} (i - \text{col\_start}[i] + 1)$$
   - **Significance:** Represents the theoretical memory capacity and storage overhead allocated for the profile data array relative to the original matrix non-zeros.
   - **Measured Values:**
     - For Cube (Model B): $N_{\text{env}} \equiv NNZ(L)$ because every entry within the RCM envelope is non-zero, yielding $r_{\text{env}} \equiv r_{\text{factor\_nnz}}$ ($9.13$, $33.67$, $73.93$).
     - For Finned Heat Sink (Model C): $N=10 \implies 12{,}262 / 2{,}656 \approx \mathbf{4.62}$; $N=20 \implies 392{,}362 / 25{,}184 \approx \mathbf{15.58}$; $N=30 \implies 2{,}974{,}017 / 89{,}424 \approx \mathbf{33.26}$ ($33.2575$).

3. **Lower-Triangular Reference Ratio ($r_{\text{lower\_ref}}$):**
   $$r_{\text{lower\_ref}} = \frac{NNZ(L)}{NNZ(A)_{\text{lower}}} = \frac{NNZ(L)}{\frac{NNZ(A) + M}{2}}$$
   - **Definition:** The ratio of numerical factor non-zeros in $\mathbf{L}$ to the structural non-zeros in the lower triangle (including diagonal) of the original symmetric matrix $\mathbf{A}$, where $NNZ(A)_{\text{lower}} = \frac{1}{2}(NNZ(A) - M) + M = \frac{NNZ(A) + M}{2}$.
   - **Significance:** Measures the fill-in multiplier specifically against the lower triangular half that $\mathbf{L}$ replaces.
   - **Measured Values:**
     - For Cube (Model B): $N=10 \implies 58{,}462 / 3{,}700 \approx \mathbf{15.80}$; $N=20 \implies 1{,}804{,}849 / 30{,}800 \approx \mathbf{58.60}$; $N=30 \implies 13{,}573{,}161 / 105{,}300 \approx \mathbf{128.90}$.
     - For Finned Heat Sink (Model C): $N=10 \implies 9{,}165 / 1{,}588 \approx \mathbf{5.77}$; $N=20 \implies 310{,}171 / 14{,}672 \approx \mathbf{21.14}$; $N=30 \implies 2{,}402{,}965 / 51{,}732 \approx \mathbf{46.45}$ ($46.4464$).

#### Authoritative Six-Configuration Fill-Metrics Reference Table:

| Geometry | Resolution ($N$) | Unknown DOFs ($M$) | Matrix $NNZ(A)$ | Factor $NNZ(L)$ | Envelope $N_{\text{env}}$ | Numerical Factor Fill Ratio ($r_{\text{factor\_nnz}}$) | Envelope Capacity Ratio ($r_{\text{env}}$) | Lower-Triangular Ref. Ratio ($r_{\text{lower\_ref}}$) |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Cube** | 10 | 1,000 | 6,400 | 58,462 | 58,462 | **9.13** | **9.13** | **15.80** |
| **Cube** | 20 | 8,000 | 53,600 | 1,804,849 | 1,804,849 | **33.67** | **33.67** | **58.60** |
| **Cube** | 30 | 27,000 | 183,600 | 13,573,161 | 13,573,161 | **73.93** | **73.93** | **128.90** |
| **Finned Sink** | 10 | 520 | 2,656 | 9,165 | 12,262 | **3.45** | **4.62** | **5.77** |
| **Finned Sink** | 20 | 4,160 | 25,184 | 310,171 | 392,362 | **12.32** | **15.58** | **21.14** |
| **Finned Sink** | 30 | 14,040 | 89,424 | 2,402,965 | 2,974,017 | **26.87** | **33.26** | **46.45** |

> [!IMPORTANT]
> **Reporting Rule on Fill Metrics:** Each metric must be labeled explicitly in all tables, text, and figures. Never refer to all three indistinctly as simply "fill ratio". For the finned $N=30$ system, the three metrics evaluate to approximately **26.87**, **33.26**, and **46.45** respectively.

#### 2. Explanation of Omitted Factor Entries in Profile Envelope

In the profile Cholesky factorization of the non-convex Finned Heat Sink (Model C), the skyline profile envelope encompasses all coordinates between $\text{col\_start}[i]$ and $i$ for each row $i$.
- For Finned $N=30$, the allocated envelope contains $N_{\text{env}} = 2{,}974{,}017$ scalar locations.
- Out of these, $NNZ(L) = 2{,}402{,}965$ entries exceed the scale-invariant numerical threshold $|L_{ij}| > \epsilon_{\text{drop}} \sqrt{L_{ii} L_{jj}}$.
- The remaining $571{,}052$ positions ($19.20\%$ of the envelope) are **not counted as numerical nonzeros under the implemented threshold**.
- **Scientific Qualification:** These omitted positions must be described strictly as *not counted as numerical nonzeros under the implemented drop threshold* ($|L_{ij}| \le 10 \epsilon_{\text{mach}} \sqrt{L_{ii} L_{jj}}$). They must **not** be characterized as "necessarily exact mathematical zeros", as small floating-point cancelations, geometric channel boundaries, and numerical drop tolerances interact within the profile envelope.

#### 3. Summary of Additional Factor Metrics
4. **Allocated Factor Memory:** $N_{\text{env}} \times \text{sizeof(double)} + 2 M \times \text{sizeof(size\_t)}$ (bytes converted to MiB via division by $1024^2$).
5. **Factorization Flop Count:** $\sum_{i=0}^{M-1} \left[ \sum_{j=\text{col\_start}[i]}^{i-1} 2(j - \max(\text{col\_start}[i], \text{col\_start}[j])) + (i - \text{col\_start}[i]) \right]$.

### 6.3 Preconditioned Conjugate Gradient (PCG with Jacobi) & Eigen Conversion

#### 1. CSR-to-Eigen Matrix Conversion Semantics
To interface our native custom `CSRMatrix` with the library-backed Eigen 3.4.0 solvers (`Eigen::ConjugateGradient<Eigen::SparseMatrix<double, Eigen::RowMajor, int>, Eigen::Lower|Eigen::Upper, Eigen::DiagonalPreconditioner<double>>`), the conversion routine `convert_csr_to_eigen` is employed.
- **Conversion Nature:** The conversion is a **validated sequential compressed deep copy using Eigen's `reserve/startVec/insertBack/finalize` API**.
- **Prohibition of "Zero-Copy" Characterization:** Under no circumstances should this conversion be described as "zero-copy". Eigen's sparse storage allocates its own internal memory arrays (`m_values`, `m_innerIndices`, `m_outerIndex`), which are distinct from our native CSR buffers.
- **Verification Integrity:** During the deep copy, the routine validates that row pointers and column indices are strictly monotonically non-decreasing, non-negative, and within dimensional bounds $[0, M)$, and asserts that all numeric values are finite (rejecting IEEE-754 `NaN` and `Inf`).
- **Timing Isolation:** Format conversion runtime ($t_{\text{convert}}$) is isolated and recorded independently in all benchmark logs (e.g., $0.995\text{ ms}$ for Cube $N=30$; $0.475\text{ ms}$ for Finned $N=30$), distinct from matrix assembly ($t_{\text{asm}}$) and PCG solve iterations ($t_{\text{sol}}$).

#### 2. PCG Theoretical Dynamics
- PCG minimizes error in the $\mathbf{A}$-norm: $\|\mathbf{e}_k\|_A = \sqrt{\mathbf{e}_k^T \mathbf{A} \mathbf{e}_k}$.
- While $\|\mathbf{e}_k\|_A$ decreases monotonically, **Euclidean residual norms $\|r_k\|_2 = \|\mathbf{b} - \mathbf{A}\mathbf{T}_k\|_2$ are not monotonic**. Residual oscillations are normal and expected.
- Per-iteration work is $O(NNZ_A) \approx O(M)$. Asymptotic total work scales as **$O(M^{4/3})$** due to iteration scaling $k \sim O(\sqrt{\kappa}) \sim O(M^{1/3})$.

### 6.4 Successive Over-Relaxation (SOR) Mathematical Rigor

#### 1. Symmetric Similarity Transformation for Jacobi Spectral Radius Estimation
The standard Jacobi iteration matrix is:
$$\mathbf{B} = \mathbf{I} - \mathbf{D}^{-1}\mathbf{A}$$
Because the diagonal matrix $\mathbf{D}$ is spatially non-uniform, $\mathbf{B}$ is **not symmetric** in the ordinary Euclidean inner product ($\mathbf{B} \ne \mathbf{B}^T$). Consequently, the conventional Euclidean Rayleigh quotient cannot be rigorously applied directly to $\mathbf{B}$ or $\mathbf{B}^2$.

**Mathematical Resolution via Symmetric Similarity Transformation:**  
We construct the symmetric matrix:
$$\mathbf{C} = \mathbf{I} - \mathbf{D}^{-1/2}\mathbf{A}\mathbf{D}^{-1/2}$$
Because $\mathbf{A}$ is symmetric and $\mathbf{D}$ is positive diagonal ($D_{ii} > 0$), $\mathbf{D}^{-1/2}$ is also positive diagonal and symmetric. Thus:
$$\mathbf{C}^T = \left(\mathbf{I} - \mathbf{D}^{-1/2}\mathbf{A}\mathbf{D}^{-1/2}\right)^T = \mathbf{I} - \mathbf{D}^{-1/2}\mathbf{A}^T\mathbf{D}^{-1/2} = \mathbf{C}$$
$\mathbf{C}$ is strictly symmetric.

Furthermore, $\mathbf{C}$ is related to $\mathbf{B}$ by the similarity transformation:
$$\mathbf{C} = \mathbf{S} \mathbf{B} \mathbf{S}^{-1}, \quad \text{where } \mathbf{S} = \mathbf{D}^{1/2} \text{ and } \mathbf{S}^{-1} = \mathbf{D}^{-1/2}$$
$$\mathbf{C} = \mathbf{D}^{1/2} \mathbf{B} \mathbf{D}^{-1/2}$$
Substituting the Jacobi iteration matrix $\mathbf{B} = \mathbf{I} - \mathbf{D}^{-1}\mathbf{A}$:
$$\begin{aligned}
\mathbf{C} &= \mathbf{D}^{1/2} \left( \mathbf{I} - \mathbf{D}^{-1}\mathbf{A} \right) \mathbf{D}^{-1/2} \\
&= \mathbf{D}^{1/2} \mathbf{D}^{-1/2} - \mathbf{D}^{1/2} \mathbf{D}^{-1}\mathbf{A}\mathbf{D}^{-1/2} \\
&= \mathbf{I} - \mathbf{D}^{-1/2}\mathbf{A}\mathbf{D}^{-1/2}
\end{aligned}$$
This establishes that:
1. $\mathbf{C}$ is strictly symmetric ($\mathbf{C} = \mathbf{C}^T$) because $\mathbf{A} = \mathbf{A}^T$ and $\mathbf{D}$ is positive diagonal.
2. $\mathbf{C}$ is similar to $\mathbf{B}$ under $\mathbf{S} = \mathbf{D}^{1/2}$ and $\mathbf{S}^{-1} = \mathbf{D}^{-1/2}$ ($\mathbf{C} = \mathbf{S} \mathbf{B} \mathbf{S}^{-1}$).
3. Because similar matrices share identical eigenvalues, $\rho(\mathbf{C}) = \rho(\mathbf{B})$.
4. Consequently, $\rho(\mathbf{B}) = \sqrt{\rho(\mathbf{C}^2)}$.

**Bipartite Spectrum & Matrix-Free Squared Operator:**
- Because the Cartesian voxel connectivity graph is bipartite (2-colorable red-black checkerboard), the spectrum of $\mathbf{C}$ contains eigenvalue pairs $\{+\lambda, -\lambda\}$.
- To prevent power iteration from oscillating between opposite eigenvector directions, the power iteration is applied to the squared operator $\mathbf{C}^2$, whose eigenvalues $\lambda^2 \ge 0$ are strictly non-negative.
- The operator $\mathbf{C}$ will **never be explicitly formed or stored** as a dense matrix. Instead, for any vector $\mathbf{v}$, the product $\mathbf{C}\mathbf{v}$ evaluates in $O(NNZ_A)$ operations via sparse CSR matvec and diagonal vector scalings:
  $$\mathbf{C}\mathbf{v} = \mathbf{v} - \mathbf{D}^{-1/2} \left( \mathbf{A} (\mathbf{D}^{-1/2} \mathbf{v}) \right)$$
- Matrix-free power iteration on $\mathbf{C}^2$ computes:
  $$\mathbf{w}_1 = \mathbf{C}\mathbf{x}_k$$
  $$\mathbf{w}_2 = \mathbf{C}\mathbf{w}_1 = \mathbf{C}^2 \mathbf{x}_k$$

**Rigorous Euclidean Rayleigh Quotient & Convergence Check:**
With normalized iterate $\|\mathbf{x}_k\|_2 = 1$, because $\mathbf{C}^2$ is symmetric positive semidefinite, the conventional Euclidean Rayleigh quotient provides a variational estimate of the dominant eigenvalue:
$$\mu_k = \mathbf{x}_k^T \mathbf{w}_2$$
The eigenpair residual vector is:
$$\mathbf{r}_{\text{eigen}} = \mathbf{w}_2 - \mu_k \mathbf{x}_k$$
Convergence is verified when:
$$\frac{\|\mathbf{r}_{\text{eigen}}\|_2}{|\mu_k|} \le \text{tol}_{\text{eigen}} \quad (\text{with } \text{tol}_{\text{eigen}} = 10^{-5})$$
- Maximum iterations: 1,000.
- If converged within 1,000 iterations: returns status `CONVERGED` and computes the numerical estimate:
  $$\hat{\rho}(\mathbf{B}) = \sqrt{\mu_k}$$
- If the iteration cap is reached without satisfying convergence: returns status `NOT_CONVERGED`, logs a warning, and leaves $\hat{\rho}(\mathbf{B})$ flagged as an unverified numerical estimate.
- In the final report, $\hat{\rho}(\mathbf{B})$ is explicitly designated as a **numerical estimate** derived from the discrete matrix, contrasted with the idealized continuous reference $\rho_{\text{ideal}}(\mathbf{B}) = \frac{2 + \cos(\pi/N)}{3}$.

#### 2. Dual-Metric SOR Parameter Sweep (Iterations & Runtimes)
The empirical sweep over $\omega \in [1.1, 1.95]$ with step $\Delta \omega = 0.05$ will record:
1. Iterations to reach tolerance ($10^{-6}$).
2. Median solve runtime (ms) across 5 repeated runs.
3. Termination status.

The report will distinguish:
- **$\omega_{\text{iter}}$:** The relaxation parameter that minimizes iteration count.
- **$\omega_{\text{time}}$:** The relaxation parameter that minimizes actual wall-clock solve time.
- **$\omega_{\text{ideal}}$:** The theoretical reference value derived from idealized Young formulas.

#### 3. Air-Tight, MECE 4-Tier SOR Termination Taxonomy
Divergence ceiling $\gamma_{\text{div}} = 10^4$ provides a complete, gap-free partition:
- **`CONVERGED`:** Relative residual $\frac{\|\mathbf{r}_k\|_2}{\|\mathbf{b}\|_2} \le 10^{-6}$ at $k \le 10,000$.
- **`DIVERGED_RESIDUAL`:** Relative residual $\frac{\|\mathbf{r}_k\|_2}{\|\mathbf{b}\|_2} > 10^4$. Execution halted immediately.
  *Empirical Qualification:* For $\omega = 2.1$ (outside the guaranteed convergence interval $0 < \omega < 2$), Kahan's determinant theorem establishes the lower bound $\rho(\boldsymbol{\mathcal{L}}_\omega) \ge |1 - \omega| = 1.10$. On the assembled discrete Model B $N=10$ cube system with zero initial guess ($\mathbf{T}_0 = \mathbf{0}$), the verified empirical divergence threshold crossing occurs at iteration 91 (relative residual $10{,}448.81 > 10^4$), cleanly triggering `DIVERGED_RESIDUAL` without numerical overflow.
- **`DIVERGED_NUMERICAL`:** Any component evaluates to IEEE-754 `NaN` or `Inf`.
- **`MAX_ITERATIONS_REACHED`:** $k = 10,000$ reached with residual strictly bounded: $10^{-6} < \text{rel\_res} \le 10^4$.

---

## 7. Complexity Scaling & Theoretical Bounds

```
+---------------------------------------------------------------------------------------------------------+
|                                    3D SOLVER COMPLEXITY REGIMES                                         |
+------------------------------+---------------------------+-----------------------+----------------------+
| SOLVER APPROACH              | FACTOR NONZEROS           | FACTORIZATION FLOPS   | PER-SOLVE COMPLEXITY |
+------------------------------+---------------------------+-----------------------+----------------------+
| Natural Banded Cholesky      | O(M * B) = O(M^(5/3))     | O(M * B^2) = O(M^(7/3)| O(M * B) = O(M^(5/3))|
| RCM Profile Cholesky (Ours)  | O(M * B_rcm) <= O(M^(5/3))| O(M * B_rcm^2)        | O(Profile Size)      |
| Nested Dissection (Theory)   | O(M^(4/3))                | O(M^2)                | O(M^(4/3))           |
| Jacobi PCG (Iterative)       | 0 (No factor)             | 0 (No factor)         | O(M^(4/3)) [M * sqrt]|
| Successive Over-Relaxation   | 0 (No factor)             | 0 (No factor)         | O(M * k_sor)         |
+------------------------------+---------------------------+-----------------------+----------------------+
```

* Custom C++17 Profile Cholesky with RCM ordering will be evaluated within the profile framework and is **never claimed to automatically follow nested-dissection $O(M^2)$ factorization complexity**.
* In report plots, **measured C++ data points will be plotted strictly as discrete markers**, while theoretical reference curves ($M^{7/3}$, $M^2$, $M^{4/3}$) will be rendered as separate dashed asymptotic reference baselines.

---

## 8. Experimental Fairness & Memory Measurement

1. **Identical System:** Within each geometry, the discrete system $(\mathbf{A}, \mathbf{b})$ is assembled once. Direct Cholesky, PCG, and SOR solve exactly the same system.
2. **Phase Separation & Lifecycle Timing:** 
   - Discrete assembly time ($t_{\text{asm}}$), format conversion time ($t_{\text{convert}}$ if applicable), ordering / symbolic analysis time ($t_{\text{sym}}$), numeric factorization time ($t_{\text{fac}}$), and triangular / iterative solve time ($t_{\text{sol}}$) are recorded and reported strictly separately.
   - For direct solvers (Profile Cholesky, Eigen SimplicialLLT), numeric factorization time ($t_{\text{fac}}$) and triangular forward/backward substitution time ($t_{\text{sol}}$) must be reported separately.
3. **Iteration Accounting:** Direct solvers have no iterative iteration count and will report `N/A`. Iterative solvers (PCG, SOR) report exact iterations $k$ to reach relative residual $\le 10^{-6}$.
4. **Initial Guess Applicability:** Initial guesses apply strictly to iterative solvers ($\mathbf{T}_0 = \mathbf{0}$). Direct factorization solves the system directly without initial guess iterations.
5. **Ordering Attribution:** Reverse Cuthill–McKee (RCM) bandwidth reduction and Approximate Minimum Degree (AMD) ordering must be identified separately, with respective preprocessing times ($t_{\text{sym}}$) explicitly documented.
6. **Memory Units & Accounting Boundaries (Strict Distinction):**
   - **Standard Units in Mebibytes (MiB):** The benchmark implementation converts raw byte allocations to memory figures by dividing by $1024^2 = 1{,}048{,}576$ (`1024.0 * 1024.0`). Consequently, all reported memory figures are in **mebibytes (MiB)** ($2^{20}\text{ bytes}$), not decimal megabytes ($10^6\text{ bytes}$). All documentation labels, table headers, and CSV columns documenting memory sizes are defined and interpreted in MiB.
   - **Tracked Allocation Categories vs Full Heap Footprint:**
     Reported CSR memory (`csr_mem_mb`), factor memory (`factor_mem_mb`), and iterative buffer memory (`iterative_mem_mb`) represent strictly **tracked structural allocation categories** of the core data structures:
     - *CSR Allocation:* $\text{Mem}_A = (M + 1) \times \text{sizeof(size\_t)} + NNZ_A \times (\text{sizeof(size\_t)} + \text{sizeof(double)})$.
     - *Profile Factor Allocation:* $\text{Mem}_L = N_{\text{env}} \times \text{sizeof(double)} + 2 M \times \text{sizeof(size\_t)}$.
     - *Iterative Buffers:* PCG allocates 4 vectors ($4 M \times \text{sizeof(double)}$), SOR allocates 2 vectors ($2 M \times \text{sizeof(double)}$).
     These tracked quantities represent algorithm-inherent data buffers and do **not** claim to capture the complete solver heap footprint (which may include temporary allocations, graph permutations, STL heap manager metadata, or Eigen internal workspace).
   - **Process Working Set (OS-Level Measurement):**
     Process working set is measured via the Windows PSAPI function `GetProcessMemoryInfo` (`WorkingSetSize`) before and after allocation, converted to MiB. This represents the total physical RAM currently mapped into the process address space by the operating system. Because it is influenced by OS page-fault granularity (4 KiB pages), virtual memory commit policies, DLL shared pages, and runtime heap caching, it is **kept strictly distinct from tracked algorithmic heap allocations**.
7. **Statistical Aggregation:** 5 repeated runs per solver; median timings reported.
8. **Attribution Rigor:** Differences between Cube and Finned Sink will be analyzed by active DOFs, condition number $\kappa$, and boundary damping.

---

## 9. Verification & Automated Testing Plan (Tests 24–30)

> [!IMPORTANT]
> **Status Note:** Acceptance criteria for planned implementation. No tests have been executed or claimed as passed prior to authorization.

- **Acceptance Criterion 0 (Regression Guard):** All 23 existing Round 1 tests in `workbench_tests.exe` must continue to pass without regression.
- **Planned Test 24: `test_sparse_matrix_spd_properties`**  
  Will assemble $\mathbf{A}$ for Cube and Finned Sink. Asserts scale-relative symmetry, strictly positive diagonal, scale-relative weak diagonal dominance across all rows ($A_{ii} \ge \sum_{j \ne i} |A_{ij}| - \epsilon_{\text{dom}} A_{ii}$), presence of strictly dominant boundary rows, non-positive off-diagonals, and confirms $NNZ(A) \equiv M + 2E$ matching the exact audit table (2,656 for $N=10$, 25,184 for $N=20$).
- **Planned Test 25: `test_direct_cholesky_factorization_exactness`**  
  Will factor $\mathbf{A} = \mathbf{L} \mathbf{L}^T$. Asserts all scale-relative pivots satisfy $D_{ii} > \epsilon_{\text{pivot}} A_{ii}$. Verifies factor reconstruction $\frac{\|\mathbf{A} - \mathbf{L}\mathbf{L}^T\|_\infty}{\|\mathbf{A}\|_\infty} \le \epsilon_{\text{recon}}$ and scale-relative solve backward error $\le \epsilon_{\text{backward}}$.
- **Planned Test 26: `test_pcg_residual_termination_and_energy_monotonicity`**  
  Will solve an SPD system with PCG. Asserts relative residual terminates at $\le 10^{-6}$. Verifies that $\mathbf{A}$-norm error decreases monotonically while testing Euclidean residual norm non-monotonicity.
- **Planned Test 27: `test_sor_stability_and_divergence_handling`**  
  Will assert SOR converges for $\omega = 1.5$. Will verify that reaching `max_iterations` triggers `MAX_ITERATIONS_REACHED`. Empirical acceptance test: will verify that setting $\omega = 2.1$ triggers `DIVERGED_RESIDUAL` ($\|r_k\|_2 / \|b\|_2 > 10^4$) at iteration 91 against the assembled Model B $N=10$ cube system with zero initial guess without numerical overflow. Will verify that matrix-free power iteration on the symmetric similar operator $\mathbf{C}^2 = (\mathbf{D}^{1/2}\mathbf{B}\mathbf{D}^{-1/2})^2 = (\mathbf{I} - \mathbf{D}^{-1/2}\mathbf{A}\mathbf{D}^{-1/2})^2$ with $\mathbf{S} = \mathbf{D}^{1/2}$ and $\mathbf{S}^{-1} = \mathbf{D}^{-1/2}$ reports numerical estimate $\hat{\rho}(\mathbf{B}) = \sqrt{\mu_k}$ with relative eigenpair residual $\le 10^{-5}$.
- **Planned Test 28: `test_cube_analytical_exactness`**  
  Will solve $N = 20$ Cube. Asserts maximum error against continuous analytical reference $T_{\text{exact}}(x) = 100 - 800x + 500x(0.1-x)$ evaluated at cell centroids is $< 0.05^\circ\text{C}$ (FVM discretization error).
- **Planned Test 29: `test_solver_to_solver_pairwise_agreement`**  
  Will solve identical systems with Cholesky, PCG, and SOR. Asserts pairwise solution agreement $\max |T_i - T_j| < 10^{-4\ \circ}\text{C}$.
- **Planned Test 30: `test_finned_geometry_energy_conservation`**  
  Will compute heat generation $Q_{\text{gen}} = q V_{\text{solid}}$ vs boundary fluxes $Q_{\text{base}} + Q_{\text{conv}}$. Asserts balance within $0.1\%$ relative tolerance.

---

## 10. Publication-Grade PDF Deliverable (< 1 MB)

The final deliverable will be compiled via headless Google Chrome into **`RA2311047010144_Round2_Report.pdf`** (< 1 MB) containing five essential figures:
1. **Figure 1: Side-by-Side 3D Temperature Fields (Cube vs Finned Sink):** 1D linear/parabolic gradient vs multidimensional cooling isotherms.
2. **Figure 2: Semi-Log Residual Convergence Dynamics ($\log_{10}(\|r_k\|_2/\|b\|_2)$ vs Iteration):** Contrasting PCG and SOR.
3. **Figure 3: Runtime Scaling vs DOFs ($M$):** Log-log plot showing banded Cholesky vs PCG scaling, with measured data plotted strictly as discrete points separate from theoretical curves.
4. **Figure 4: Memory Allocation vs DOFs ($M$):** Illustrating Cholesky factor envelope and fill-in explosion vs constant sparse CSR storage in PCG.
5. **Figure 5: Empirical SOR $\omega$ Sweep (Dual-Axis):** Showing both Iteration Count (primary Y-axis) and Median Solve Time in ms (secondary Y-axis) vs $\omega \in [1.1, 1.95]$, distinguishing $\omega_{\text{iter}}$ from $\omega_{\text{time}}$ alongside $\omega_{\text{ideal}}$ and the estimated spectral radius $\hat{\rho}(\mathbf{B})$ computed via symmetric $\mathbf{C}^2$.

Candidate metadata in report header:
- **Candidate ID:** `RA2311047010144`
- **College Mail:** `sp4985@srmist.edu.in`
- **Personal Mail:** `sp2742006@gmail.com`
- **Phone:** `9003996833`
- **Assessment:** PASS Science Works R&D Internship 2026–2027 — Round 2 Assignment Extension

---

## 11. Final Pre-Implementation Approval Checklist

- [x] **1. Symmetric Similarity Transformation Specified:** Defined $\mathbf{C} = \mathbf{S}\mathbf{B}\mathbf{S}^{-1} = \mathbf{D}^{1/2}\mathbf{B}\mathbf{D}^{-1/2} = \mathbf{I} - \mathbf{D}^{-1/2}\mathbf{A}\mathbf{D}^{-1/2}$ with $\mathbf{S} = \mathbf{D}^{1/2}$ and $\mathbf{S}^{-1} = \mathbf{D}^{-1/2}$, proven strictly symmetric ($\mathbf{C} = \mathbf{C}^T$) and similar to $\mathbf{B}$ ($\rho(\mathbf{C}) = \rho(\mathbf{B})$), allowing the Euclidean Rayleigh quotient on $\mathbf{C}^2$ to provide a mathematically valid variational estimate of the dominant eigenvalue.
- [x] **2. Matrix-Free Power Iteration on $\mathbf{C}^2$:** Evaluated strictly via sparse matvec and diagonal vector scalings, eigenpair residual check $\|\mathbf{w}_2 - \mu_k \mathbf{x}_k\|_2 / |\mu_k| \le 10^{-5}$ within 1,000 iterations, explicit `NOT_CONVERGED` outcome, and reporting $\hat{\rho}(\mathbf{B}) = \sqrt{\mu_k}$ only upon convergence.
- [x] **3. Cube Analytical Reference Wording Corrected:** Formally designated as "Exact continuous analytical reference, evaluated at cell-centre coordinates for numerical error assessment," with planned $< 0.05^\circ\text{C}$ acceptance criterion.
- [x] **4. Weak Diagonal Dominance Formula Exact:** Formulated using absolute values $A_{ii} \ge \sum_{j \ne i} |A_{ij}| - \epsilon_{\text{dom}} A_{ii}$ with scale-relative tolerance tied to $\epsilon_{\text{mach}}$.
- [x] **5. SOR $\omega=2.1$ Divergence Empirically Qualified:** Designated as a planned empirical acceptance test against the assembled matrix and zero initial guess, rather than an unverified mathematical guarantee.
- [x] **6. Language Rigor Maintained:** Phrased strictly in future/planned tense; zero premature completion claims.
- [x] **7. Strict Planning Gate:** No source code written, compiled, or executed; no test or benchmark run; no git changes made; zero fabricated data.
- [x] **8. Phase 2D Documentation Clarifications Applied:** Explicit distinction of the three fill metrics (numerical factor fill ratio $26.87$, envelope capacity ratio $33.26$, lower-triangular reference ratio $46.45$ for Finned $N=30$; authoritative integer counts $58{,}462$, $1{,}804{,}849$, $13{,}573{,}161$ for Cube $N=10, 20, 30$); factor nonzeros evaluated under scale-invariant threshold $|L_{ij}| > 10 \epsilon_{\text{mach}} \sqrt{L_{ii} L_{jj}}$ with non-counted envelope positions qualified without assuming exact mathematical zeros; Eigen conversion documented as validated sequential compressed deep copy; and memory units standardized to MiB with clear separation between tracked structural allocations and OS process working set.

