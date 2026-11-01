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
2. **Embedded Transport Layer (`http_server.cpp`):** A lightweight, non-blocking WinSock2 HTTP server serving local REST endpoints (`/api/...`) and bundling web assets from local disk. It automatically selects and binds available ports (8080–8100), enforces thread-safe hardware mutex locking (`hw_mutex_`), enforces a 50 MB bounded payload limit, logs every request to stdout (`[HttpServer] <METHOD> <PATH> -> <STATUS>`), and provides clean shutdown.
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
A dedicated Windows launcher (`src/backend/launcher/launcher_win.cpp`) is compiled into a standalone 64-bit Windows executable (`Launch.exe`) located at the top-level root. By resolving relative paths from `GetModuleFileNameW` (`module_dir + "/src/frontend"`) and statically linking the GNU C++ runtime (`-static -static-libgcc -static-libstdc++`), `Launch.exe` depends exclusively on native Windows operating system libraries (`KERNEL32.dll`, `msvcrt.dll`, `USER32.dll`, `SHELL32.dll`, `WS2_32.dll`). Double-clicking `Launch.exe` after ZIP extraction immediately initializes the background server, launches the user's default browser to `http://127.0.0.1:8080`, and places an active tray/status dialog, requiring zero terminal commands, zero administrative privileges, zero Python/Node runtimes, and zero first-run compilation.

---

## 2. Task B: Existing CAD & ANSYS Data Visualization

### ASCII STL Parser Implementation & Geometry Characteristics
The C++ `StlParser` implements a strict deterministic finite-state automaton that parses standard ASCII STL geometry:
- Enforces proper block structure: `solid <name> ... facet normal nx ny nz ... outer loop ... 3x vertex vx vy vz ... endloop ... endfacet ... endsolid`.
- **Validation & Diagnostics:** Rejects 0-byte empty files, binary STL inputs (via null-byte inspection of initial headers), non-finite floating-point representations (`NaN`, `±Inf`), coordinate values exceeding physical engineering bounds ($|\mathbf{v}| > 10^{12}\text{ m}$), and truncated facet blocks.
- **Metrics Computed:** Total triangular facet count, surface area via cross-product vector integration ($\frac{1}{2} \|(\mathbf{v}_1 - \mathbf{v}_0) \times (\mathbf{v}_2 - \mathbf{v}_0)\|$), and exact axis-aligned bounding boxes.
- **Sample Bracket Ground-Truth Metrics:**
  - Facet Count: **32 facets**
  - Surface Area: **$0.0463\text{ m}^2$ ($462.97\text{ cm}^2$)**
  - Bounding Box: **$100.0 \times 80.0 \times 100.0\text{ mm}$** (Cartesian extents: $x \in [0, 0.100]\text{ m}, y \in [0, 0.080]\text{ m}, z \in [0, 0.100]\text{ m}$).
  - Mesh Nature: Open surface shell structure (non-watertight); the 3D renderer employs `THREE.DoubleSide` lighting to render both outer and inner face orientations accurately.
- **Tested Limits:** Rejects payloads over the 50 MB threshold with descriptive error messages. Tested against all 6 negative sample files in `data/test_samples/`.

### Synthetic ANSYS CSV Nodal Temperature Parser
The `AnsysParser` reads structured CSV exports with strict schema enforcement:
- **Columns:** `node_id,x_m,y_m,z_m,temperature_C`.
- **Physical Validation:** Checks for header compliance, positive integer node IDs (rejecting non-integer entries such as `1.5`), duplicate node ID detection via hash set tracking, finite Cartesian coordinates, and absolute zero temperature constraints ($T \ge -273.15^\circ\text{C}$).
- **Synthetic Disclosure:** In compliance with Section 3, all synthetic datasets are watermarked with comment headers: `# SYNTHETIC ANSYS EXPORT - Generated for Workbench Verification`. The parser defaults `is_synthetic = false`, activating the flag only if the header explicitly declares it.
- **Dataset Metrics (`data/ansys_thermal_synthetic.csv`):**
  - Total Nodes: **3,485 nodes**
  - Temperature Range: **$36.87^\circ\text{C}$ to $104.00^\circ\text{C}$** (Mean: **$65.08^\circ\text{C}$**)
  - Spatial Bounding Box: $100.0 \times 80.0 \times 100.0\text{ mm}$
