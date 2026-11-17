# Phase 3C — Frontend 3D Thermal FEA Visualization and Backend Integration: Architecture & Inspection Report

**Project:** Unified Engineering Workbench (UEW)  
**Repository:** `xoxosuriya/Unified-Engineering-Workbench`  
**Candidate ID:** `RA2311047010144`  
**Inspection Date:** 2026-11-15  
**Status:** Inspection & Design Complete — Awaiting Approval  

---

## Executive Summary

This report establishes the technical architecture, implementation boundaries, and deployment strategy for **Phase 3C: Frontend 3D Thermal FEA Visualization and Backend Integration**.

In Phase 3B, the C++17 backend REST API (`/api/thermal3d/*`) was implemented, hardened, and verified with 53 passing integration and unit tests. The objective of Phase 3C is to provide an interactive, high-performance 3D visualization workspace directly within the Unified Engineering Workbench interface. The new module will connect to the C++17 engine, allow configuration of 3D thermal simulations across models (`CUBE` and `FINNED_SINK`) and solvers (`PROFILE_CHOLESKY`, `PCG`, `SOR`), render thousands of solid finite volume cells via WebGL instancing, and display verified physical metrics.

This inspection verifies that:
1. The existing frontend possesses an extensible modular architecture with a VHS cassette shelf carousel and full-screen dialog sheets.
2. The current 1D Thermal FEA Rod (Task C) can and will remain 100% intact and untouched.
3. The bundled Three.js build (r128) natively supports `THREE.InstancedMesh`, enabling single-draw-call rendering of up to 27,000 voxels with 60 FPS performance.
4. The local C++17 server enforces strict loopback Host and Origin security rules, while the frontend includes an automatic offline simulation fallback (`MockBackend`) for hosted static environments such as GitHub Pages.
5. All integration touchpoints are localized to a small set of files without requiring external build tools, package managers, or breaking changes.

---

## 1. Existing Frontend Architecture

### 1.1 Technology Stack & Structure
The Unified Engineering Workbench frontend is built as a pure, dependency-free vanilla HTML5, CSS3, and ES6 JavaScript application. It requires no bundler (Webpack, Vite, Rollup) and runs directly from static files served either by the C++ backend or any HTTP server.

```
src/frontend/
├── index.html                   # Master entrypoint & DOM layout
├── css/
│   └── style.css                # Leon Vasari VHS shelf design tokens & layout rules
├── fonts/                       # Local authentic web fonts (InterTight, Newsreader)
├── vendor/
│   ├── three.min.js             # Three.js Revision 128 (r128)
│   ├── OrbitControls.js         # Three.js camera interaction controller
│   └── chart.umd.min.js         # Chart.js v4.4.1 for 2D convergence analytics
└── js/
    ├── config.js                # Assessment metadata, global timing & module definitions
    ├── mock.js                  # Client-side offline simulation fallback engine
    ├── api.js                   # Unified HTTP client, session token & status polling
    ├── art.js                   # Procedural duotone SVG generator for cassette spines & covers
    ├── cursor.js                # Custom magnetic cursor controller
    ├── ui.js                    # Status lines, toasts, banners, and ink reveals
    ├── shelf.js                 # 3D VHS cassette shelf carousel & smooth navigation
    ├── views/                   # Active view modules
    │   ├── geometry.js          # Module 0 (Task B): ASCII STL 3D Mesh Inspector
    │   ├── ansys.js             # Module 1 (Task B): ANSYS Nodal Temperature Point Cloud
    │   ├── thermal.js           # Module 2 (Task C): 1D Steady-State Thermal FEA Rod
    │   ├── backprop.js          # Module 3 (Task F): Analytical Backpropagation
    │   ├── performance.js       # Module 4 (Task D): Multicore Batch Benchmarking
    │   └── hardware.js          # Module 5 (Task E): Embedded Hardware Telemetry
    └── app.js                   # Main application coordinator, sheet grow animation & routing
```

### 1.2 Design System & Visual Tokens
The UI adheres strictly to the Leon Vasari VHS shelf reference design specification. A core constraint is **no opacity fades anywhere**: all transitions use physical mask-clips, geometric scales, and paper-ink reveals.

