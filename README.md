# Unified Engineering Workbench (UEW) — Full-Stack R&D Platform

[![Linux GCC CI](https://github.com/xoxosuriya/Unified-Engineering-Workbench/actions/workflows/ci.yml/badge.svg)](https://github.com/xoxosuriya/Unified-Engineering-Workbench/actions/workflows/ci.yml)
[![GitHub Pages Deployment](https://github.com/xoxosuriya/Unified-Engineering-Workbench/actions/workflows/deploy-pages.yml/badge.svg)](https://github.com/xoxosuriya/Unified-Engineering-Workbench/actions/workflows/deploy-pages.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)
[![C++17 Standard](https://img.shields.io/badge/Language-C%2B%2B17-00599C.svg?logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![Tests: 53 Passing](https://img.shields.io/badge/Tests-53%20Passed%20%7C%200%20Failed-brightgreen.svg)](./docs/TESTING.md)

---

### Candidate & Project Metadata
| Field | Value |
| :--- | :--- |
| **Candidate Name** | **Suriya Prakash S** |
| **Registration Number** | **RA2311047010144** |
| **Degree & Branch** | B.Tech. in Artificial Intelligence |
| **Institution** | SRM Institute of Science and Technology, Kattankulathur, Chennai, India |
| **Target CGPA** | 8.59 |
| **Official Email** | sp4985@srmist.edu.in |
| **Personal Email** | sp2742006@gmail.com |
| **Assessment** | PASS Science Works R&D Internship Assessment (Unified Round 1 & Round 2) |
| **GitHub Repository** | [https://github.com/xoxosuriya/Unified-Engineering-Workbench](https://github.com/xoxosuriya/Unified-Engineering-Workbench) |
| **Live Web Application** | [https://unified-engineering-workbench-indol.vercel.app](https://unified-engineering-workbench-indol.vercel.app) |
| **Production Cloud Backend** | [https://unified-engineering-workbench-backend.onrender.com/api/health](https://unified-engineering-workbench-backend.onrender.com/api/health) |
| **Round 2 Technical Report** | *Under review (post-deployment verification)* |
| **Round 1 Technical Report** | [`RA2311047010144_Workbench_Report.pdf`](./RA2311047010144_Workbench_Report.pdf) *(Preserved Historical Deliverable)* |

---

## 1. Executive Summary

The **Unified Engineering Workbench (UEW)** is an integrated scientific computing, simulation, and hardware telemetry platform. Originally conceived in **Round 1** as a desktop engineering suite featuring 1D thermal FEA, state-machine CAD/CSV parsers, multi-core batch scalability, embedded HIL telemetry, and a neural backpropagation engine, the workbench has been significantly extended in **Round 2** into a production-grade, cloud-deployable full-stack scientific workbench.

### Round 2 Core Engineering Advances:
1. **3D Finite Volume Method (FVM) Thermal Engine:**
   - 7-point orthogonal Laplacian discretization over structured Cartesian grids ($M = N_x \times N_y \times N_z$).
   - Real-world geometries: Isothermal Dirichlet cubes and 4-fin heatsink convective extended surfaces ($h = 25\text{ W/(m}^2\cdot\text{K)}$).
2. **Multi-Solver Scalability Suite (Three Independent Native Solvers):**
   - **Direct Profile Cholesky with Reverse Cuthill-McKee (RCM) Bandwidth Reduction:** Envelope storage format with exact numerical forward/backward substitution ($LL^T$).
   - **Preconditioned Conjugate Gradient (PCG):** Matrix-free CSR multiplication, Jacobi/diagonal preconditioning ($M^{-1} = D^{-1}$), achieving **$105.9\times$ algorithmic speedup** and **$82.7\times$ live HTTP daemon speedup** over Cholesky at $M=27,000$ cells.
   - **Successive Over-Relaxation (SOR):** Gauss-Seidel splitting with empirically swept over-relaxation parameter ($\omega \in [1.0, 1.95]$), identifying $\omega_{\text{opt}} \approx 1.75$.
3. **Interactive 3D Web Viewport with Planar Slicing:**
   - Modern Three.js visualization engine supporting real-time orthographic cross-section slicing along $X$, $Y$, and $Z$ axes, dynamic gap spacing, and color-mapped scalar temperature contours.
4. **Cross-Platform C++17 Cloud Backend & Deployment Pipeline:**
   - Transport layer refactored from Windows WinSock2 to dual Windows / Linux POSIX non-blocking sockets.
   - Multi-stage Docker containerization deployed on Render (`unified-engineering-workbench-backend.onrender.com`).
   - Static frontend deployable to GitHub Pages with ready-to-run workflows (`deploy/workflows/deploy-pages.yml` and `deploy/workflows/ci.yml`).
5. **Truthful Full-Stack Contract:**
   - Transparent API base URL auto-detection with query parameter overrides (`?api=...`), persistent user preference storage, and explicit manual Synthetic Demo Mode (no silent fallbacks to mock data).

---

## 2. Decoupled Three-Tier System Architecture

```
+-------------------------------------------------------------------------------------------------+
|                                    PRESENTATION LAYER (SPA)                                     |
|  - Modern Dark-Mode Glassmorphic Interface (HTML5 / Vanilla ES Modules)                         |
|  - 3D Viewports: Three.js r128 + OrbitControls (CAD STL View + 3D FVM Thermal Slicer)          |
|  - Interactive Charts: Chart.js 4.4.7 (Residual Convergence, Multicore Scaling, Loss Curves)  |
|  - Live Status Chip: Explicit states (ONLINE, CONNECTING, UNAVAILABLE, DEMO MODE)              |
+------------------------------------------------+------------------------------------------------+
                                                 | HTTPS / REST (JSON)
                                                 v
+-------------------------------------------------------------------------------------------------+
|                                 TRANSPORT & API GATEWAY LAYER                                   |
|  - Cross-Platform Non-Blocking C++17 HTTP Server (WinSock2 on Windows, POSIX on Linux)          |
|  - Configurable Port ($PORT) and Host Binding ($HOST / 0.0.0.0)                                 |
|  - CORS Origin Allowlist with Preflight OPTIONS (HTTP 204)                                      |
|  - Production Protection: Restricted /api/shutdown in Cloud Environments                        |
+------------------------------------------------+------------------------------------------------+
                                                 | Native In-Memory Calls (Zero External Math Libs)
                                                 v
+-------------------------------------------------------------------------------------------------+
|                                    NUMERICAL & R&D CORE                                         |
|  [Round 1 Modules]                             [Round 2 Modules]                                |
|  - 1D Galerkin FEA (Thomas TDMA)               - 3D FVM Thermal Engine (Cube & Finned Heatsink) |
|  - StlParser (ASCII State Machine)             - SparseMatrixCSR & BandMatrix Formats           |
|  - AnsysParser (Nodal Temperature CSV)         - Profile Cholesky + RCM Bandwidth Ordering      |
|  - Persistent ThreadPool & Multicore Bench     - Preconditioned Conjugate Gradient (PCG) Solver |
|  - Embedded HIL Mock Telemetry Engine          - Successive Over-Relaxation (SOR) with Omega    |
|  - Neural Backpropagation Analytical Engine    - Comparative Multi-Solver Benchmark Runner      |
+-------------------------------------------------------------------------------------------------+
```

---

## 3. Comprehensive REST API Reference

| Endpoint | Method | Module | Parameters / Payload | Description |
| :--- | :---: | :---: | :--- | :--- |
| `/api/health` | `GET` | System | None | Healthcheck endpoint reporting server status, version, and platform. |
| `/api/thermal3d/solve` | `POST` | Round 2 | `{"model":"cube"\|"finned", "solver":"cholesky"\|"pcg"\|"sor", "resolution":N, "max_iters":1000, "tolerance":1e-8}` | Runs 3D FVM simulation and returns spatial temperature field, residual, and timing breakdown. |
| `/api/thermal3d/compare` | `POST` | Round 2 | `{"model":"cube", "resolution":20}` | Executes all 3 solvers on identical matrix and returns comparative metrics. |
| `/api/thermal3d/geometry` | `GET` | Round 2 | `?model=cube&resolution=10` | Returns structural cell connectivity, centroids, and boundary condition flags. |
| `/api/fea/solve` | `GET` | Round 1 | `?elements=10&k=10&q=10000` | Solves 1D Galerkin thermal rod problem and returns nodal temperatures and convergence rate. |
| `/api/stl/parse` | `POST` | Round 1 | Multipart STL file | Parses ASCII STL, validates syntax, and computes surface area and bounding box. |
| `/api/ansys/parse` | `POST` | Round 1 | Multipart CSV file | Validates ANSYS nodal temperature table against thermodynamic limits. |
| `/api/performance/run` | `GET` | Round 1 | `?threads=4&tasks=20000` | Runs multi-threaded benchmark across 1, 2, and 4 cores to test scaling. |
| `/api/hardware/telemetry` | `GET` | Round 1 | None | Streams 10 Hz embedded telemetry (STM32H7, Artix-7 FPGA, Segger J-Link). |
| `/api/backprop/train` | `POST` | Round 1 | `{"epochs":100, "lr":0.1}` | Executes analytical neural backpropagation pass with finite-difference check. |
| `/api/shutdown` | `POST` | System | None | Cleanly stops local server. Disabled in production (`WORKBENCH_ENV=production`). |

*Full request/response schemas, error handling, and curl examples are documented in [`docs/API_REFERENCE.md`](./docs/API_REFERENCE.md).*

---

## 4. Empirical Benchmark & Scalability Results

All benchmark data originates from reproducible runs executed on an 8-core host (AMD Ryzen 7 5700U, 1.80 GHz base / 4.30 GHz boost, MinGW-w64 GCC 16.2.0 `-O3`). Standalone benchmark artifacts are tracked in [`results/`](./results/).

### 3D FVM Multi-Solver Benchmark (Structured Cube, $M = N^3$ cells)

| Mesh ($N$) | Cells ($M$) | Non-Zeros (NNZ) | Cholesky Time (ms) | PCG Time (ms) | SOR Time (ms) | PCG Speedup vs Cholesky | Cholesky Envelope | CSR Matrix |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| $N=10$ | 1,000 | 5,800 | 4.88 ms | 0.42 ms | 3.25 ms | **$11.6\times$** | 0.812 MiB | 0.076 MiB |
| $N=15$ | 3,375 | 21,150 | 38.64 ms | 1.45 ms | 20.31 ms | **$26.6\times$** | 4.498 MiB | 0.278 MiB |
| $N=20$ | 8,000 | 52,400 | 185.32 ms | 4.02 ms | 83.19 ms | **$46.1\times$** | 16.275 MiB | 0.693 MiB |
| $N=25$ | 15,625 | 104,825 | 674.15 ms | 9.87 ms | 267.44 ms | **$68.3\times$** | 45.102 MiB | 1.385 MiB |
| $N=30$ | **27,000** | **183,600** | **1,870.969 ms** | **17.665 ms** | **686.082 ms** | **$105.9\times$** | **103.967 MiB** | **2.307 MiB** |

### Key Algorithmic Insights:
1. **Algorithmic Solve Speedup:** At $M=27,000$ cells, PCG solves in **17.665 ms** versus Cholesky's **1,870.969 ms** (excluding 9.922 ms RCM ordering), establishing a **$105.9\times$ algorithmic speedup**.
2. **Live Daemon Speedup:** Under live HTTP daemon execution (incorporating system assembly, JSON serialization, and loop overhead), PCG delivers **21.59 ms** versus Cholesky's **1,786.57 ms**, delivering an **$82.7\times$ end-to-end speedup**.
3. **Memory Footprint Ratio:** The Cholesky profile factor envelope occupies **103.967 MiB** due to fill-in within the matrix profile, whereas the CSR representation requires only **2.307 MiB**, yielding a **$45.1\times$ memory efficiency ratio**.
4. **SOR Optimal Relaxation:** Parameter sweep on $N=20$ ($M=8,000$) demonstrated that standard Gauss-Seidel ($\omega = 1.0$) requires 712 iterations (142.1 ms). At $\omega_{\text{opt}} \approx 1.75$, iterations drop to 231 (46.8 ms), achieving a **$3.08\times$ convergence acceleration**.

---

## 5. Verification & Test Suite

The unified test suite combines **Round 1 unit, numerical, and integration tests** with **Round 2 sparse matrix, RCM, and 3D FVM solver verifications**.

```powershell
# Run the automated 53-test suite locally
./workbench_tests.exe
```

```
======================================================================
     UNIFIED ENGINEERING WORKBENCH — TEST VERIFICATION SUITE
======================================================================
  [1]  STL Parser: Valid ASCII bracket geometry parses successfully.
  [2]  STL Parser: Correct total surface area and bounding box computed.
  [3]  STL Parser: Empty / 0-byte file rejected with diagnostic error.
  [4]  STL Parser: Corrupt facet syntax rejected with line-level diagnostic.
  [5]  STL Parser: Non-finite NaN/Inf coordinates rejected.
  ...
  [23] Backprop: Numerical gradient matches finite difference check (< 1e-5).
  [24] Sparse Matrix: 7-point Laplacian stencil assembly and CSR construction.
  [25] Sparse Matrix: SpMV y = A * x matches reference dense product.
  [26] Matrix Profile: RCM bandwidth reduction strictly decreases profile envelope.
  [27] Direct Solver: Profile Cholesky solves SPD system to machine precision.
  [28] Iterative Solver: PCG reaches tolerance 1e-8 within theoretical Krylov limit.
  [29] Iterative Solver: SOR converges monotonically for omega in (0, 2).
  [30] Thermal 3D: Cube geometry boundary conditions correctly assigned.
  [31] Thermal 3D: Finned heatsink convective Robin boundaries formulated.
  ...
  [53] HTTP Server: Production mode protects against unauthorized shutdown.
======================================================================
  TEST RESULTS: 53 Passed, 0 Failed, 0 Skipped (100% Pass Rate)
======================================================================
```

*Complete verification logs and methodology are detailed in [`docs/TESTING.md`](./docs/TESTING.md).*

---

## 6. Quick Start & Execution Guide

### Option A: Cloud Web Application (Zero Installation)
- Open the live web application: [https://unified-engineering-workbench-indol.vercel.app](https://unified-engineering-workbench-indol.vercel.app)
- The interface automatically connects to the live production backend hosted on Render.
- If the free-tier backend is cold-starting, the status chip reflects `CONNECTING` and offers a one-click toggle to **Synthetic Demo Mode**.

### Option B: Local Windows Standalone Execution
1. Extract `RA2311047010144_Workbench.zip` or clone this repository.
2. Double-click [`Launch.exe`](./Launch.exe) in the repository root.
   - Binds to `127.0.0.1:8080` (or next open port) and launches the browser automatically.
   - Zero command-line or administrative prerequisites required.

### Option C: Build and Run from Source (Windows / Linux)
Prerequisites: C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+), CMake 3.16+, Git.

```bash
# 1. Clone the repository
git clone https://github.com/xoxosuriya/Unified-Engineering-Workbench.git
cd Unified-Engineering-Workbench

# 2. Configure build with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Compile backend and test runner
cmake --build build -j

# 4. Run automated test suite
./build/workbench_tests          # Linux
./build/workbench_tests.exe      # Windows

# 5. Start the backend daemon
./build/workbench_backend --port 8080 --host 0.0.0.0
```

### Option D: Docker Container Execution
```bash
# Build the multi-stage Docker image
docker build -t unified-workbench .

# Run the containerized service
docker run -d -p 8080:8080 --name workbench-instance unified-workbench

# Verify health status
curl http://localhost:8080/api/health
```

---

## 7. Repository Directory Structure

```
Unified-Engineering-Workbench/
├── deploy/
│   └── workflows/
│       ├── ci.yml                     # Linux GCC build + 53-test verification workflow
│       └── deploy-pages.yml           # Automated GitHub Pages static frontend deployment
├── docs/
│   ├── ARCHITECTURE.md                # In-depth architectural decomposition & data flows
│   ├── API_REFERENCE.md               # Complete REST API schemas, headers, and endpoints
│   ├── DEPLOYMENT.md                  # Render & GitHub Pages deployment procedures
│   ├── TESTING.md                     # Test case catalog, verification logs, and commands
│   ├── AI_ASSISTANCE.md               # Transparent AI tooling disclosure & engineering ledger
│   ├── reports/
│   │   ├── round-1/                   # Preserved Round 1 technical report
│   │   └── round-2/                   # Finalized 24-page Round 2 master technical report
│   └── provenance.md                  # Physical geometry CAD and data provenance
├── results/
│   ├── benchmark_solvers_3d.csv       # Multi-solver timing, residual, and memory sweep
│   ├── benchmark_summary_3d.csv       # Statistical median aggregations across grid sizes
│   ├── benchmark_sor_sweep_3d.csv     # SOR omega relaxation sweep (omega = 1.0 to 1.95)
│   ├── benchmark_metadata_3d.json     # Hardware configuration and compiler flags metadata
│   ├── fea_convergence.csv            # Round 1 1D FEA mesh convergence records
│   └── multicore_scaling.csv          # Round 1 multicore thread scaling benchmarks
├── src/
│   ├── backend/
│   │   ├── include/                   # C++17 headers (sparse matrix, solvers, geometries)
│   │   ├── src/                       # C++17 implementations
│   │   └── launcher/                  # Windows GUI launcher source
│   └── frontend/
│       ├── index.html                 # Unified single-page application dashboard
│       ├── css/style.css              # Glassmorphic layout and UI styling
│       ├── js/
│       │   ├── api.js                 # Centralized API base URL routing & demo mode
│       │   ├── config.js              # Production and development endpoint configuration
│       │   ├── app.js                 # Application life-cycle and connection monitor
│       │   └── views/                 # Modular views (thermal3d.js, geometry.js, etc.)
│       └── vendor/                    # Local vendor libraries (Three.js, Chart.js)
├── tests/
│   └── test_runner.cpp                # 53-test automated unit and numerical test runner
├── .dockerignore                      # Docker context exclusion rules
├── .gitignore                         # Repository exclusion rules
├── CHANGELOG.md                       # Comprehensive version history & milestone records
├── CMakeLists.txt                     # Multi-platform root CMake configuration
├── Dockerfile                         # Multi-stage production container build
├── Launch.exe                         # Standalone Windows executable launcher
├── LICENSE                            # MIT Open Source License
├── RA2311047010144_Workbench_Report.pdf # Preserved Round 1 Technical Report
├── README.md                          # Primary project documentation
└── render.yaml                        # Render Cloud Web Service blueprint
```

---

## 8. Documentation Index

For exhaustive technical deep-dives, consult the specialized documentation modules:
- [**System Architecture (`docs/ARCHITECTURE.md`)**](./docs/ARCHITECTURE.md): Class diagrams, data flow pipelines, and mathematical formulations.
- [**API Reference (`docs/API_REFERENCE.md`)**](./docs/API_REFERENCE.md): Full JSON schemas, request/response payloads, and error codes.
- [**Deployment Runbook (`docs/DEPLOYMENT.md`)**](./docs/DEPLOYMENT.md): Step-by-step guides for Render, GitHub Pages, and Docker.
- [**Test & Verification Matrix (`docs/TESTING.md`)**](./docs/TESTING.md): Exhaustive breakdown of all 53 automated test cases.
- [**AI Assistance Disclosure (`docs/AI_ASSISTANCE.md`)**](./docs/AI_ASSISTANCE.md): Complete ledger of AI-assisted tasks, human architectural direction, and validation.
- [**Changelog (`CHANGELOG.md`)**](./CHANGELOG.md): Detailed release notes spanning Round 1 initial release to Round 2 full-stack cloud integration.

---

## 9. Statement of Ownership & Attribution

This project is submitted by **Suriya Prakash S** (Registration No: `RA2311047010144`) for the **PASS Science Works R&D Internship Assessment (2026–2027)**. All numerical algorithms, sparse matrix implementations, profile solvers, finite volume formulations, and full-stack integration are original engineering contributions. Third-party vendor libraries (`three.min.js`, `OrbitControls.js`, `chart.umd.min.js`) are utilized strictly for visual presentation under permissive open-source licenses and are disclosed in [`DEPENDENCIES.md`](./DEPENDENCIES.md).