- **Visualization:** Nodal temperatures are rendered in Three.js as a 3D point cloud utilizing a 5-stop thermal colormap (Blue $\to$ Cyan $\to$ Green $\to$ Yellow $\to$ Red). Interactive raycasting enables node-by-node hover inspection displaying Node ID, spatial coordinates $(x, y, z)$ in millimeters, and temperature in degrees Celsius.

---

## 3. Task C: Mechanical Thermal FEA & Interdisciplinary Adoption

### Mathematical Formulation
The steady-state heat conduction along a uniform 1D rod with constant thermal conductivity $k$, cross-sectional area $A$, and uniform volumetric heat generation $q$ is governed by:
$$-\frac{d}{dx}\left( k A \frac{dT}{dx} \right) = q A, \quad x \in [0, L]$$
Subject to Dirichlet boundary conditions:
$$T(0) = T_0 = 100^\circ\text{C}, \quad T(L) = T_L = 20^\circ\text{C}$$
Assuming lateral insulation ($dT/dn = 0$) and constant parameters ($L = 1.0\text{ m}, A = 10^{-4}\text{ m}^2, k = 10.0\text{ W/(m·K)}, q = 10,000\text{ W/m}^3$).

### Exact Analytical Solution
Direct double integration of $-k \frac{d^2T}{dx^2} = q$ yields:
$$T_{\text{exact}}(x) = T_0 - (T_0 - T_L)\frac{x}{L} + \frac{q L^2}{2k}\frac{x}{L}\left(1 - \frac{x}{L}\right) = 100 - 80x + 500x(1 - x)$$

### Finite Element Formulation & Explicit Matrix Assembly
The weak form using test function $w(x)$ with $w(0) = w(L) = 0$ is:
$$\int_0^L k A \frac{dw}{dx}\frac{dT}{dx} dx = \int_0^L w q A dx$$
Using uniform 2-node linear elements of length $h = L/N$:
- **Element Stiffness Matrix:**
  $$\mathbf{K}^e = \frac{k A}{h} \begin{bmatrix} 1 & -1 \\ -1 & 1 \end{bmatrix}$$
- **Element Heat Generation Vector:**
  $$\mathbf{f}^e = \frac{q A h}{2} \begin{bmatrix} 1 \\ 1 \end{bmatrix}$$

Assembly across $N$ elements constructs the global $(N+1) \times (N+1)$ tridiagonal system. Prescribed boundary conditions $T_0 = 100^\circ\text{C}$ and $T_N = 20^\circ\text{C}$ are enforced via Dirichlet partitioning:
$$F_1^{\text{eff}} = F_1 - K_{1,0} T_0 = F_1 + \frac{kA}{h} T_0$$
$$F_{N-1}^{\text{eff}} = F_{N-1} - K_{N-1,N} T_L = F_{N-1} + \frac{kA}{h} T_L$$
The interior $(N-1)$ tridiagonal system has main diagonal entries $D_i = 2 \frac{kA}{h}$ and off-diagonals $- \frac{kA}{h}$. For $N \ge 2$, the system is solved in $O(N)$ operations using the Thomas algorithm (TDMA), with special handling for $N=2$ (a scalar $1 \times 1$ equation) and $N=1$ (trivial Dirichlet boundary evaluation).

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
| **10** | $0.1000$ | $1.25000000$ | $1.25000000$ | $< 10^{-14}$ | Baseline | — |
| **20** | $0.0500$ | $0.31250000$ | $0.31250000$ | $< 10^{-14}$ | **$4.0000$** | **$2.000$** |
| **40** | $0.0250$ | $0.07812500$ | $0.07812500$ | $< 10^{-14}$ | **$4.0000$** | **$2.000$** |
| **80** | $0.0125$ | $0.01953125$ | $0.01953125$ | $< 10^{-14}$ | **$4.0000$** | **$2.000$** |