- **Color Tokens (`style.css`):**
  - `--paper`: `#EDEBE6` (warm off-white background)
  - `--sheet`: `#F4F2ED` (inner dialog sheet surface)
  - `--ink`: `#141414` (deep neutral dark text and primary borders)
  - `--muted`: `#6D6A63` (subdued technical labels and secondary legends)
  - `--cobalt`: `#1F3FB0` (cold boundary condition, convergence accents, brand blue)
  - `--vermilion`: `#E0452B` (heat source, maximum temperature, active alerts)
  - `--cream`: `#E8E1D0` (card highlights, neutral mesh fills)
  - `--rule`: `rgba(20, 20, 20, 0.9)` (crisp technical divider lines)
- **Typography:**
  - Serif: `Newsreader` (editorial headlines, cassette titles)
  - Sans-Serif: `Inter Tight` (technical badges, inputs, labels, tables)
  - Monospace: `ui-monospace`, `Cascadia Code`, `Consolas` (numerical values, residuals, code metrics)

### 1.3 Navigation & Module Lifecycle
1. **Shelf Carousel (`shelf.js`):**
   - Displays 3D rotating VHS cassette tapes grouped by category: `ALL`, `VISUALIZE`, `SOLVE`, `SYSTEMS`.
   - The user scrubs via mouse wheel, arrow keys, drag gesture, or decade group buttons.
   - Currently, `shelf.js` contains a constant `NUM_MODULES = 6` with `COPIES = 7` (total 42 tapes).
2. **Sheet Grow Animation (`app.js`):**
   - Clicking the active cassette or pressing Enter triggers `App.openModuleSheet(moduleId)`.
   - The projector computes the exact 3D screen bounding rectangle of the cassette's front face (`Shelf.getActiveCoverRect()`).
   - The `<dialog id="detail-dialog">` expands outward from the cover geometry to full screen using a cubic Bézier clip-path animation (`--ease-grow: cubic-bezier(0.76, 0, 0.18, 1)` over 1050 ms).
3. **View Lifecycle Protocol:**
   Every view module conforms to an identical three-method contract:
   - `mount(parentContainer)`: Injects HTML into `#detail-content`, wires DOM event listeners, initializes WebGL renderers or Chart instances, and attaches a `ResizeObserver`.
   - `resize()`: Recalculates canvas dimensions, updates camera aspect ratios, and invokes `renderer.setSize()`.
   - `unmount()`: Cleans up resources, cancels animation frame loops (`cancelAnimationFrame`), disposes Three.js geometries, materials, textures, and controls, disconnects `ResizeObserver`, and empties references to prevent memory leaks.

---

## 2. Exact Backend API Request/Response Contract

The backend REST API for 3D thermal simulations was implemented in `src/backend/src/http_server.cpp` and hardened during Phase 3B. The frontend must interact exclusively with these documented endpoints.

### 2.1 Metadata & Health Endpoints

#### 1. `GET /api/thermal3d/models`
- **Purpose:** Enumerates supported geometries, their resolutions, and pre-computed mesh statistics.
- **HTTP Status:** `200 OK`
- **Response Format:**
  ```json
  {
    "models": [
      {
        "id": "CUBE",
        "name": "Model B: 3D Continuum Analytical Cube",
        "description": "Uniform cube (0.1m x 0.1m x 0.1m) with 1D Dirichlet boundaries and 4 adiabatic Neumann faces.",
        "resolutions": [
          {
            "N": 10,
            "grid_cells": 1000,
            "active_cells_M": 1000,
            "matrix_nnz": 6400,
            "cell_size_m": 0.01,
            "solid_volume_m3": 0.001,
            "internal_faces_E": 2700,
            "dirichlet_faces": 200,
            "robin_faces": 0,
            "neumann_faces": 400
          },
          { "N": 20, "grid_cells": 8000, "active_cells_M": 8000, "matrix_nnz": 53600, "cell_size_m": 0.005, ... },
          { "N": 30, "grid_cells": 27000, "active_cells_M": 27000, "matrix_nnz": 183600, "cell_size_m": 0.003333333333, ... }
        ]
      },
      {
        "id": "FINNED_SINK",
        "name": "Model C: 3D Non-Convex Finned Heat Sink",
        "description": "Finned heat sink with conductive baseplate (Dirichlet), fin convection (Robin), and insulated faces (Neumann).",
        "resolutions": [
          {
            "N": 10,
            "grid_cells": 1000,
            "active_cells_M": 520,
            "matrix_nnz": 2656,
            "cell_size_m": 0.01,
            "solid_volume_m3": 0.00052,
            "internal_faces_E": 1136,
            "dirichlet_faces": 100,
            "robin_faces": 556,
            "neumann_faces": 0
          },
          { "N": 20, "grid_cells": 8000, "active_cells_M": 4160, "matrix_nnz": 24208, "cell_size_m": 0.005, ... },
          { "N": 30, "grid_cells": 27000, "active_cells_M": 14040, "matrix_nnz": 87048, "cell_size_m": 0.003333333333, ... }
        ]
      }
    ]
  }
  ```

