# Unified Engineering Workbench — Technical Verification Report

**Candidate ID:** RA2311047010144 | **Target Platform:** Windows 11 x64 (MinGW Static Build)  
**Organization:** PASS SCIENCE WORKS — R&D Internship 2026–2027 Assessment  
**Session Run Date:** 2026-11-01 | **Repository Root Binary:** `Launch.exe`  
**Effort Disclosure:** Target: 40–50 hours | **Recorded Actual Effort:** 46.5 hours

---

## 1. Architectural Overview & Engineering Design Choices

The **Unified Engineering Workbench (UEW)** is a modular engineering platform developed to fulfill Tasks A through F of the PASS Science Works evaluation brief. It integrates 3D CAD/ANSYS visualization, 1D steady-state thermal FEA, multicore parallel execution, embedded hardware testbench simulation, and first-principles backpropagation verification into a unified, responsive interface.

### Architectural Separation of Concerns
1. **Decoupled C++17 Numerical Core (`src/backend/src/`):** All mathematics, finite-element formulations, parsers, thread pools, and numerical verification are implemented strictly in standard C++17. The backend has zero presentation dependencies and operates completely headlessly.
2. **Embedded WinSock2 HTTP Transport (`http_server.cpp`):** A lightweight, non-blocking WinSock2 server serves REST endpoints (`/api/...`) and static frontend assets without external runtimes (Node/Python). It enforces Host and Origin CSRF validation, session-token protected shutdown, flushed logging to standard output and `logs/server.log`, and a 50 MB bounded payload limit.
3. **Single-Instance Win32 Launcher (`Launch.exe`):** Employs `CreateMutexW` (`Local\PassScienceWorks_Workbench_Launch_Mutex`) as a single-instance guard. Secondary launches query `logs/port.txt` and open the default browser at the active port without spawning duplicate servers. Static runtime linking (`-static -static-libgcc -static-libstdc++`) ensures zero external DLL requirements beyond native Windows operating system libraries.
4. **Offline Presentation Tier (`src/frontend/`):** Single-page web dashboard using CSS3 dark-mode styling, Three.js (r128), OrbitControls, and Chart.js (v4.4.7), vendor-bundled locally with zero internet dependencies.

---

## 2. Geometry Ingestion & Temperature Field Visualization (Task B)

### ASCII STL Parser
The C++ `StlParser` implements a deterministic state machine tokenizer for standard ASCII STL files:
- **Grammar & Integrity:** Validates `solid ... endsolid` framing, 3-vertex loops, non-finite values (`NaN`, `±Inf`), and coordinate bounds.
- **Surface Integration:** Computes facet count, surface area via cross-products ($\frac{1}{2}\|(\mathbf{v}_1 - \mathbf{v}_0) \times (\mathbf{v}_2 - \mathbf{v}_0)\|$), and exact Cartesian bounding box.
- **Sample Bracket Geometry (`data/sample_bracket.stl`):** Contains **32 triangular facets**, surface area of **$0.0463\text{ m}^2$ ($463.00\text{ cm}^2$ / $46,300\text{ mm}^2$)**, and bounding box **$100.0 \times 80.0 \times 100.0\text{ mm}$**. The mesh topology is an open sheet-metal shell (non-watertight); rendered via double-sided lighting.

### ANSYS Nodal Temperature Parser
The `AnsysParser` ingests tabular nodal datasets (`node_id,x_m,y_m,z_m,temperature_C`):
- **Validation:** Enforces positive integer IDs, non-duplicate nodes via hash set, finite coordinates, and physical limits ($-273.15^\circ\text{C} \le T \le 5000^\circ\text{C}$).
- **Synthetic Disclosure:** `data/ansys_thermal_synthetic.csv` is explicitly flagged and displayed as synthetic ($3,485$ nodes, range $36.87^\circ\text{C}$ to $104.00^\circ\text{C}$, average $65.08^\circ\text{C}$).
- **Interactive 3D Point Cloud:** Rendered in Three.js with continuous jet colormap and raycast hover inspection displaying Node ID, coordinates, and temperature.

### Measured Parser Capacities & File Size Limits
All endpoints enforce a strict **50 MB payload cap**. Stress testing on the running server (`tools/stress_files.ps1`) verified real ingestion limits:

| Format | Tested Entities | File Size (MB) | File Size (Bytes) | Server Parse Time (ms) | Response Payload (Bytes) | Status |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **STL (ASCII)** | 50,000 facets | 7.18 MB | 75,32,053 | **3573.3 ms** | 81,00,244 | **SUCCESS** |
| **STL (ASCII)** | 1,80,000 facets | 25.86 MB | 2,71,15,255 | **13044.45 ms** | 2,91,60,247 | **SUCCESS** |
| **STL (ASCII)** | 2,70,000 facets | 38.79 MB | 4,06,72,855 | **20789.51 ms** | 4,37,40,248 | **SUCCESS** |
| **ANSYS (CSV)** | 500,000 nodes | 13.84 MB | 1,45,10,530 | **4281.23 ms** | 2,21,22,474 | **SUCCESS** |