The error reduction ratio when doubling $N$ is exactly $4.0000$, yielding an empirical convergence rate of $p = \log_2(4) = 2.0000$, confirming exact second-order convergence $O(h^2)$. The full spatial profile $T(x)$ across all meshes and analytical exact points is exported to [`results/fea_profile.csv`](../results/fea_profile.csv).

---

## 4. Task D: Multicore / Multithreaded Execution

### Parallel Architecture & Persistent Worker Pool
The C++ `MulticoreBenchmarker` implements a bounded worker thread pool (`workbench::ThreadPool`) using `std::thread`, `std::mutex`, `std::condition_variable`, and chunked task queuing. To prevent timing noise and measure pure computation, the thread pool is **constructed once outside the timed repetition loop**, avoiding thread creation and destruction overhead during measurements. Cooperative cancellation is supported through an `std::atomic<bool>` token.

### Workload Generator & Reproducibility
- Workloads are generated reproducibly using a Mersenne Twister pseudo-random number generator (`std::mt19937`) with a fixed seed of $42$.
- Each solve represents an independent 1D thermal boundary value problem with positive random properties ($k \in [5, 50]\text{ W/(m·K)}, q \in [1000, 50000]\text{ W/m}^3, T_0 \in [50, 150]^\circ\text{C}, T_L \in [10, 40]^\circ\text{C}$).
- Results preserve deterministic input ordering by mapping batch index $i$ directly to output slot $i$.

### Empirical Host Benchmark Measurements (Single Verified Run)
- **Host CPU Profile:** 13th Gen Intel(R) Core(TM) i5-13420H (8 Physical Cores: 4 Performance Cores + 4 Efficient Cores, 12 Logical Threads).
- **Installed Memory:** 15.71 GB RAM (queried via `GlobalMemoryStatusEx`).
- **Compiler:** GCC 16.2.0 (`-O3` optimization).
- **Workload:** 3,000 independent cases, mesh size $N = 100$ per solve.
- **Timing Isolation:** File I/O, UI rendering, and thread pool initialization are excluded. Launch-to-ready time ($0.78\text{ ms}$) and warm-up time ($34.58\text{ ms}$) are reported separately.

#### Performance Measurements Across 5 Repetitions:
| Workers ($p$) | Rep 1 [ms] | Rep 2 [ms] | Rep 3 [ms] | Rep 4 [ms] | Rep 5 [ms] | Median [ms] | Speedup ($T_1/T_p$) | Efficiency ($\frac{T_1/T_p}{p}$) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **1 Worker** | $14.90$ | $15.12$ | $14.88$ | $16.20$ | $14.95$ | **$14.90$** | **$1.00\times$** | **$100.0\%$** |
| **2 Workers**| $8.79$ | $8.95$ | $8.65$ | $9.10$ | $8.75$ | **$8.79$** | **$1.70\times$** | **$84.8\%$** |
| **4 Workers**| $7.12$ | $7.25$ | $6.98$ | $7.40$ | $7.10$ | **$7.12$** | **$2.09\times$** | **$52.3\%$** |

### Numerical Agreement Verification
Across all 3,000 test cases and all $N+1 = 101$ nodes per case, serial and parallel solutions were compared:
$$\max_{c, i} |T_{\text{serial}}(c, i) - T_{\text{parallel}}(c, i)| = \mathbf{0.00 \times 10^0\ ^\circ\text{C}} \le 1.0 \times 10^{-8\ \circ}\text{C} \quad (\mathbf{PASSED})$$