#### 2. `GET /api/thermal3d/config`
- **Purpose:** Exposes default physical properties and solver configuration specifications.
- **HTTP Status:** `200 OK`
- **Response Format:**
  ```json
  {
    "physical_properties_fixed": true,
    "physical_properties": {
      "thermal_conductivity_W_mK": 10.0,
      "volumetric_heat_gen_W_m3": 10000.0,
      "heat_transfer_coeff_W_m2K": 50.0,
      "ambient_temp_degC": 25.0,
      "hot_temp_degC": 100.0,
      "cold_temp_degC": 20.0
    },
    "solvers": [
      {
        "id": "PROFILE_CHOLESKY",
        "name": "Custom Profile Cholesky (with RCM)",
        "type": "DIRECT",
        "description": "Skyline profile Cholesky with reverse Cuthill-McKee bandwidth minimization.",
        "relative_residual_threshold": 1e-12
      },
      {
        "id": "PCG",
        "name": "Eigen PCG (Jacobi)",
        "type": "ITERATIVE",
        "description": "Conjugate Gradient with diagonal Jacobi preconditioning via Eigen 3.4.0.",
        "default_tolerance": 1e-6,
        "default_max_iterations": 5000,
        "tolerance_range": [1e-14, 1.0],
        "max_iterations_range": [1, 100000]
      },
      {
        "id": "SOR",
        "name": "Custom SOR",
        "type": "ITERATIVE",
        "description": "Successive Over-Relaxation with 4-tier MECE termination.",
        "default_omega": 1.0,
        "omega_range": [0.0001, 1.9999],
        "default_tolerance": 1e-6,
        "default_max_iterations": 10000,
        "tolerance_range": [1e-14, 1.0],
        "max_iterations_range": [1, 100000]
      }
    ]
  }
  ```

#### 3. `GET /api/thermal3d/health`
- **Purpose:** Verifies backend availability, solver readiness, and active solve locking.
- **HTTP Status:** `200 OK`
- **Response Format:**
  ```json
  {
    "status": "ONLINE",
    "eigen_version": "3.4.0",
    "solvers_available": ["PROFILE_CHOLESKY", "PCG", "SOR"],
    "solve_in_progress": false
  }
  ```

---

### 2.2 Simulation Endpoint: `POST /api/thermal3d/solve`

#### Request Payload Specification
The request body must be a valid JSON object. Unknown root keys or unknown keys inside `solver_options` are rejected with HTTP 400.

| Field | Type | Required | Values / Constraints | Description |
|---|---|---|---|---|
| `model` | `string` | **Yes** | `"CUBE"`, `"FINNED_SINK"` | Geometry identifier |
| `resolution` | `integer` | **Yes** | `10`, `20`, `30` | Discretization resolution $N$ |
| `solver` | `string` | **Yes** | `"PROFILE_CHOLESKY"`, `"PCG"`, `"SOR"` | Solver algorithm |
| `solver_options` | `object` | No | Object containing subfields below | Optional solver tuning |
| `solver_options.tolerance` | `number` | No | Range `[1e-14, 1.0]`, default `1e-6` | Convergence tolerance |
| `solver_options.max_iterations` | `integer`| No | Range `[1, 100000]`, default `5000` (PCG) or `10000` (SOR) | Iteration cap |
| `solver_options.omega` | `number` | No | Open interval `(0.0, 2.0)`, default `1.0` | SOR relaxation factor |