---

## 3. 1D Steady-State Thermal FEA & Verification (Task C)

### Mathematical Formulation
Steady-state heat conduction along a uniform 1D rod is governed by Poisson's equation:
$$-\frac{d}{dx}\left(k A \frac{dT}{dx}\right) = q A, \quad x \in [0, L], \quad T(0) = T_0, \quad T(L) = T_L$$
With constant conductivity $k$, area $A$, and uniform internal heat generation $q$, the exact analytical solution is:
$$T_{\text{exact}}(x) = T_0 + \frac{T_L - T_0}{L}x + \frac{q}{2k}x(L - x)$$
For benchmark parameters $L = 1.0\text{ m}, A = 10^{-4}\text{ m}^2, k = 10.0\text{ W/(m}\cdot\text{K)}, q = 10,000\text{ W/m}^3, T_0 = 100.0^\circ\text{C}, T_L = 20.0^\circ\text{C}$:
$$T_{\text{exact}}(x) = 100 - 80x + 500x(1 - x)$$

### Galerkin Element Formulation & Tridiagonal Solve
Discretizing into $N$ linear elements of size $h = L/N$ with shape functions $N_1(x) = 1 - \xi$ and $N_2(x) = \xi$:
$$\mathbf{k}^e = \frac{k A}{h}\begin{bmatrix} 1 & -1 \\ -1 & 1 \end{bmatrix}, \quad \mathbf{f}^e = \frac{q A h}{2}\begin{bmatrix} 1 \\ 1 \end{bmatrix}$$
Global assembly creates a symmetric tridiagonal stiffness matrix. Dirichlet boundary conditions are enforced by system partitioning: boundary columns shift to effective loads for interior unknowns $i \in \{1, \dots, N-1\}$:
$$F_1^{\text{eff}} = F_1 + \frac{kA}{h}T_0, \quad F_{N-1}^{\text{eff}} = F_{N-1} + \frac{kA}{h}T_L$$
The $(N-1)$ interior linear system is solved in $O(N)$ operations via the Thomas algorithm (TDMA).

### Nodal Superconvergence & Asymptotic Midpoint Error
Linear 1D elements span the exact piecewise-linear Green's function, so nodal temperatures match analytical values to machine precision ($|T_i - T_{\text{exact}}(x_i)| < 10^{-14\ \circ}\text{C}$). Truncation error is evaluated at element midpoints $x_{\text{mid}} = x_i + h/2$, where the linear interpolant deviates from the parabolic peak by:
$$e_{\text{mid}}^{\text{theory}} = \frac{q h^2}{8 k}$$
Evaluating across $N \in \{10, 20, 40, 80\}$ meshes confirms exact second-order $O(h^2)$ convergence ($p = 2.0000$):

| Mesh (N) | Element Size h (m) | Max Midpoint Error (C) | Theoretical Error q*h^2/8k (C) | Max Nodal Error (C) | Error Reduction Ratio | Observed Rate (p = log2 r) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 10 | 0.1 | 1.25000000 | 1.25000000 | 0.0E+000 | 1.0000 | **2.0000** |
| 20 | 0.05 | 0.31250000 | 0.31250000 | 0.0E+000 | 4.0000 | **2.0000** |
| 40 | 0.025 | 0.07812500 | 0.07812500 | 0.0E+000 | 4.0000 | **2.0000** |
| 80 | 0.0125 | 0.01953125 | 0.01953125 | 0.0E+000 | 4.0000 | **2.0000** |

---

## 4. Multicore Execution & Reproducibility (Task D)

### Concurrency Architecture
The batch benchmark engine (`multicore_bench.cpp`) uses a persistent, bounded `ThreadPool` across $p \in \{1, 2, 4\}$ worker threads, eliminating thread creation overhead during timing runs. A cooperative `std::atomic<bool>` cancel flag enables mid-run cancellation. Concurrent benchmark requests return HTTP 409 Conflict.

### System Environment & Evidence Run Metadata
*Recorded programmatically from a single verified execution (`results/environment.json`):*

