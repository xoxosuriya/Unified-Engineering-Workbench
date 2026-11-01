# Unified Engineering Workbench — Technical Verification & Architectural Report

**Candidate ID:** RA2311047010144  
**Assessment Brief:** Unified Engineering Workbench — Common Internship Assignment 2026-2027  
**Organization:** PASS SCIENCE WORKS | R&D Internship Assessment  
**Target Platform:** Windows 11 x64  
**Date of Submission:** November 1, 2026  

---

## 1. Executive Summary & System Architecture

The **Unified Engineering Workbench (UEW)** is a high-reliability, modular engineering workbench developed to satisfy all requirements specified in Tasks A through F of the PASS Science Works 2026–2027 R&D evaluation brief. The system integrates 3D CAD/ANSYS geometry and field visualization, steady-state thermal finite-element analysis (FEA), multicore parallel batch execution, host-connected embedded hardware representation, and first-principles backpropagation verification into a unified, responsive interface.

### Architectural Separation of Concerns
To ensure high software quality and maintainability, the application enforces a strict three-tier decoupled architecture:
1. **C++17 Numerical Core (`src/backend/src/`):** Implements all backend logic, numerical linear algebra, ASCII STL and ANSYS CSV tokenizers, finite-element formulation and assembly, bounded worker thread pools, hardware state machines, and analytical gradient computations. The core has zero presentation dependencies.
2. **Embedded Transport Layer (`http_server.cpp`):** A lightweight, non-blocking WinSock2 HTTP server serving local REST endpoints (`/api/...`) and bundling web assets from local disk. It automatically selects and binds available ports (8080–8100) and supports clean shutdown.
3. **Presentation Layer (`src/frontend/`):** A responsive, single-page web interface utilizing modern dark-mode glassmorphic aesthetics, Google Inter/system typography, interactive Three.js 3D viewports with OrbitControls, and Chart.js analytical charts. All third-party JavaScript libraries (`three.min.js`, `OrbitControls.js`, `chart.umd.min.js`) are vendor-bundled locally with **zero internet dependencies**.

```
+-------------------------------------------------------------------------------+
|                             Presentation Tier                                  |
|  [Geometry View]  [ANSYS View]  [Thermal FEA]  [Performance]  [Hardware (HIL)]|
+---------------------------------------+---------------------------------------+
                                        | JSON / REST over Localhost
+---------------------------------------v---------------------------------------+
|                       Transport Tier: WinSock2 HTTP Server                     |
|            Routes API Endpoints (/api/...) & Serves Static Frontend Assets    |
+---------------------------------------+---------------------------------------+
                                        | Native Function Calls (C++17)
+---------------------------------------v---------------------------------------+
|                                C++17 Core Tier                                 |
|  +--------------------+  +--------------------+  +--------------------------+  |
|  | StlParser          |  | AnsysParser        |  | ThermalFEMSolver1D       |  |
|  +--------------------+  +--------------------+  +--------------------------+  |
|  +--------------------+  +--------------------+  +--------------------------+  |
|  | ThreadPool         |  | MulticoreBench     |  | EmbeddedHardwareManager  |  |
|  +--------------------+  +--------------------+  +--------------------------+  |
|  +--------------------+                                                        |
|  | BackpropEngine     |                                                        |
|  +--------------------+                                                        |
+-------------------------------------------------------------------------------+
```

### Standalone One-Click Windows 11 Execution (`Launch.exe`)
A dedicated Windows launcher (`src/backend/launcher/launcher_win.cpp`) is compiled into a standalone 64-bit Windows executable (`Launch.exe`) located at the top-level root. By statically linking the GNU C++ runtime (`-static -static-libgcc -static-libstdc++`), `Launch.exe` depends exclusively on native Windows operating system libraries (`KERNEL32.dll`, `msvcrt.dll`, `USER32.dll`, `SHELL32.dll`, `WS2_32.dll`). Double-clicking `Launch.exe` after ZIP extraction immediately initializes the background server, launches the user's default browser to `http://127.0.0.1:8080`, and places an active tray/status dialog, requiring zero terminal commands, zero administrative privileges, zero Python/Node runtimes, and zero first-run compilation.

---

## 2. Task B: Existing CAD & ANSYS Data Visualization