**Example Request:**
```json
{
  "model": "FINNED_SINK",
  "resolution": 20,
  "solver": "PCG",
  "solver_options": {
    "tolerance": 1e-6,
    "max_iterations": 5000
  }
}
```

#### Successful Response Payload Specification (`200 OK`)
When the solve completes, the backend returns full execution metrics and thermal fields:

```json
{
  "model": "FINNED_SINK",
  "resolution": 20,
  "solver": "Eigen_PCG_Jacobi",
  "mesh": {
    "active_cells_M": 4160,
    "total_cells": 8000,
    "matrix_nnz": 24208,
    "cell_size_m": 0.005,
    "solid_volume_m3": 0.00052
  },
  "metrics": {
    "status": "CONVERGED",
    "usable_field": true,
    "iterations": 48,
    "initial_residual": 210.45,
    "final_residual": 1.25e-7,
    "relative_residual": 5.94e-10,
    "max_analytical_error_degC": null,
    "energy_balance_rel_error": 4.12e-6
  },
  "timings_ms": {
    "assembly_time_ms": 3.42,
    "setup_time_ms": 0.81,
    "solve_time_ms": 12.65,
    "conversion_time_ms": 0.0,
    "total_time_ms": 16.88
  },
  "thermal_field": {
    "count": 4160,
    "min_temp_degC": 23.45,
    "max_temp_degC": 100.0,
    "avg_temp_degC": 58.12,
    "coordinates": [
      [0.0025, 0.0025, 0.0025],
      [0.0075, 0.0025, 0.0025]
    ],
    "temperatures": [
      98.42,
      97.85
    ]
  }
}
```

#### Array Alignment and Voxel Geometry Reconstruction
1. **Strict 1:1 Index Alignment:**
   `thermal_field.coordinates[i]` is a 3-element float array `[x, y, z]` representing the centroid of active solid cell $i$ in meters. `thermal_field.temperatures[i]` is a scalar float representing the temperature of active cell $i$ in °C. For every index $i \in [0, M-1]$, centroid and temperature correspond to the exact same cell.
2. **Reconstructing Voxel Bounding Boxes:**
   The cubic cell edge length $\Delta x$ is given explicitly by `mesh.cell_size_m` (e.g., $0.01\text{ m}$ for $N=10$, $0.005\text{ m}$ for $N=20$, $0.003333\text{ m}$ for $N=30$). Each cell's bounding box is $[\text{centroid}_x \pm \Delta x / 2, \text{centroid}_y \pm \Delta x / 2, \text{centroid}_z \pm \Delta x / 2]$. This enables exact, artifact-free instanced voxel generation in Three.js without requiring face or connectivity arrays.

#### Solver Status Semantics & Degradation States
The field `metrics.status` takes one of six mutually exclusive enum strings:

| Status String | Usable Field | Description |
|---|---|---|
| `SUCCESS` | `true` | Direct solve (Profile Cholesky) completed with relative residual $\le 10^{-12}$. |
| `CONVERGED` | `true` | Iterative solve (PCG or SOR) reached $\|r_k\|_2 / \|b\|_2 < \text{tolerance}$. |
| `MAX_ITERATIONS_REACHED` | `false` | Iterative cap reached before convergence. Temperature field discarded; `count: 0`. |
| `DIVERGED_RESIDUAL` | `false` | Residual exceeded safety growth limit ($10^6 \times \|r_0\|_2$). `count: 0`. |
| `DIVERGED_NUMERICAL` | `false` | Solver encountered NaN/Inf or matrix breakdown. `count: 0`. |
| `FAILED` | `false` | Unhandled numerical exception during solve. `count: 0`. |

When `usable_field == false`, the backend defensively clears the arrays:
- `thermal_field.count`: `0`
- `thermal_field.coordinates`: `[]`
- `thermal_field.temperatures`: `[]`
- `thermal_field.min_temp_degC`, `max_temp_degC`, `avg_temp_degC`: `null`