| Parameter | Recorded Value |
|---|---|
| **Candidate ID** | RA2311047010144 |
| **CPU Model** | 13th Gen Intel(R) Core(TM) i5-13420H |
| **Physical / Logical Cores** | 8 Physical / 12 Logical Cores |
| **Total System RAM** | 15.7108 GB |
| **Compiler & Platform** | GNU 16.2.0 (Windows 11 x64, MinGW Static) |
| **Optimization Flags** | "-O3 -DNDEBUG -O3" |
| **Benchmark Workload** | 20000 Cases, Mesh N = 200, Seed 42, 5 Repetitions |
| **Launch-to-Ready Time** | 0 ms |
| **Batch Generation Time** | 2.14 ms |
| **Warmup Duration** | 258.66 ms |
| **Max Serial/Parallel Diff** | 0.0000E+000 C |

### Parallel Scaling Results
To ensure timings exceed the 100 ms noise floor, the workload evaluates 20,000 cases with mesh $N = 200$ and fixed seed 42 across 5 repetitions:

| Workers (p) | Rep 1 (ms) | Rep 2 (ms) | Rep 3 (ms) | Rep 4 (ms) | Rep 5 (ms) | Median (ms) | Mean (ms) | Speedup (Sp) | Efficiency (Ep) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 1 | 285.5 | 276.63 | 265.08 | 277.57 | 272.5 | **276.63** | 275.46 | **1x** | 100.0% |
| 2 | 144.46 | 148.64 | 236.18 | 178.2 | 153.61 | **153.61** | 172.22 | **1.8009x** | 90.0% |
| 4 | 87.46 | 85.04 | 82.26 | 82.76 | 91.24 | **85.04** | 85.75 | **3.2531x** | 81.3% |

- **Numerical Invariance:** Parallel execution preserves serial results identically across all 20,000 solutions: $\max |T_{\text{serial}} - T_{\text{parallel}}| = \mathbf{0.0000\times 10^0\ ^\circ\text{C}} \le 1.0 \times 10^{-8\ \circ}\text{C}$ (`PASSED`).

---

## 5. Embedded Integration & HIL Testbench Architecture (Task E)

### Multi-Target Hardware Topology
The workbench models a Hardware-in-the-Loop (HIL) testbench integrating three heterogeneous targets:
1. **Host Microcontroller:** STMicroelectronics STM32H753VI (ARM Cortex-M7 @ 480 MHz) with DP-FPU for real-time control.
2. **Co-Processor FPGA:** AMD/Xilinx Artix-7 XC7A35T for high-speed sensor DSP and digital filtering.
3. **Debug/Trace Interface:** Segger J-Link Ultra+ Emulator (JTAG IEEE 1149.1 / SWD).

### Electrical & Protocol Constraints
- **Ground Loops & Star Grounding:** Differential return currents across high-speed FPGA switching transients and sensitive analog MCU rails require a dedicated star grounding layout to eliminate common-impedance noise coupling.
- **Voltage Domain Isolation:** The STM32H7 operates on 3.3V logic, whereas Artix-7 I/O banks interface at 1.8V. High-speed bidirectional level translators (TI TXS0108E) prevent gate oxide breakdown.
- **Role of JTAG vs General-Purpose Compute:** JTAG is an IEEE 1149.1 synchronous TAP master probe for boundary-scan test, non-volatile flashing, and hardware breakpointing. It lacks autonomous arbitration, packet framing, and asynchronous DMA messaging required for general-purpose peer-to-peer compute.

### Software State Machine & Fault Recovery
The C++ `EmbeddedHardwareManager` simulates live 10 Hz telemetry streaming (`STREAMING_TELEMETRY`), fault injection (timeout, CRC corruptions), and automated link recovery (`POST /api/hardware/recover`). All telemetry packets explicitly include `"is_simulated": true` and display `SIMULATED / 10 Hz MOCK`.

---

## 6. Backpropagation Mathematics & Gradient Verification (Task F)

### Model Graph & Forward Pass
The scalar neural network comprises:
$$z = w \cdot x + b, \quad h = \tanh(z), \quad \hat{y} = v \cdot h + c, \quad J = \frac{1}{2}(\hat{y} - y)^2$$
Given inputs $x = 1.0$, target $y = 2.0$, learning rate $\eta = 0.1$, and initial parameters $\{w_0 = 0.5, b_0 = 0.0, v_0 = 1.0, c_0 = 0.0\}$:

#### Step 0 Forward & Analytical Gradients
| Quantity | Symbol | Formula | Program Output |
|---|---|---|---|
| Initial Weights | w0, b0, v0, c0 | Given | w = 0.5, b = 0.0, v = 1.0, c = 0.0 |
| Linear Combination | z0 | w*x + b | **0.5** |
| Hidden Activation | h0 | tanh(z0) | **0.46211716** |
| Output Prediction | y_hat_0 | v*h0 + c | **0.46211716** |
| Step 0 Loss | J0 | 0.5*(y_hat_0 - y)^2 | **1.18254182** |
| Gradient wrt c | dJ/dc | y_hat_0 - y | **-1.53788284** |
| Gradient wrt v | dJ/dv | (y_hat_0 - y)*h0 | **-0.71068205** |
| Gradient wrt b | dJ/db | (y_hat_0 - y)*v*(1 - h0^2) | **-1.20946448** |
| Gradient wrt w | dJ/dw | (dJ/db)*x | **-1.20946448** |