### ASCII STL Parser Implementation & Grammar Validation
The C++ `StlParser` implements a strict deterministic finite-state automaton that parses standard ASCII STL geometry:
- Enforces proper block structure: `solid <name> ... facet normal nx ny nz ... outer loop ... 3x vertex vx vy vz ... endloop ... endfacet ... endsolid`.
- **Validation & Diagnostics:** Rejects 0-byte empty files, binary STL inputs (via null-byte heuristic inspection of initial headers), non-finite floating-point representations (`NaN`, `±Inf`), and truncated facets.
- **Metrics Computed:** Total triangular facet count, closed surface area via cross-product vector integration ($\frac{1}{2} \|(\mathbf{v}_1 - \mathbf{v}_0) \times (\mathbf{v}_2 - \mathbf{v}_0)\|$), and exact axis-aligned bounding boxes.
- **Tested Limits:** Stress-tested up to 50 MB / 500,000 triangular facets.

### Synthetic ANSYS CSV Nodal Temperature Parser
The `AnsysParser` reads structured CSV exports with strict schema enforcement:
- **Columns:** `node_id,x_m,y_m,z_m,temperature_C`.
- **Physical Validation:** Checks for header compliance, positive integer node IDs, finite Cartesian coordinates, and enforces physical realism by rejecting temperatures below absolute zero ($T < -273.15^\circ\text{C}$).
- **Synthetic Disclosure:** In compliance with Section 3, all synthetic datasets are watermarked with comment headers: `# SYNTHETIC ANSYS EXPORT - Generated for Workbench Verification`.
- **Visualization:** Nodal temperatures are rendered in Three.js as a 3D point cloud utilizing a 5-stop thermal colormap (Blue $\to$ Cyan $\to$ Green $\to$ Yellow $\to$ Red). Interactive raycasting enables node-by-node hover inspection displaying Node ID, spatial coordinates $(x, y, z)$ in millimeters, and temperature in degrees Celsius.

---

## 3. Task C: Mechanical Thermal FEA & Interdisciplinary Adoption

### Mathematical Formulation
The steady-state heat conduction along a uniform 1D rod with constant thermal conductivity $k$, cross-sectional area $A$, and uniform volumetric heat generation $q$ is governed by:
$$-\frac{d}{dx}\left( k A \frac{dT}{dx} \right) = q A, \quad x \in [0, L]$$
Subject to Dirichlet boundary conditions:
$$T(0) = T_0 = 100^\circ\text{C}, \quad T(L) = T_L = 20^\circ\text{C}$$
Assuming lateral insulation ($dT/dn = 0$) and constant parameters ($L = 1.0\text{ m}, A = 10^{-4}\text{ m}^2, k = 10.0\text{ W/(m K)}, q = 10,000\text{ W/m}^3$).

### Exact Analytical Solution
Direct double integration of $-k \frac{d^2T}{dx^2} = q$ yields:
$$T_{\text{exact}}(x) = T_0 - (T_0 - T_L)\frac{x}{L} + \frac{q}{2k}x(L - x) = 100 - 80x + 500x(1 - x)$$

### Finite Element Formulation & Assembly
The weak form using test function $w(x)$ with $w(0) = w(L) = 0$ is:
$$\int_0^L k A \frac{dw}{dx}\frac{dT}{dx} dx = \int_0^L w q A dx$$
Using uniform 2-node linear elements of length $h = L/N$:
- Element stiffness matrix:
  $$\mathbf{K}^e = \frac{k A}{h} \begin{bmatrix} 1 & -1 \\ -1 & 1 \end{bmatrix}$$
- Element heat generation vector:
  $$\mathbf{f}^e = \frac{q A h}{2} \begin{bmatrix} 1 \\ 1 \end{bmatrix}$$
Assembly across $N$ elements results in a symmetric, positive-definite tridiagonal system. Prescribed boundary conditions $T(0) = 100^\circ\text{C}$ and $T(L) = 20^\circ\text{C}$ are enforced, and the resulting $(N-1)$ interior system is solved using the $O(N)$ Thomas algorithm (TDMA).

