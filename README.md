# Unified Engineering Workbench (UEW)
**PASS Science Works — R&D Internship Assessment 2026–2027**  
**Candidate ID:** RA2311047010144  
*Common Internship Assignment | Target Platform: Windows 11 x64*  
**Effort Disclosure:** Target: 40–50 hours | **Recorded Actual Effort:** 46.5 hours (C++17 Core & Numerical Solvers: 22h, Interactive Frontend & Viewports: 12h, Windows Launcher & Packaging Pipeline: 7.5h, Empirical Verification & Technical Report: 5.0h)  
**🌐 Live Interactive UI:** [https://xoxosuriya.github.io/Unified-Engineering-Workbench/](https://xoxosuriya.github.io/Unified-Engineering-Workbench/)

---

## 1. Quick Start & One-Click Execution

### Standalone Zero-Prerequisite Execution (Evaluator Gate)
1. **Extract Archive:** Extract `RA2311047010144_Workbench.zip` into any folder on Windows 11 x64.
2. **Double-Click `Launch.exe`:** Double-click [`Launch.exe`](./Launch.exe) in the extracted root directory.
   - **No terminal, PowerShell, administrator privileges, package installations (Node/Python), or first-run builds are required.**
   - The native C++ server binds to `127.0.0.1:8080` (or the next available port) and opens your default browser immediately to the workbench.
   - A minimalist Windows system tray and status window is provided to manage the session.
   - To stop the application, either click **"Shutdown"** in the browser header, click **"Exit Workbench"** in the status dialog, or right-click the system tray icon and select **Exit**.

### One-Action Execution in VS Code or Antigravity
1. Open this repository folder in **VS Code** or **Antigravity**.
2. Navigate to the **Run & Debug** panel (`Ctrl + Shift + D`).
3. Select the run configuration: **`"Launch Unified Engineering Workbench (One-Action)"`**.
4. Press **`F5`** (or click Run). The packaged application starts immediately in one single action.
   - *Editor Extension Prerequisite:* Standard C/C++ extension (`ms-vscode.cpptools`) or CMake Tools if developing/building from source. Note: The standalone `Launch.exe` does **not** require the editor, extensions, or internet.

### Version Control Note
The initial commits were created in one batch after initial development, and all subsequent enhancements, bug fixes, security hardening, and empirical evidence runs are recorded as individual, transparent commits per item.

---

## 2. System Architecture

The Unified Engineering Workbench enforces a decoupled three-tier architecture:
- **Presentation Layer (`src/frontend/`):** Responsive Single-Page Application (SPA) designed with a modern dark-mode glassmorphic aesthetic. Includes interactive Three.js 3D viewports with OrbitControls, node raycast hover tooltips, and dynamic Chart.js plots. All libraries (`three.min.js`, `OrbitControls.js`, `chart.umd.min.js`) are vendor-bundled locally in `src/frontend/vendor/` with **zero internet dependencies**.
- **Transport Layer (`src/backend/src/http_server.cpp`):** High-speed, non-blocking WinSock2 HTTP server running on localhost. Handles file uploads, REST API queries (`/api/stl/parse`, `/api/ansys/parse`, `/api/fea/solve`, `/api/performance/run`, `/api/hardware/...`, `/api/backprop/...`), and CSV file downloads.
- **C++17 Numerical Core (`src/backend/src/`):** All application backend logic, numerical computations, finite-element formulations, file parsing, and multithreading are implemented strictly in **modern standard C++17** with zero external math or solver libraries.

```
+-----------------------------------------------------------------------------------------+
|                                    PRESENTATION TIER                                    |
|   Geometry View  |  ANSYS Thermal Data  |  Thermal FEA  |  Performance  |  Hardware HIL |
+--------------------------------------------+--------------------------------------------+
                                             | Local REST / JSON (127.0.0.1:8080)
+--------------------------------------------v--------------------------------------------+
|                          TRANSPORT TIER: WinSock2 HTTP Server                           |
+--------------------------------------------+--------------------------------------------+
                                             | C++17 Function Calls
+--------------------------------------------v--------------------------------------------+
|                                   C++17 CORE ENGINE                                     |
|  - StlParser: State-machine ASCII parser & boundary integrator                          |
|  - AnsysParser: Schema validator & temperature constraint verifier                      |
|  - ThermalFEMSolver1D: Galerkin 1D linear rod solver (Thomas TDMA)                      |
|  - ThreadPool & MulticoreBench: Bounded worker pool & reproducible batch tester         |
|  - EmbeddedHardware: Mock adapters (MCU, FPGA, JTAG) with state machine recovery        |
|  - BackpropEngine: Analytical forward/backward pass & finite difference checker         |
+-----------------------------------------------------------------------------------------+
```

---

## 3. Supported File Formats & Validated Limits

| Format | File Extension | Schema / Header | Tested File Size Limit | Diagnostic Handling |
|:---|:---:|:---|:---:|:---|
| **ASCII STL Geometry** | `.stl` | `solid <name> ... facet normal ... outer loop ... 3x vertex ... endloop ... endfacet ... endsolid` | **50 MB** cap (benchmarked to 270,000 facets / 38.79 MB; see `results/file_limits.csv`) | Rejects 0-byte files, binary STL headers, non-finite values (`NaN`, `±Inf`), and malformed facets with exact line numbers. |
| **ANSYS Nodal Temperatures** | `.csv` | `node_id,x_m,y_m,z_m,temperature_C` (labeled synthetic in header) | **50 MB** cap (benchmarked to 500,000 nodes / 13.84 MB; see `results/file_limits.csv`) | Validates header format, positive integer IDs, finite coordinates, and rejects temperatures below absolute zero ($-273.15^\circ\text{C}$) or $> 5000^\circ\text{C}$. |

Sample files supplied with the distribution:
- [`data/sample_bracket.stl`](./data/sample_bracket.stl): Analytical CAD model of a structural mounting bracket with dual stiffener ribs (32 facets, area $0.0463\text{ m}^2$ ($463.00\text{ cm}^2$ / $46,300\text{ mm}^2$), bounding box $100 \times 80 \times 100\text{ mm}$, open surface shell mesh, units in meters). Full provenance documented in [`data/provenance.md`](./data/provenance.md).
- [`data/ansys_thermal_synthetic.csv`](./data/ansys_thermal_synthetic.csv): Synthetic steady-state nodal temperature field on the bracket geometry (3,485 nodes, range $36.87^\circ\text{C}$ to $104.00^\circ\text{C}$, average $65.08^\circ\text{C}$). Clearly disclosed as synthetic.
- Negative test cases in [`data/test_samples/`](./data/test_samples/): `empty.stl`, `corrupted_syntax.stl`, `nan_coords.stl`, `missing_columns.csv`, `sub_absolute_zero.csv`, `negative_coords.stl`.

---

## 4. Key Engineering Results Summary

### Task C: Thermal FEA & Mesh Convergence
- Solves $-d/dx(kA dT/dx) = qA$ for uniform rod ($L=1\text{ m}, A=10^{-4}\text{ m}^2, k=10\text{ W/(m·K)}, q=10000\text{ W/m}^3, T_0=100^\circ\text{C}, T_L=20^\circ\text{C}$).
- Exact analytical solution: $T_{\text{exact}}(x) = 100 - 80x + 500x(1-x)$.
- **Nodal Exactness:** Finite element nodal temperatures match the analytical solution to machine precision ($< 10^{-14\ \circ}\text{C}$ error).
- **Midpoint Error:** Evaluated at element midpoints via linear interpolation, matching theoretical error $\frac{qh^2}{8k}$ with exact second-order $O(h^2)$ convergence ($p = 2.000$):
  - $N=10$: $1.2500^\circ\text{C}$
  - $N=20$: $0.3125^\circ\text{C}$ (ratio $4.00$)
  - $N=40$: $0.0781^\circ\text{C}$ (ratio $4.00$)
  - $N=80$: $0.0195^\circ\text{C}$ (ratio $4.00$)
- Spatial $T(x)$ profile exported across all meshes in [`results/fea_profile.csv`](./results/fea_profile.csv).

### Task D: Multicore Execution & Numerical Agreement
- Persistent worker pool testing 20,000 independent batch solves across $p \in \{1, 2, 4\}$ threads with mesh $N=200$ and fixed seed $42$ on an 8-core host CPU (GNU 16.2.0 `-O3`).
- **Numerical Agreement:** Serial and parallel results match identically across all nodes:
  $$\max |T_{\text{serial}} - T_{\text{parallel}}| = \mathbf{0.00 \times 10^0\ ^\circ\text{C}} \le 1.0 \times 10^{-8\ \circ}\text{C} \quad (\mathbf{PASSED})$$
- 5 measured repetitions, median times (**276.63 ms** on 1 worker, **153.61 ms** on 2 workers, **85.04 ms** on 4 workers), speedup (**$1.8009\times$** on 2 workers, **$3.2531\times$** on 4 workers), and efficiency recorded in [`results/multicore_scaling.csv`](./results/multicore_scaling.csv).

### Task E: Embedded Integration Architecture
- Models a modular testbench for an STM32H7 MCU, Xilinx Artix-7 FPGA, and Segger J-Link probe.
- Demonstrates star grounding, level shifting (TI TXS0108E), and explains why JTAG is a dedicated synchronous TAP controller master, not an interchangeable general-purpose compute node.
- Live 10 Hz telemetry streaming, recoverable timeout/CRC fault injection, and automated link recovery.

### Task F: Simple Handwritten Backpropagation
- Sample $x=1, y=2$, initial $w=0.5, b=0, v=1, c=0, \eta=0.1$.
- Step 0 forward: $z=0.5, h=0.46211716, \hat{y}=0.46211716$, Loss $J = 1.18254182$.
- Analytical derivatives: $\frac{\partial J}{\partial c} = -1.53788284, \frac{\partial J}{\partial v} = -0.71068205, \frac{\partial J}{\partial w} = -1.20946448, \frac{\partial J}{\partial b} = -1.20946448$.
- Simultaneous Step 1 update: $w_1 = 0.62094645, b_1 = 0.12094645, v_1 = 1.07106820, c_1 = 0.15378828$.
- **Central Finite Difference Check ($\epsilon = 10^{-6}$):** Verified across all 4 parameters with error $< 1.13 \times 10^{-10} \le 10^{-5}$ (`PASSED`).
- Typed 2-page derivation provided in [`docs/handwritten_backprop/`](./docs/handwritten_backprop/handwritten_derivation.md) (photos of handwritten pages to be photographed and added by candidate).

---

## 5. Source Build & Testing Instructions

To build the project from source using CMake and any standard C++17 compiler (GCC / MinGW / Clang / MSVC):

### Build Steps:
```powershell
# 1. Configure build directory
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 2. Build all targets (Workbench Core, Launcher, and Test Suite)
cmake --build build -j
```

### Running Automated Test Suite:
```powershell
# Run the 23-test positive, negative, disk file, and numerical accuracy suite
./workbench_tests.exe
```
*Expected Output:*
```
Test Summary: 23 Passed, 0 Failed (100% Pass Rate)
```

### Command-Line CLI Execution:
```powershell
# Solve 1D FEA and output convergence table directly
./workbench_backend.exe --fea

# Run multicore benchmark directly
./workbench_backend.exe --bench

# Run backpropagation study directly
./workbench_backend.exe --backprop
```

---

## 6. Directory Structure
```
RA2311047010144_Workbench/
├── Launch.exe                       # Standalone zero-prerequisite Windows launcher
├── workbench_backend.exe           # CLI runner & headless server executable
├── workbench_tests.exe             # 23-test automated unit & verification executable
├── CMakeLists.txt                   # Standard root CMake build configuration
├── DEPENDENCIES.md                  # Third-party inventory, versions & MIT license texts
├── README.md                        # Primary project documentation
├── .gitignore                       # Clean repository exclusions
├── .vscode/
│   ├── launch.json                  # One-action editor launch configuration (gdb)
│   └── tasks.json                   # Build and test tasks with CMake configure
├── src/
│   ├── backend/                     # C++17 Core Engine (Separated from presentation)
│   │   ├── include/                 # Header files (parsers, solvers, thread pool, hardware)
│   │   ├── src/                     # C++ implementations
│   │   └── launcher/                # Win32 GUI Launch.exe implementation
│   └── frontend/                    # Single-Page Web Workbench Interface
│       ├── index.html               # Main dashboard HTML
│       ├── css/style.css            # Dark-mode glassmorphic styling
│       ├── js/                      # Modular view controllers
│       └── vendor/                  # Bundled offline dependencies (Three.js, Chart.js)
├── data/
│   ├── sample_bracket.stl           # ASCII STL geometry with provenance (32 facets)
│   ├── ansys_thermal_synthetic.csv  # Synthetic ANSYS nodal temperature export (3,485 nodes)
│   ├── test_samples/                # Negative test specimens (empty, NaN, corrupt, bounds)
│   └── provenance.md                # Provenance and schema documentation
├── tests/
│   ├── test_runner.cpp              # 23-test automated unit and numerical verification suite
│   └── smoke_routes.ps1             # Automated dynamic-port HTTP route smoke test
├── results/
│   ├── fea_convergence.csv          # Clean FEA convergence data
│   ├── fea_profile.csv              # Spatial T(x) temperature profile across all meshes
│   ├── multicore_scaling.csv        # Clean multicore timing repetitions & speedup
│   ├── backprop_loss.csv            # Clean 100-step training loss history
│   ├── file_limits.csv              # Measured parser file size and timing limits
│   ├── environment.json             # Benchmark environment run metadata
│   └── plots/                       # Labelled vector plots (SVG)
└── docs/
    ├── Workbench_Report.md          # Comprehensive technical engineering report
    ├── AI_LOG.md                    # Transparent AI-assistance log & critique records
    ├── OWNERSHIP.md                 # Statement of ownership and reused code disclosures
    ├── DEMONSTRATION_WALKTHROUGH.md # Verification walkthrough logs
    ├── workbench_demo.webp          # Demonstration recording (14.92 MB silent screen capture of older build)
    └── handwritten_backprop/        # Handwritten derivation reference document (photos to be added by candidate)
```

---

## 7. Known Limitations
1. **Geometry Solid Formats:** ASCII STL meshes are fully supported; binary STL, STEP, and native proprietary CAD formats are outside prototype scope.
2. **Thermal Solver Scope:** 1D steady-state linear elements are implemented. 2D/3D geometries require tetrahedral volume meshing and sparse iterative solvers.
3. **Hardware Emulation:** The embedded hardware workbench utilizes C++ software mock adapters. Physical instrument interfacing requires board-specific USB/JTAG hardware transceivers and physical wiring.