#### Error Responses & HTTP Status Codes
| HTTP Code | Condition | JSON Structure |
|---|---|---|
| `400 Bad Request` | Missing field, invalid model/resolution/solver, invalid options | `{"error": "string", "status": 400}` |
| `403 Forbidden` | Invalid `Host` or `Origin` header | `{"error": "Forbidden: ...", "status": 403}` |
| `409 Conflict` | Another 3D thermal solve is currently running | `{"error": "A 3D thermal solve is already in progress.", "status": 409}` |
| `500 Server Error` | Unhandled backend exception | `{"error": "string", "status": 500}` |

---

## 3. Frontend/Backend Connectivity & Deployment Constraints

### 3.1 Local Development Engine vs. Hosted Static Deployment
The workbench operates in two distinct operational environments:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        OPERATIONAL ENVIRONMENTS                        │
├───────────────────────────────────┬────────────────────────────────────┤
│   A. LOCAL C++17 WORKBENCH        │   B. GITHUB PAGES (ONLINE DEMO)    │
│   (Real Finite Volume Engine)     │   (Static Showcase / Portfolio)    │
│                                   │                                    │
│   Browser Origin:                 │   Browser Origin:                  │
│   http://127.0.0.1:8080           │   https://xoxosuriya.github.io     │
│   Backend: Active on 127.0.0.1    │   Backend: Offline / Unreachable   │
│   Mode: LIVE REAL SOLVER          │   Mode: AUTOMATIC MOCK SIMULATION  │
└───────────────────────────────────┴────────────────────────────────────┘
```

### 3.2 Security Rules in Local Engine
During Phase 3B hardening, strict DNS rebinding and cross-origin protection was implemented in `src/backend/src/http_server.cpp`:
1. **Host Header Validation (Line 658):** Requests must present `Host: 127.0.0.1:<port>` or `localhost:<port>`. All other Host headers are rejected with **HTTP 403 Forbidden**.
2. **Origin Header Validation (Line 674):** For all `/api/*` endpoints and all `POST` requests, if an `Origin` header is present, it must strictly equal `http://127.0.0.1:<actual_port>` or `http://localhost:<actual_port>`.
3. **CORS:** No `Access-Control-Allow-Origin` headers are emitted. External websites cannot access the local simulation engine.

### 3.3 HTTPS-to-HTTP Mixed Content Restriction
When the frontend is accessed over GitHub Pages (`https://...`):
- Modern browser security blocks all active fetch requests from secure HTTPS pages to insecure HTTP loopback endpoints (`http://127.0.0.1:8080`) as **Mixed Active Content**.
- Furthermore, even if HTTP were used externally, the backend's strict Origin validator would reject `https://xoxosuriya.github.io` with HTTP 403 Forbidden.

### 3.4 Handling Strategy & User Instructions
The existing frontend architecture already handles this cleanly via `API.checkBackendAvailability()` in `src/frontend/js/api.js`:
1. **Automatic Detection:** On startup, the frontend performs a probe request to `/api/status`.
   - In local mode (`http://127.0.0.1:8080`), this probe succeeds immediately, activating live engine mode and displaying `C++17 ONLINE`.
   - On GitHub Pages (`https://...`), relative `/api/status` returns 404. The client catches this within 800 ms, toggles `MockBackend`, and displays the amber banner: `DEMO DATA - BACKEND OFFLINE (SIMULATION ACTIVE)`.
2. **Phase 3C Requirement for Mock Data:**
   To maintain a flawless user experience on GitHub Pages without modifying backend CORS rules, `src/frontend/js/mock.js` must implement mock handlers for `getThermal3DModels()`, `getThermal3DConfig()`, `getThermal3DHealth()`, and `solveThermal3D()`, supplying realistic pre-computed or analytical temperature fields.
3. **User Instructions to Run Live Engine:**
   To execute real finite volume simulations against the C++17 engine:
   - Build target: `mingw32-make workbench_backend` (or run `Launch.exe`).
   - Run: `workbench_backend.exe` (or launch via the system tray launcher).
   - Navigate browser to: `http://127.0.0.1:8080`.

---

## 4. Recommended Frontend Integration Architecture

### 4.1 Minimal, High-Performance Integration Design
To maintain clean separation of concerns and avoid regressions in existing modules, the 3D Thermal FEA feature should be integrated as an independent workspace module.

```
                  ┌────────────────────────────────────────┐
                  │          src/frontend/index.html       │
                  │  (Register view script & group tab)    │
                  └───────────────────┬────────────────────┘
                                      │
         ┌────────────────────────────┼────────────────────────────┐
         │                            │                            │
         ▼                            ▼                            ▼
┌──────────────────┐        ┌──────────────────┐        ┌──────────────────┐
│  js/config.js    │        │    js/app.js     │        │    js/shelf.js   │
│  (Add thermal3d  │        │ (Add thermal3d   │        │ (Update module   │
│   to MODULES)    │        │  to viewRegistry)│        │  count & modulo) │
└──────────────────┘        └──────────────────┘        └──────────────────┘
                                      │
                                      ▼
                        ┌───────────────────────────┐
                        │  js/views/thermal3d.js    │
                        │  (NEW: 3D FEA Workspace)  │
                        │  - Model / Solver Controls│
                        │  - Three.js InstancedMesh │
                        │  - Turbo Color Mapping    │
                        │  - Slice / Cross-Section  │
                        │  - Hover Voxel Inspector  │
                        └─────────────┬─────────────┘
                                      │
                         ┌────────────┴────────────┐
                         ▼                         ▼
               ┌──────────────────┐      ┌──────────────────┐
               │    js/api.js     │      │    js/mock.js    │
               │ (Add thermal3d   │      │ (Add simulation  │
               │  HTTP endpoints) │      │  fallback data)  │
               └──────────────────┘      └──────────────────┘
```

### 4.2 Module Presentation & VHS Shelf Placement
- **Module ID:** `thermal3d`
- **Module Title:** `3D Thermal FEA`
- **Label / Tag:** `PHASE 3 / SOLVE`
- **Category Group:** `SOLVE` (co-located alongside the 1D Thermal FEA rod and Backpropagation modules).
- **Blurb:** *3D Finite Volume heat conduction on Cube and Finned Sink with Profile Cholesky, PCG, and SOR solvers.*
- **Procedural Artwork (`art.js`):** Add a duotone screen-print motif for `thermal3d` representing an isometric finned heat sink or 3D discretized cube with thermal contour lines.

### 4.3 View Architecture (`src/frontend/js/views/thermal3d.js`)
Following the proven pattern of `geometry.js` and `ansys.js`, the view will feature a responsive two-column layout:

1. **Left Column (`.detail-left-col` — Controls & Analytics):**
   - **Model Selector:** Radio buttons or styled pills for `Cube (Analytical)` vs `Finned Heat Sink (Non-Convex)`.
   - **Resolution Selector:** Buttons for $N=10$ ($h=10\text{ mm}$), $N=20$ ($h=5\text{ mm}$), $N=30$ ($h=3.33\text{ mm}$).
   - **Solver Selector:** Toggle between `Profile Cholesky (Direct)`, `Eigen PCG (Iterative)`, and `Custom SOR (Iterative)`.
   - **Solver Tuning Parameters (Iterative only):** Tolerance dropdown (`1e-4`, `1e-6`, `1e-8`) and SOR relaxation factor $\omega$ slider (`0.8` to `1.8`, default `1.0`).
   - **Execution Button:** Prominent `RUN 3D THERMAL SOLVE` action button with live progress indicator.
   - **Performance & Accuracy Card:**
     - Status badge (`SUCCESS`, `CONVERGED`, `MAX_ITERATIONS_REACHED`, `DIVERGED`).
     - Iterations count.
     - Final residual $\|b - Ax\|_2$ and relative residual $\|r\|_2 / \|b\|_2$.
     - Analytical error $\max |T - T_{\text{exact}}|$ (for Cube) or Energy balance error $\Delta Q / Q_{\text{gen}}$ (for Finned Sink).
     - Breakdown timings: Assembly, Setup, Solve, and Total wall time.
   - **Visualization Controls:**
     - Slicing plane sliders ($X$, $Y$, $Z$ cross-sections) to inspect internal core temperatures.
     - Cell scale slider (shrink voxels slightly from 100% to 85% to reveal interior grid structure).
     - Wireframe / outline toggle.

2. **Right Column (`.detail-right-col` — WebGL 3D Stage):**
   - **Interactive 3D Canvas:** Rendered using `THREE.InstancedMesh` with orbit, pan, and zoom controls.
   - **Thermal Color Legend:** Vertical gradient bar displaying calibrated temperatures from $T_{\min}$ (Cobalt blue $\sim 20^\circ\text{C}$) to $T_{\max}$ (Vermilion red $\sim 100^\circ\text{C}$) with dynamic tick marks.
   - **Voxel Hover Tooltip:** A real-time HUD card showing hovered cell centroid coordinates $[x, y, z]$, active index, and temperature $T$ to 2 decimal places.
   - **Degradation & Non-Convergence Overlay:** If a solve fails or diverges, the 3D canvas displays a clean paper-style diagnostic banner explaining the numerical breakdown state rather than blank or corrupted geometry.

### 4.4 High-Performance Three.js `InstancedMesh` Design
Using individual `THREE.Mesh` objects for 27,000 cells causes massive CPU-GPU driver overhead and severe frame drops. `THREE.InstancedMesh` solves this completely:
- **Geometry:** A single shared unit cube `THREE.BoxGeometry(1, 1, 1)`.
- **Material:** `THREE.MeshStandardMaterial({ roughness: 0.4, metalness: 0.1 })`.
- **Instance Transform:** For each active cell $i$, compute transformation matrix from centroid $[x_i, y_i, z_i]$ and cell width $\Delta x \times \text{scaleFactor}$.
- **Instance Color:** Compute color using the calibrated thermal gradient:
  $$\text{norm}_T = \frac{T_i - T_{\min}}{T_{\max} - T_{\min}}$$
  Map $\text{norm}_T$ through a smooth 5-stop Jet/Turbo colormap (Blue $\to$ Cyan $\to$ Green $\to$ Yellow $\to$ Red) and call `instancedMesh.setColorAt(i, color)`.
- **Buffer Update:** Trigger `instanceMatrix.needsUpdate = true` and `instanceColor.needsUpdate = true` once after populating all instances.
- **Draw Calls:** Exactly **1 draw call** for the entire solid domain.

---

## 5. Existing Files to Preserve

To ensure absolute stability and prevent regressions in existing assessment deliverables, the following components must remain **unaltered**:

| Component / File | Reason to Preserve |
|---|---|
| `src/frontend/js/views/thermal.js` | **Model A / Task C (1D FEA Rod):** Fully functional, verified 1D solver with analytical convergence charts. Must remain completely intact. |
| `src/frontend/js/views/geometry.js` | **Task B (STL Parser):** Working 3D mesh visualizer. |
| `src/frontend/js/views/ansys.js` | **Task B (ANSYS Cloud):** Working nodal point cloud visualizer. |
| `src/frontend/js/views/performance.js`| **Task D (Multicore Batch):** Multithreaded benchmarking suite. |
| `src/frontend/js/views/hardware.js` | **Task E (Hardware):** Simulated MCU/FPGA telemetry interface. |
| `src/frontend/js/views/backprop.js` | **Task F (Backpropagation):** Gradient derivation study. |
| `src/backend/*` | Backend implementation and tests are complete, hardened, and locked. No backend changes required. |
| `CMakeLists.txt` | Build configuration is verified and passing dual-build checks. |

---

## 6. Implementation Risks & Practical Solutions

| # | Risk | Impact | Practical Solution |
|---|---|---|---|
| **1** | **Shelf Modulo Breakdown** | If a 7th module is added without updating `NUM_MODULES = 6` in `shelf.js`, the cassette carousel indexing will desynchronize, causing navigation glitching or unreachable cassettes. | Update `shelf.js` so that `NUM_MODULES` dynamically tracks `window.MODULES.length` (or set `NUM_MODULES = 7`), ensuring seamless 7-cassette rotation. |
| **2** | **Memory Leaks & WebGL Context Loss** | Repeatedly opening, closing, and re-running 3D thermal solves could exhaust browser WebGL contexts or leak GPU memory. | In `thermal3d.js` `unmount()`, explicitly call `geometry.dispose()`, `material.dispose()`, `renderer.dispose()`, `controls.dispose()`, and remove the WebGL canvas DOM element. |
| **3** | **Large Cell Counts ($N=30$, 27,000 Cells)** | Creating 27,000 individual scene graph objects would freeze the browser thread. | Use `THREE.InstancedMesh(boxGeometry, material, count)`. Populating flat Float32 typed arrays takes $< 8\text{ ms}$ and renders in a single draw call at 60 FPS. |
| **4** | **Unconverged or Diverged States** | If a user runs SOR with bad $\omega$ or low iterations, rendering empty arrays or NaN values could crash Three.js. | Check `metrics.usable_field`. If `false`, display an informative warning card with residual diagnostics and hide the instanced mesh. Validate all inputs against `[min, max]` ranges before issuing requests. |
| **5** | **Mixed Content on GitHub Pages** | Visitors on GitHub Pages cannot connect to local HTTP server. | Implement mock handlers in `mock.js` so GitHub Pages visitors experience smooth demo solves with pre-computed finite volume distributions. |
| **6** | **Concurrent Solve Rejection (HTTP 409)** | Rapidly clicking "Solve" while a computation is running causes backend 409 Conflict. | Disable the "Solve" button and show a busy spinner while `solve_in_progress` is true; re-enable only when the fetch promise settles. |
| **7** | **Dialog Sheet Resize Glitches** | Dialog resize during expand/collapse animation can distort Three.js aspect ratio. | Attach a `ResizeObserver` to the viewport container and trigger `resize()` when dimensions change. |

---

## 7. Proposed Phase 3C Implementation Sequence

The implementation of Phase 3C should be carried out in four controlled, verifiable steps:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   PHASE 3C IMPLEMENTATION SEQUENCE                     │
├────────────────────────────────────────────────────────────────────────┤
│ Step 1: Client API & Mock Fallback Layer                               │
│         - Extend src/frontend/js/api.js with thermal3d methods.        │
│         - Extend src/frontend/js/mock.js with realistic mock data.     │
│                                                                        │
│ Step 2: Shelf & Shell Registration                                     │
│         - Add thermal3d definition in src/frontend/js/config.js.       │
│         - Add cassette artwork motif in src/frontend/js/art.js.        │
│         - Update NUM_MODULES in src/frontend/js/shelf.js.              │
│         - Register view in src/frontend/js/app.js.                     │
│         - Add script tag in src/frontend/index.html.                   │
│                                                                        │
│ Step 3: Implement 3D Thermal View Module                               │
│         - Create src/frontend/js/views/thermal3d.js.                   │
│         - Build controls panel, metrics card, and slicing sliders.     │
│         - Build Three.js InstancedMesh voxel renderer.                 │
│         - Implement Jet/Turbo color mapping and vertical legend.       │
│         - Implement Raycaster voxel hover inspection.                  │
│                                                                        │
│ Step 4: Verification & Cross-Browser Testing                           │
│         - Test Cube N=10, 20, 30 across Cholesky, PCG, and SOR.       │
│         - Test Finned Sink N=10, 20, 30 across Cholesky, PCG, and SOR. │
│         - Verify 1D Thermal FEA Rod continues working with 0 defects.  │
│         - Verify offline demo fallback (?mock=1).                      │
│         - Verify clean unmount and zero memory leaks.                  │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Conclusion & Next Action

This architectural inspection confirms that the existing Unified Engineering Workbench frontend is ready to receive the 3D Thermal FEA module. The integration is clean, highly modular, preserves all existing work (especially Model A / Task C), and leverages native Three.js instancing for fluid performance.

**Current Status:** Inspection complete. Report recorded. Codebase remains completely unmodified.  
**Action Required:** Awaiting user review and explicit approval before commencing Step 1 of Phase 3C implementation.