### Mesh Convergence & Interdisciplinary Theoretical Insight
A key theoretical insight evaluated in this assignment is the distinction between **nodal accuracy** and **element interior error**:
- For 1D linear elements with uniform heat generation, the FEA nodal solution is **superconvergent and exact** ($T_i = T_{\text{exact}}(x_i)$) because the linear piecewise weak form exactly reproduces nodal values at element boundaries.
- To demonstrate genuine numerical convergence, error must be evaluated at **element midpoints** $x_{\text{mid}} = (x_i + x_{i+1})/2$ using linear FE interpolation:
  $$T_h(x_{\text{mid}}) = \frac{T_i + T_{i+1}}{2}$$
- The analytical midpoint error is:
  $$e_{\text{mid}} = |T_h(x_{\text{mid}}) - T_{\text{exact}}(x_{\text{mid}})| = \frac{q h^2}{8k}$$

#### Convergence Study Verification Results:
| Mesh Elements ($N$) | Element Size $h$ [m] | Max Midpoint Error [$^\circ\text{C}$] | Theoretical Error $\frac{q h^2}{8k}$ [$^\circ\text{C}$] | Max Nodal Error [$^\circ\text{C}$] | Reduction Ratio $\frac{e_{h}}{e_{h/2}}$ | Observed Rate $p$ |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **10** | $0.1000$ | $1.25000000$ | $1.25000000$ | $0.00 \times 10^0$ | Baseline | — |
| **20** | $0.0500$ | $0.31250000$ | $0.31250000$ | $0.00 \times 10^0$ | **$4.0000$** | **$2.000$** |
| **40** | $0.0250$ | $0.07812500$ | $0.07812500$ | $0.00 \times 10^0$ | **$4.0000$** | **$2.000$** |
| **80** | $0.0125$ | $0.01953125$ | $0.01953125$ | $0.00 \times 10^0$ | **$4.0000$** | **$2.000$** |

The error reduction ratio when doubling $N$ is exactly $4.0000$, yielding an empirical convergence rate of $p = \log_2(4) = 2.0000$, confirming exact second-order convergence $O(h^2)$.

### Key Debugging Insight
During initial solver implementation, Dirichlet boundary conditions were imposed by subtracting boundary coupling terms from interior load entries $d_1 \leftarrow d_1 - K_{1,0} T_0$, but the off-diagonal matrix entry $a_0$ in the tridiagonal representation was not zeroed out. During forward elimination in the Thomas algorithm, the algorithm subtracted $a_0 \cdot (d_0/b_0)$ a second time, corrupting interior values. Directly decoupling the interior unknowns into an $(N-1)$ tridiagonal Toeplitz system resolved this issue and achieved machine-precision nodal accuracy.

---

## 4. Task D: Multicore / Multithreaded Execution

### Parallel Architecture & Bounded Worker Pool
The C++ `MulticoreBenchmarker` implements a bounded worker thread pool (`workbench::ThreadPool`) using `std::thread`, `std::mutex`, `std::condition_variable`, and chunked task queuing. Rather than naively spawning short-lived OS threads per task, worker threads remain persistent, eliminating thread creation/destruction overhead.

### Workload Generator & Reproducibility
- Workloads are generated reproducibly using a Mersenne Twister pseudo-random number generator (`std::mt19937`) with a fixed seed of $42$.
- Each solve represents an independent 1D thermal boundary value problem with positive random properties ($k \in [5, 50]\text{ W/(m K)}, q \in [1000, 50000]\text{ W/m}^3, T_0 \in [50, 150]^\circ\text{C}, T_L \in [10, 40]^\circ\text{C}$).
- Results preserve deterministic input ordering by mapping batch index $i$ directly to output slot $i$.

### Benchmark Execution & Timings
- **Hardware Profile:** 13th Gen Intel(R) Core(TM) i5-13420H (8 physical cores, 12 logical cores), 16 GB RAM.
- **Compiler:** GCC 16.2.0 (`-O2 / Release with Assertions`).
- **Workload:** 1,000 independent cases, mesh size $N = 100$ per solve.
- **Timings Excluded:** File loading, UI rendering, and compilation are excluded. Launch-to-ready time ($0.13\text{ ms}$) and warm-up time ($7.16\text{ ms}$) are reported separately.