#### Step 1 Parameter Update ($\theta_1 = \theta_0 - \eta \nabla J$)
| Parameter | Symbol | Computation | Value at Step 1 |
|---|---|---|---|
| Weight w | w1 | 0.5 - 0.1 * (-1.20946448) | **0.62094645** |
| Bias b | b1 | 0.0 - 0.1 * (-1.20946448) | **0.12094645** |
| Weight v | v1 | 1.0 - 0.1 * (-0.71068205) | **1.07106820** |
| Bias c | c1 | 0.0 - 0.1 * (-1.53788284) | **0.15378828** |

### Numerical Finite Difference Validation ($\epsilon = 10^{-6}$)
Analytical gradients were verified via central finite differences: $\left[\frac{\partial J}{\partial \theta}\right]_{\text{FD}} = \frac{J(\theta + \epsilon) - J(\theta - \epsilon)}{2\epsilon}$:

| Parameter θ | Analytical dJ/dθ | Numerical FD (eps=1e-6) | Absolute Difference | Verification Status (Tol <= 1e-5) |
|:---:|:---:|:---:|:---:|:---:|
| **w** | -1.20946448 | -1.20946448 | 1.13E-10 | **PASSED** |
| **b** | -1.20946448 | -1.20946448 | 1.13E-10 | **PASSED** |
| **v** | -0.71068205 | -0.71068205 | 5.25E-11 | **PASSED** |
| **c** | -1.53788284 | -1.53788284 | 1.12E-10 | **PASSED** |

### Physical Meaning of Gradient Directions
Initial prediction $\hat{y}_0 \approx 0.462$ underpredicts target $y = 2.0$, producing negative error $(\hat{y}_0 - y) = -1.538$. Because activations $v_0 = 1.0$, $h_0 \approx 0.462$, and $(1 - h_0^2) \approx 0.786$ are strictly positive, all four partial derivatives $\nabla J$ are strictly negative. Gradient descent updates in the opposite direction ($-\eta \nabla J$), increasing all four parameters, which drives $\hat{y} \to 2.0$ and asymptotically drives loss $J \to 0$ over 100 iterations ($\hat{y}_{100} = 2.00000000, J_{100} = 0.0000\times 10^0$).  
*(Note: Typed equations are verified in `docs/handwritten_backprop/handwritten_derivation.md`; physical photos are to be photographed and added by the candidate).*

---

## 7. Verification Outcomes & Known Limitations

### Automated C++ Test Suite (`workbench_tests.exe`)
The C++ verification test suite executes **23 automated tests across 7 functional categories** with a 100% pass rate:
1. **ASCII STL Parsing:** Positive bracket geometry, 0-byte rejection, non-finite `NaN` coordinates, malformed syntax.
2. **ANSYS CSV Parsing:** Positive synthetic dataset, empty CSV, corrupt header tokens, physical sub-absolute zero rejection ($-273.15^\circ\text{C}$), upper-bound rejection ($> 5000^\circ\text{C}$), duplicate node ID rejection.
3. **Thermal FEA Solver:** Analytical and midpoint convergence, single/two-element edge cases ($N=1, N=2$), input parameter range rejection ($L, A, k, q, T_0, T_L$).
4. **Multicore Benchmarking:** Serial/parallel node temperature agreement, 409 Conflict concurrency guard, cooperative mid-run cancellation.
5. **Embedded Hardware Life Cycle:** Connection state machine, 10 Hz streaming, fault injection, link recovery.
6. **Backpropagation Engine:** Analytical gradient checks, central finite difference matching ($< 10^{-10}$), 100-step monotonic loss decay.
7. **JSON Robustness & Sanitization:** Emission of `null` without `inf` or `nan` tokens on failed parsing attempts.

### Known Prototype Limitations
1. **CAD File Formats:** Parser is tailored to ASCII STL. Binary STL, IGES, and STEP formats are outside prototype scope.
2. **FEA Dimensionality:** Solver implements 1D steady-state conduction. 2D/3D domains require tetrahedral volume meshing and sparse iterative solvers.
3. **Hardware Emulation:** The testbench uses C++ software mock adapters. Interfacing physical silicon requires hardware JTAG/USB adapters.