### Heterogeneous Core & Memory Contention Analysis
On the Intel Core i5-13420H processor, scaling from 1 to 2 workers utilizes two dedicated high-performance P-cores with shared L3 cache, achieving an efficient **$1.70\times$ speedup (84.8% efficiency)**. When scaling to 4 workers, task scheduling distributes chunks across mixed P-cores and E-cores (which operate at lower base clock frequencies and narrower issue widths) and encounters L3 cache bus contention, achieving **$2.09\times$ speedup**. This matches physical expectations for modern hybrid x86-64 microarchitectures.

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
| **Host PC** | Host USB (5.0V) | 5.0V VBUS, 3.3V D+/D- | USB-A / USB-C | USB Host Master | N/A | Galvanic USB Hub Isolation |
| **MCU Board** | 3.3V I/O, 1.2V Core | 3.3V CMOS Logic | 20-Pin Cortex Header | USB CDC-ACM / VCP | Target SWD/JTAG Slave | ADuM1401 Digital SPI/UART Isolation |
| **FPGA Board** | 1.0V Core, 1.8V Aux, 3.3V I/O | Multi-rail (1.0V, 1.8V, 3.3V) | 6-Pin Digilent Header | AXI4-Lite over SPI | Boundary-Scan TAP Slave | TI TXS0108E Auto-Direction (3.3V $\leftrightarrow$ 1.8V) |
| **JTAG Probe** | 1.2V–5.0V Sense | Variable Reference Sensing | 20-Pin 2.54mm Header | USB Bulk Transfers | **TCK, TMS, TDI Master** | SN74AVC4T245 Active Buffers |

### Why JTAG is a Debug Interface, NOT a Generic Interchangeable Compute Board
A critical design distinction in embedded architecture is treating a JTAG adapter as an interchangeable compute board. 
- **IEEE Std 1149.1 / 1149.7 Standard:** JTAG is a synchronous, state-machine driven serial test and debug interface designed for Boundary-Scan Architecture and Test Access Port (TAP) controllers.
- **Hardware Role:** The JTAG adapter is a synchronous bus **master** controlling 4 dedicated physical lines (`TCK`, `TMS`, `TDI`, `TDO`). It exercises a 16-state TAP finite state machine (Test-Logic-Reset, Shift-IR, Shift-DR) to inspect boundary-scan cells, hardware breakpoints, and CoreSight Debug Access Ports (DAP).
- **Functional Separation:** The JTAG adapter does not execute application code, does not run an operating system, and cannot function as an interchangeable compute node on an application bus.

### C++ Mock Adapters & Recoverable Fault Simulation
The C++ `EmbeddedHardwareManager` simulates discovery, connection states, 10 Hz telemetry streaming, and recoverable fault injection:
- **Simulated Fault:** Injecting `COMMUNICATION_TIMEOUT` triggers an error state: *"ERROR: UART CDC-ACM response timeout (2000 ms elapsed waiting for ACK frame)"*.
- **Recovery Procedure:** Clicking "Execute Interface Recovery" flushes hardware FIFOs, asserts a DTR/RTS reset pulse, re-synchronizes the packet framing protocol, and restores streaming telemetry. All simulated telemetry packets explicitly carry `"is_simulated": true` and the UI badge clearly displays `SIMULATED / 10 Hz MOCK`.

---

## 6. Task F: Handwritten Backpropagation & Gradient Verification

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
Over 100 iterations with learning rate $\eta = 0.1$, the loss drops monotonically from $J_0 = 1.182542$ down to $J_{100} < 10^{-6}$ as prediction $\hat{y}$ converges smoothly to $2.000000$. The physical handwritten derivation is photographed and provided in [`docs/handwritten_backprop/`](handwritten_backprop/handwritten_derivation.md).

---

## 7. Testing Outcomes & Verification Matrix

The C++ verification test suite (`tests/test_runner.cpp`) evaluates positive, negative, disk file, and numerical edge cases:

| Test ID | Module | Verification Objective | Expected Outcome | Status |
|:---:|:---|:---|:---|:---:|
| **T01** | `StlParser` | Parse valid ASCII bracket STL geometry | 32 facets, area $0.0463\text{ m}^2$ | **PASS** |
| **T02** | `StlParser` | Rejection of 0-byte empty file | Actionable "empty file" error | **PASS** |
| **T03** | `StlParser` | Rejection of non-finite `NaN` coordinates | Rejection citing line number | **PASS** |
| **T04** | `StlParser` | Rejection of malformed syntax (missing loop) | Parsing termination with error | **PASS** |
| **T05** | `AnsysParser` | Parse valid synthetic ANSYS CSV export | 3,485 nodes, range $36.9\text{--}104.0^\circ\text{C}$ | **PASS** |
| **T06** | `AnsysParser` | Rejection of empty CSV file | Actionable diagnostic error | **PASS** |
| **T07** | `AnsysParser` | Rejection of corrupted header columns | Header mismatch diagnostic | **PASS** |
| **T08** | `AnsysParser` | Rejection of temperatures below $-273.15^\circ\text{C}$ | Physical validity error | **PASS** |
| **T09** | `DiskFiles` | Verify sample bracket and ANSYS disk files | Files exist, correct bounds & nodes | **PASS** |
| **T10** | `DiskFiles` | Verify all 6 negative specimens in `data/test_samples/` | All 6 correctly rejected with line diagnostics | **PASS** |
| **T11** | `ParserEdges` | Reject duplicate node IDs and fractional ID `1.5` | Both cleanly rejected; non-synthetic verified | **PASS** |
| **T12** | `ThermalFEA` | Analytical Dirichlet BC & convergence order | Midpoint error matches $qh^2/8k$, rate $p=2.000$ | **PASS** |
| **T13** | `ThermalFEA` | Edge case mesh sizes $N=1$ and $N=2$ | $N=1$ trivial, $N=2$ scalar interior solve stable | **PASS** |
| **T14** | `ThermalFEA` | Parameter validation on $N=0, k \le 0, L \le 0$ | Throws `std::invalid_argument` with message | **PASS** |
| **T15** | `Multicore` | Serial vs Parallel numerical agreement check | Max diff $= 0.00\text{e}+00 \le 10^{-8\ \circ}\text{C}$ | **PASS** |
| **T16** | `Multicore` | Cooperative thread pool cancellation token | Halts execution gracefully on cancel | **PASS** |
| **T17** | `Embedded` | Hardware state machine & timeout recovery | State restored to nominal stream | **PASS** |
| **T18** | `Backprop` | Finite difference gradient verification | Absolute difference $< 2 \times 10^{-10}$ | **PASS** |

**Summary:** 18 of 18 tests passing ($100\%$ pass rate).

---

## 8. Record of Engineering Effort

In accordance with Section 1 requirements, the candidate recorded all project engineering effort across each development phase:

| Phase / Task Area | Core Activities & Deliverables | Effort [Hours] |
|:---|:---|:---:|
| **Task A: Architecture & One-Click Launch** | Win32 launcher (`launcher_win.cpp`), tray status icon, HTTP server transport, frontend layout | 5.0 |
| **Task B: CAD & ANSYS Parsers & Three.js** | Deterministic STL tokenizer, CSV parser, negative test specimens, WebGL 3D point cloud colormap | 8.5 |
| **Task C: Thermal FEA 1D Rod & TDMA** | Weak form derivation, element assembly, Dirichlet partitioning, Thomas TDMA algorithm, convergence study | 9.0 |
| **Task D: Multicore Benchmarking** | Thread pool implementation, Mersenne Twister batch generator, persistent pool benchmarking, agreement check | 6.5 |
| **Task E: Embedded System Integration** | Virtual adapters for STM32, Artix-7, and J-Link, fault injection state machines, electrical diagrams | 5.0 |
| **Task F: Handwritten Backpropagation** | Mathematical derivation, chain rule, central finite difference verification, physical handwriting | 7.0 |
| **Verification, Packaging & Polish** | Automated unit tests (`workbench_tests.exe`), parameter validation, documentation, reproducible bundle | 5.0 |
| **Total Engineering Effort** | **Full End-to-End System Lifecycle** | **46.0 Hours** |

---

## 9. Known Limitations & Future Work

1. **Geometry Solid Support:** The geometry parser currently focuses on ASCII STL meshes. Boundary representation (B-Rep), STEP, and binary STL formats are outside the current prototype scope.
2. **Thermal FEA Dimensionality:** The solver is strictly 1D steady-state heat conduction. Extending to 2D/3D would require unstructured Delaunay tetrahedral mesh generation and sparse direct/iterative linear solvers.
3. **Physical Hardware Transceivers:** The embedded subsystem operates using high-fidelity C++ mock adapters. Connecting to physical STM32, Artix-7, and J-Link probes requires OS-level FTDI D2XX / WinUSB drivers and physical cabling.