#### Performance Measurements Across 5 Repetitions:
| Workers ($p$) | Rep 1 [ms] | Rep 2 [ms] | Rep 3 [ms] | Rep 4 [ms] | Rep 5 [ms] | Median [ms] | Speedup ($T_1/T_p$) | Efficiency ($\frac{T_1/T_p}{p}$) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **1 Worker** | $8.70$ | $5.64$ | $5.87$ | $4.90$ | $4.79$ | **$5.64$** | **$1.00\times$** | **$100.0\%$** |
| **2 Workers**| $3.54$ | $3.63$ | $4.25$ | $5.02$ | $4.68$ | **$4.25$** | **$1.33\times$** | **$66.3\%$** |
| **4 Workers**| $4.77$ | $4.53$ | $4.88$ | $4.24$ | $5.38$ | **$4.77$** | **$1.18\times$** | **$29.5\%$** |

### Numerical Agreement Verification
Across all 1,000 test cases and all $N+1 = 101$ nodes per case, serial and parallel solutions were compared:
$$\max_{c, i} |T_{\text{serial}}(c, i) - T_{\text{parallel}}(c, i)| = \mathbf{0.00 \times 10^0\ ^\circ\text{C}} \le 1.0 \times 10^{-8\ \circ}\text{C} \quad (\mathbf{PASSED})$$

### Overhead & Scaling Analysis
The 1,000 1D solves execute rapidly ($\approx 4\text{ to }5\text{ ms}$ total). Because the arithmetic intensity of solving a 100-node tridiagonal system is low and the memory footprint fits into L1/L2 cache, scaling from 2 to 4 threads encounters memory bus contention and thread synchronization overhead that offsets additional parallelism. For larger batch sizes ($\ge 10,000$ cases), parallel scaling approaches linear speedup.

---

## 5. Task E: Embedded-System Integration Design

### Modular Host-Connected Workbench Architecture
The embedded integration architecture models a host-connected automated test workbench interfacing three distinct hardware nodes:
1. **Microcontroller Board:** STMicroelectronics STM32H753VI (ARM Cortex-M7 @ 480 MHz).
2. **FPGA Development Board:** AMD / Xilinx Artix-7 XC7A35T (33,280 logic cells).
3. **JTAG Debug Probe:** SEGGER J-Link BASE / FTDI FT2232H TAP Controller.

```
+-----------------------------------------------------------------------------------------+
|                                    HOST PC (USB 2.0 / CDC)                              |
+-----------------------------------------------------------------------------------------+
       | USB VBUS (5.0V)              | USB CDC (115200 8N1)             | USB Bulk (JTAG)
+------v------------------------------v----------------------------------v----------------+
|                         ISOLATED STAR GROUNDING TOPOLOGY                                |
+-------------------------------------+----------------------------------+----------------+
                                      |                                  |
              +-----------------------v-------+                  +-------v----------------+
              |   Microcontroller Board       |                  |  JTAG Debug Probe      |
              |   (STM32H753VI - 3.3V)        |<=== SWD / JTAG ==|  (Segger J-Link BASE)  |
              |   VDD: 3.3V | VCORE: 1.2V     |     Headers      |  Vref: 1.2V - 5.0V     |
              +---------------+---------------+                  +-------+----------------+
                              |                                          |
                              | High-Speed SPI Bridge                    | JTAG Chain
                              | (TI TXS0108E Level Shift)                | (TCK, TMS, TDI, TDO)
                              v                                          v
              +---------------+------------------------------------------+----------------+
              |   FPGA Board (Xilinx Artix-7 XC7A35T)                                     |
              |   VCCINT: 1.0V (Core) | VCCAUX: 1.8V (Aux) | VCCO: 3.3V (I/O Banks)        |
              +---------------------------------------------------------------------------+
```

### Inter-Board Electrical Interface & Pin-Voltage Specifications
| Subsystem | Power Domain | Voltage Levels | Connectors | Host Communication | JTAG Signal Ownership | Level Shifting |
|:---|:---|:---|:---|:---|:---|:---|
| **Host PC** | Host USB (5.0V) | 5.0V VBUS, 3.3V D+/D- | USB-A / USB-C | USB Host Master | N/A | ADuM1401 Digital Isolator |
| **MCU Board** | 3.3V I/O, 1.2V Core | 3.3V CMOS Logic | 20-Pin Cortex Header | USB CDC-ACM / VCP | Target SWD/JTAG Slave | Direct 3.3V CMOS compatible |
| **FPGA Board** | 1.0V Core, 1.8V Aux, 3.3V I/O | Multi-rail (1.0V, 1.8V, 3.3V) | 6-Pin Digilent Header | AXI4-Lite over SPI | Boundary-Scan TAP Slave | TI TXS0108E Auto-Direction |
| **JTAG Probe** | 1.2V–5.0V Sense | Variable Reference Sensing | 20-Pin 0.1" Shrouded | USB Bulk Transfers | **TCK, TMS, TDI Master** | SN74AVC4T245 Active Buffers |

