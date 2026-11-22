# Unified Engineering Workbench (UEW)

[![C++17 Standard](https://img.shields.io/badge/Language-C%2B%2B17-00599C.svg?logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)
[![Tests: 53 Passing](https://img.shields.io/badge/Tests-53%20Passed%20%7C%200%20Failed-brightgreen.svg)](./docs/TESTING.md)

**Live Demo:** https://unified-engineering-workbench-indol.vercel.app/ *(Better experience on desktop)*

---

## 1. Overview

**Unified Engineering Workbench (UEW)** is a high-performance scientific simulation and visualization platform for computational thermal engineering. The workbench couples a high-throughput, native **C++17 finite volume numerical engine** with a modern, interactive **Three.js WebGL 3D visualization viewport**. 

The system solves 3D steady-state heat conduction across complex geometries (including isothermal multi-face cubes and convective finned heat-sink arrays) and provides real-time multi-axis planar cross-section slicing, contour visualization, and multi-solver comparative performance analytics.

---

## 2. Key Features

- **3D Finite Volume Method (FVM) Discretization:**
  - 7-point orthogonal Laplacian stencil on Cartesian grids ($M = N_x \times N_y \times N_z$).
  - Built-in geometry models: Isothermal Dirichlet cubes and 4-fin convective heat-sink extended surfaces ($h_c = 25\text{ W/(m}^2\cdot\text{K)}$, $T_\infty = 25^\circ\text{C}$).
  - Conservative finite volume flux formulations satisfying machine-precision energy balances.

- **Native Multi-Solver Suite (Three Solvers):**
  - **Direct Profile Cholesky with Reverse Cuthill-McKee (RCM):** Active envelope matrix profile storage with numerical forward/backward substitution ($L L^T$), dramatically reducing fill-in.
  - **Preconditioned Conjugate Gradient (PCG):** Sparse matrix-free CSR operations with Jacobi diagonal preconditioning ($M^{-1} = D^{-1}$), yielding over $100\times$ speedup at large grid sizes.
  - **Successive Over-Relaxation (SOR):** Gauss-Seidel matrix splitting with dynamic over-relaxation parameter ($\omega \in [1.0, 1.95]$) and spectral radius acceleration.

- **Interactive 3D WebGL Visualization Viewport:**
  - Real-time voxel rendering powered by Three.js.
  - Multi-axis planar cross-section slicing ($X\text{-Cut}$, $Y\text{-Cut}$, $Z\text{-Cut}$) with adjustable voxel gaps to inspect interior thermal gradients.
  - Interactive camera presets (Isometric, Front, Top), mesh wireframe toggle, and perceptual thermal colormaps.

- **Full-Stack Truthful Connectivity:**
  - Real-time backend status health checking (`/api/thermal3d/health`).
  - Transparent error diagnostics, automatic reconnect monitoring, and optional synthetic demo simulation mode.

---

## 3. 3D Simulation & Visualization Workflow

```
[Select Geometry Model]  -->  [Choose Mesh Resolution]  -->  [Select Linear Solver]
  - Model A: Solid Cube         - N = 10 (1,000 cells)        - Profile Cholesky (RCM)
  - Model C: Finned Sink        - N = 20 (8,000 cells)        - PCG (Jacobi Precond)
                                - N = 30 (27,000 cells)       - SOR (Omega sweep)
                                           |
                                           v
[Interactive 3D Analysis]  <--  [JSON REST Transmission]  <--  [C++17 Numerical Engine]
  - Multi-Axis Slicing (X/Y/Z)    - Node Coordinates             - FVM Matrix Assembly
  - Thermal Contour Gradient      - Temperatures [deg C]         - RCM Ordering & Solvers
  - Residuals & Solve Timings     - Convergence Residual         - Energy Balance Check
```

1. **Model Selection:** Choose between the canonical solid conducting cube and the 4-fin heat sink.
2. **Resolution & Parameter Selection:** Choose mesh resolution ($N=10$, $N=20$, $N=30$) and solver parameters (convergence tolerance, maximum iterations).
3. **Execution:** The frontend dispatches a structured `POST /api/thermal3d/solve` request to the C++ backend.
4. **Solving:** The C++ daemon executes assembly, bandwidth minimization, and numerical solution, returning nodal temperatures, residuals, and timing breakdowns.
5. **Interactive Inspection:** The frontend renders the 3D voxel heat contour mesh with orbit, pan, zoom, and live planar slicing controls.

---

## 4. System Architecture

The workbench is structured into three decoupled tiers:

```
+-------------------------------------------------------------------------------------------------+
|                                    PRESENTATION LAYER (SPA)                                     |
|  - Modern Dark-Mode Glassmorphic Interface (HTML5 / Vanilla ES Modules)                         |
|  - 3D Viewports: Three.js r128 + OrbitControls (3D FVM Thermal Slicer & CAD STL Viewer)         |
|  - Real-Time Charts: Chart.js (Multi-solver runtime comparisons & residual histories)           |
|  - Status Indicator: Synchronized live backend health state (ONLINE / UNAVAILABLE / DEMO)       |
+------------------------------------------------+------------------------------------------------+
                                                 | HTTPS / REST (JSON)
                                                 v
+-------------------------------------------------------------------------------------------------+
|                                 TRANSPORT & API GATEWAY LAYER                                   |
|  - Cross-Platform Non-Blocking C++17 HTTP Microserver (POSIX sockets on Linux, WinSock2 on Win) |
|  - Strict CORS Origin Security Firewall with Preflight OPTIONS (HTTP 204)                       |
|  - Production Protection: Restricted administrative commands in cloud environments              |
+------------------------------------------------+------------------------------------------------+
                                                 | Native In-Memory Calls (C++17)
                                                 v
+-------------------------------------------------------------------------------------------------+
|                                    NUMERICAL & R&D CORE                                         |
|  - 3D Finite Volume Discretization (Cartesian 7-point stencil, Dirichlet/Robin BCs)             |
|  - Sparse Matrix Representations: Compressed Sparse Row (CSR) & Symmetric Profile Envelopes    |
|  - Reverse Cuthill-McKee (RCM) Graph Bandwidth Optimization                                     |
|  - Profile Cholesky (Exact L*L^T direct factorization and triangular back-substitution)        |
|  - Preconditioned Conjugate Gradient (Jacobi diagonal preconditioning)                         |
|  - Successive Over-Relaxation (SOR) with dynamic omega relaxation parameter                    |
+-------------------------------------------------------------------------------------------------+
```

---

## 5. Technology Stack

- **Backend Numerical Core & API:** C++17 (compiled with GCC/Clang `-O3` or MSVC `/O2`).
- **Linear Algebra Architecture:** Custom high-efficiency sparse CSR matrix routines, profile envelope storage, and Eigen 3.4 headers for reference validation.
- **Frontend Presentation:** Vanilla ES6+ JavaScript, Three.js (r128), OrbitControls, Chart.js.
- **Cloud Infrastructure & Deployment:**
  - **Frontend:** Hosted on Vercel Global Edge Network.
  - **Backend Daemon:** Containerized multi-stage Linux Docker service hosted on Render.

---

## 6. Project Directory Structure

```
Unified-Engineering-Workbench/
├── CMakeLists.txt                     # Multi-platform root CMake configuration
├── Dockerfile                         # Production multi-stage Dockerfile
├── LICENSE                            # MIT License
├── README.md                          # Primary project documentation
├── render.yaml                        # Render cloud deployment blueprint
├── vercel.json                        # Vercel deployment & API proxy configuration
├── data/                              # Bundled sample STL meshes and validation data
├── docs/                              # Technical documentation
│   ├── API_REFERENCE.md               # Complete REST API schemas and parameters
│   ├── ARCHITECTURE.md                # Mathematical formulations and class designs
│   ├── DEPLOYMENT.md                  # Cloud deployment runbook (Render, Vercel, Docker)
│   └── TESTING.md                     # Test case catalog and verification procedures
├── results/                           # Benchmark datasets and solver performance CSVs
│   ├── benchmark_solvers_3d.csv       # Multi-solver timing, residual, and memory sweep
│   ├── benchmark_summary_3d.csv       # Statistical aggregations across grid sizes
│   └── benchmark_sor_sweep_3d.csv     # SOR omega relaxation sweep (omega = 1.0 to 1.95)
├── src/
│   ├── backend/
│   │   ├── include/                   # C++17 headers (sparse matrix, solvers, geometries)
│   │   │   ├── http_server.hpp        # Non-blocking HTTP microserver header
│   │   │   ├── thermal_geometry_3d.hpp# FVM grid discretization and boundary conditions
│   │   │   ├── sparse_matrix.hpp      # CSR sparse format & SpMV kernel
│   │   │   ├── rcm_ordering.hpp       # Reverse Cuthill-McKee graph reordering
│   │   │   ├── profile_cholesky.hpp   # Direct profile envelope Cholesky solver
│   │   │   ├── pcg_solver.hpp         # Preconditioned Conjugate Gradient solver
│   │   │   └── sor_solver.hpp         # Successive Over-Relaxation solver
│   │   └── src/                       # C++17 implementation files
│   └── frontend/                      # Web application
│       ├── index.html                 # Single-page application entry point
│       ├── css/style.css              # Styling and responsive layout
│       ├── js/
│       │   ├── api.js                 # Unified API client & status synchronization
│       │   ├── app.js                 # Shell state and view routing
│       │   ├── config.js              # Environment settings and defaults
│       │   └── views/                 # View controllers (thermal3d.js, etc.)
│       └── vendor/                    # Self-contained third-party libraries (Three.js)
└── tests/
    └── test_runner.cpp                # 53-test automated unit and numerical test runner
```

---

## 7. Prerequisites

- **C++17 Compiler:** GCC 9+, Clang 10+, or MSVC 2019+
- **Build System:** CMake 3.16+
- **Header Dependency:** `libeigen3-dev` (Eigen 3.3+ / 3.4)
- **Optional Tools:** Docker 20.10+, Git, Python 3 / Node.js (for static local file serving)

---

## 8. Local Installation & Setup

### 1. Clone Repository
```bash
git clone https://github.com/xoxosuriya/Unified-Engineering-Workbench.git
cd Unified-Engineering-Workbench
```

### 2. Configure and Build Backend
```bash
# Generate build configuration
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile backend server and test runner
cmake --build build --config Release -j
```

### 3. Run Automated Tests
```bash
# Execute full 53-test verification suite
./build/workbench_tests          # Linux / macOS
.\build\Release\workbench_tests.exe # Windows
```

### 4. Start Backend Server
```bash
# Launch the C++ HTTP microservice on port 8080
./build/workbench_backend --port 8080 --host 127.0.0.1
```

### 5. Launch Frontend
Because the frontend is built with vanilla ES modules, serve `src/frontend` via any local HTTP static server:
```bash
# Using Python
python3 -m http.server 3000 --directory src/frontend

# Or using Node.js npx
npx serve src/frontend -p 3000
```
Open `http://localhost:3000` in your web browser. The frontend automatically detects the local backend on port `8080`.

---

## 9. Docker Deployment

To build and run the production container locally:

```bash
# Build multi-stage Docker container
docker build -t unified-engineering-workbench .

# Run container binding port 8080
docker run -d -p 8080:8080 --name uew-backend unified-engineering-workbench

# Verify health endpoint
curl http://localhost:8080/api/thermal3d/health
```

---

## 10. Environment Variables

The backend supports configurable execution through environment variables:

| Variable | Description | Default | Example |
| :--- | :--- | :--- | :--- |
| `PORT` | Listening TCP port | `8080` | `10000` |
| `HOST` | Listening host IP address | `0.0.0.0` | `127.0.0.1` |
| `WORKBENCH_ENV` | Runtime environment mode | `development` | `production` |
| `ALLOWED_ORIGINS` | Comma-separated list of permitted CORS origins | *(builtin defaults)* | `https://your-domain.vercel.app` |
| `DISABLE_SHUTDOWN`| Disables `/api/shutdown` endpoint in production | `0` | `1` |

---

## 11. REST API Quick Reference

| Endpoint | Method | Description |
| :--- | :---: | :--- |
| `/api/thermal3d/health` | `GET` | Health check returning status, version, and solver list |
| `/api/thermal3d/models` | `GET` | Returns available geometries (`CUBE`, `FINNED_SINK`) |
| `/api/thermal3d/config` | `GET` | Advertised server limits, mesh bounds, and defaults |
| `/api/thermal3d/solve` | `POST` | Dispatches 3D FVM simulation; returns nodal field and metrics |
| `/api/status` | `GET` | Session token, active port, and hardware telemetry |

### Example Solve Request:
```bash
curl -X POST http://localhost:8080/api/thermal3d/solve \
  -H "Content-Type: application/json" \
  -d '{"model":"FINNED_SINK","resolution":10,"solver":"PROFILE_CHOLESKY"}'
```

---

## 12. Troubleshooting & Backend Connectivity

- **Status Shows "BACKEND UNAVAILABLE (Demo Mode)":**
  - If running in the cloud, Render free-tier containers automatically spin down after inactivity. On cold boot, waking up the container takes approximately 40–50 seconds. The status indicator will transition to **C++17 Backend Online** as soon as the container responds.
  - If running locally, confirm that `workbench_backend` is executing and listening on port `8080` (or check console logs for port conflicts).
- **CORS Origin Rejection:**
  - Verify that the hostname of your frontend is listed in `ALLOWED_ORIGINS` or matches localhost/Vercel/Render domains.
- **Simulation Tolerance Warnings:**
  - For high mesh resolutions ($N=30$), iterative solvers (PCG, SOR) require sufficient iteration budgets (typically 500–1000 iterations) to reach tight convergence tolerances ($10^{-8}$).

---

## 13. License

This project is licensed under the **MIT License** — see the [LICENSE](./LICENSE) file for details.
All third-party libraries utilized for client-side rendering (Three.js, Chart.js) are included under permissive open-source licenses.