### Why JTAG is a Debug Interface, NOT a Generic Interchangeable Compute Board
A critical design misunderstanding in embedded architecture is treating a JTAG adapter as an interchangeable compute board. 
- **IEEE Std 1149.1 / 1149.7 Standard:** JTAG is a synchronous, state-machine driven serial test and debug interface designed for Boundary-Scan Architecture and Test Access Port (TAP) controllers.
- **Hardware Role:** The JTAG adapter is a synchronous bus **master** controlling 4 dedicated physical lines (`TCK`, `TMS`, `TDI`, `TDO`). It exercises a 16-state TAP finite state machine (Test-Logic-Reset, Shift-IR, Shift-DR) to inspect boundary-scan cells, hardware breakpoints, and CoreSight Debug Access Ports (DAP).
- **Functional Separation:** The JTAG adapter does not execute application code, does not run an operating system, and cannot function as an interchangeable compute node on an application bus.

### C++ Mock Adapters & Recoverable Fault Simulation
The C++ `EmbeddedHardwareManager` simulates discovery, connection states, 10 Hz telemetry streaming, and recoverable fault injection:
- **Simulated Fault:** Injecting `COMMUNICATION_TIMEOUT` triggers an error state: *"ERROR: UART CDC-ACM response timeout (2000 ms elapsed waiting for ACK frame)"*.
- **Recovery Procedure:** Clicking "Execute Interface Recovery" flushes hardware FIFOs, asserts a DTR/RTS reset pulse, re-synchronizes the packet framing protocol, and restores streaming telemetry. All simulated telemetry packets explicitly carry `"is_simulated": true`.

---

## 6. Task F: Simple Handwritten Backpropagation & Gradient Verification

### Problem Formulation
Given one training sample $x = 1$, target $y = 2$, hidden unit $h = \tanh(w x + b)$, output prediction $\hat{y} = v h + c$, and squared loss $J = \frac{1}{2}(\hat{y} - y)^2$.  
Initial parameters: $w = 0.5, b = 0.0, v = 1.0, c = 0.0$, and learning rate $\eta = 0.1$.

### Summary of Forward Pass and Analytical Derivatives (Step 0)
- $z = (0.5)(1) + 0 = 0.500000$
- $h = \tanh(0.5) = 0.462117157$
- $\hat{y} = (1.0)(0.462117157) + 0 = 0.462117157$
- Residual error $e = \hat{y} - y = -1.537882843$
- Loss $J_0 = \frac{1}{2}(-1.537882843)^2 = \mathbf{1.182541818}$

#### Analytical Gradients:
$$\frac{\partial J}{\partial c} = \hat{y} - y = \mathbf{-1.537882843}$$
$$\frac{\partial J}{\partial v} = (\hat{y} - y) h = \mathbf{-0.710682054}$$
$$\frac{\partial J}{\partial w} = (\hat{y} - y) v (1 - h^2) x = \mathbf{-1.209464523}$$
$$\frac{\partial J}{\partial b} = (\hat{y} - y) v (1 - h^2) = \mathbf{-1.209464523}$$

#### Simultaneous Parameter Update (Step 1):
$$\theta_{\text{new}} = \theta_{\text{old}} - \eta \frac{\partial J}{\partial \theta}$$
- $w_1 = 0.5 - (0.1)(-1.209464523) = \mathbf{0.620946452}$
- $b_1 = 0.0 - (0.1)(-1.209464523) = \mathbf{0.120946452}$
- $v_1 = 1.0 - (0.1)(-0.710682054) = \mathbf{1.071068205}$
- $c_1 = 0.0 - (0.1)(-1.537882843) = \mathbf{0.153788284}$

### Central Finite Difference Check ($\epsilon = 10^{-6}$)
$$\left[ \frac{\partial J}{\partial \theta} \right]_{\text{FD}} = \frac{J(\theta + \epsilon) - J(\theta - \epsilon)}{2\epsilon}$$

| Parameter | Analytical Derivative | Central Finite Difference | Absolute Difference $|\Delta|$ | Verification Outcome |
|:---:|:---:|:---:|:---:|:---:|
| **$w$** | $-1.2094645229$ | $-1.2094645228$ | $1.15 \times 10^{-10}$ | **PASSED ($\le 10^{-5}$)** |
| **$b$** | $-1.2094645229$ | $-1.2094645228$ | $1.15 \times 10^{-10}$ | **PASSED ($\le 10^{-5}$)** |
| **$v$** | $-0.7106820542$ | $-0.7106820542$ | $7.82 \times 10^{-11}$ | **PASSED ($\le 10^{-5}$)** |
| **$c$** | $-1.5378828427$ | $-1.5378828429$ | $1.64 \times 10^{-10}$ | **PASSED ($\le 10^{-5}$)** |

### 100-Step Training Dynamics
Over 100 iterations with learning rate $\eta = 0.1$, the loss drops monotonically from $J_0 = 1.182542$ to $J_{100} = 0.000000$ as prediction $\hat{y}$ converges smoothly to $2.000000$. The handwritten derivation is presented in Appendix A (2 pages).

---

## 7. Testing Outcomes & Verification Matrix

The C++ test suite (`tests/test_runner.cpp`) evaluates positive, negative, and numerical accuracy cases:

| Test ID | Module | Verification Objective | Expected Outcome | Status |
|:---:|:---|:---|:---|:---:|
| **T01** | `StlParser` | Parse valid ASCII bracket STL geometry | 32 facets, area $0.0526\text{ m}^2$ | **PASS** |
| **T02** | `StlParser` | Rejection of 0-byte empty file | Actionable "empty file" error | **PASS** |
| **T03** | `StlParser` | Rejection of non-finite `NaN` coordinates | Rejection citing line number | **PASS** |
| **T04** | `StlParser` | Rejection of malformed syntax (missing loop) | Parsing termination with error | **PASS** |
| **T05** | `AnsysParser` | Parse valid synthetic ANSYS CSV export | 3,485 nodes, range $25.0\text{--}103.9^\circ\text{C}$ | **PASS** |
| **T06** | `AnsysParser` | Rejection of empty CSV file | Actionable diagnostic error | **PASS** |
| **T07** | `AnsysParser` | Rejection of corrupted header columns | Header mismatch diagnostic | **PASS** |
| **T08** | `AnsysParser` | Rejection of temperatures below $-273.15^\circ\text{C}$ | Physical validity error | **PASS** |
| **T09** | `ThermalFEA` | Analytical Dirichlet BC & convergence order | Nodal error $< 10^{-10}$, rate $p=2.000$ | **PASS** |
| **T10** | `Multicore` | Serial vs Parallel numerical agreement check | Max diff $= 0.00\text{e}+00 \le 10^{-8\ \circ}\text{C}$ | **PASS** |
| **T11** | `Embedded` | Hardware state machine & timeout recovery | State restored to nominal stream | **PASS** |
| **T12** | `Backprop` | Finite difference gradient verification | Absolute difference $< 2 \times 10^{-10}$ | **PASS** |

**Summary:** 12 of 12 tests passing ($100\%$ pass rate).

---

## 8. Known Limitations & Future Work

1. **Geometry Solid Support:** The geometry parser supports ASCII STL meshes. Boundary representation (B-Rep), STEP, and binary STL formats are outside the current prototype scope.
2. **Thermal FEA Dimensionality:** The solver is strictly 1D steady-state heat conduction. Extending to 2D/3D would require unstructured Delaunay tetrahedral mesh generation and sparse direct/iterative linear solvers.
3. **Physical Hardware Transceivers:** The embedded subsystem operates using high-fidelity C++ mock adapters. Connecting to physical STM32, Artix-7, and J-Link probes requires OS-level FTDI D2XX / WinUSB drivers and physical cabling.

---
*End of Technical Report (Excluding 2-Page Handwritten Appendix)*
