# REST API Reference: Unified Engineering Workbench

**PASS Science Works — R&D Internship Assessment 2026–2027**  
**Candidate:** Suriya Prakash S (RA2311047010144)  
**Base URL:**
- Cloud Production: `https://unified-engineering-workbench-backend.onrender.com`
- Local Desktop: `http://127.0.0.1:8080` (or dynamic port from `logs/port.txt`)

---

## 1. Authentication & Security Policy

1. **CORS Policy:**
   - Allowed Origins: `https://xoxosuriya.github.io`, `http://127.0.0.1:*`, `http://localhost:*`.
   - Wildcard origins with credentials are explicitly prohibited.
   - Preflight `OPTIONS` requests return HTTP 204 No Content with `Access-Control-Allow-Origin`, `Access-Control-Allow-Methods`, `Access-Control-Allow-Headers`.
2. **Session Protection:**
   - Desktop administrative operations require `X-Session-Token` generated at process launch.
   - In cloud production (`WORKBENCH_ENV=production`), administrative shutdown `/api/shutdown` is strictly disabled (HTTP 403 Forbidden).
3. **Payload Limits & Sanity:**
   - Maximum request payload size: 50 MB (`HTTP 413 Payload Too Large`).
   - Non-finite numbers (`NaN`, `Inf`) and duplicate JSON keys are rejected (`HTTP 400 Bad Request`).
4. **Deterministic Concurrency Guard:**
   - Mutually exclusive 3D solve requests return `HTTP 409 Conflict` if a solve is currently executing.

---

## 2. 3D Thermal Simulation Endpoints (Round 2)

### 2.1 Health Check
* **Route:** `GET /api/thermal3d/health`
* **Response:**
```json
{
  "status": "ONLINE",
  "engine": "PASS Science Works 3D FVM Engine",
  "version": "2.0.0",
  "dofs_supported": [1000, 8000, 27000, 520, 4160, 14040],
  "solvers": ["PROFILE_CHOLESKY", "PCG", "SOR"],
  "active_job": null
}
```

### 2.2 Model Catalog
* **Route:** `GET /api/thermal3d/models`
* **Response:**
```json
{
  "models": [
    {
      "id": "CUBE",
      "name": "Analytical 3D Cube",
      "dimensions": [0.1, 0.1, 0.1],
      "active_fraction": 1.0,
      "boundary_conditions": { "left": "T_base=100C", "all_others": "adiabatic" }
    },
    {
      "id": "FINNED_SINK",
      "name": "3D Industrial Finned Heat Sink",
      "dimensions": [0.1, 0.1, 0.1],
      "active_fraction": 0.52,
      "fins_count": 4,
      "boundary_conditions": { "base": "T_base=100C", "exposed_faces": "Robin h=50 W/(m^2*K), T_inf=20C" }
    }
  ]
}
```

### 2.3 Solver Configuration & Parameters
* **Route:** `GET /api/thermal3d/config`
* **Response:**
```json
{
  "resolutions": [10, 20, 30],
  "solvers": [
    {
      "id": "PROFILE_CHOLESKY",
      "type": "direct",
      "description": "Profile/Skyline Cholesky with Reverse Cuthill-McKee bandwidth minimization"
    },
    {
      "id": "PCG",
      "type": "iterative",
      "description": "Preconditioned Conjugate Gradient with Jacobi diagonal scaling",
      "default_options": { "max_iterations": 1000, "relative_tolerance": 1e-8 }
    },
    {
      "id": "SOR",
      "type": "iterative",
      "description": "Successive Over-Relaxation with matrix-free spectral optimization",
      "default_options": { "omega": 1.70, "max_iterations": 5000, "relative_tolerance": 1e-8 }
    }
  ]
}
```

### 2.4 Execute 3D Thermal Solve
* **Route:** `POST /api/thermal3d/solve`
* **Request Schema:**
```json
{
  "model": "FINNED_SINK",
  "resolution": 20,
  "solver": "PCG",
  "solver_options": {
    "relative_tolerance": 1e-8,
    "max_iterations": 1000
  }
}
```
* **Response Schema:**
```json
{
  "success": true,
  "model": "FINNED_SINK",
  "resolution": 20,
  "solver": "PCG",
  "status": "CONVERGED",
  "dofs_M": 4160,
  "nnz_A": 26880,
  "iterations": 64,
  "relative_residual": 8.12e-9,
  "temperature_min_C": 28.45,
  "temperature_max_C": 100.00,
  "temperature_avg_C": 64.32,
  "energy_conservation_error": 1.42e-14,
  "metrics": {
    "tAssembly": 1.45,
    "tSolve": 1.82,
    "tTotal": 3.27,
    "memory_csr_bytes": 355928,
    "memory_workspace_bytes": 199680
  },
  "temperatures": [100.0, 99.8, 98.4, "..."]
}
```

---

## 3. Preserved Historical Endpoints (Round 1)

* `GET /api/status`: System hardware telemetry, CPU model, core count, process start time.
* `POST /api/stl/parse`: ASCII STL geometry upload, facet parsing, surface area integration.
* `POST /api/ansys/parse`: ANSYS synthetic nodal temperature CSV validation.
* `GET /api/fea/solve`: 1D Galerkin FEA rod calculation with analytical verification.
* `GET /api/fea/convergence`: 1D FEA mesh refinement study ($N \in [10, 80]$).
* `GET /api/performance/run`: Multicore batch benchmark ($p \in [1, 4]$ threads, 20,000 cases).
* `POST /api/performance/cancel`: Mid-run cancellation of multicore benchmark.
* `GET /api/performance/progress`: Real-time streaming progress of active benchmark workload.
* `POST /api/hardware/connect`: Connect mock embedded device (`mcu_stm32h7`, `fpga_artix7`, `jtag_probe`).
* `POST /api/hardware/disconnect`: Disconnect active mock embedded device.
* `POST /api/hardware/inject_fault`: Inject recoverable timeout or CRC fault.
* `GET /api/hardware/telemetry`: Stream live 10 Hz telemetry telemetry frame.
* `GET /api/backprop/study`: Execute 100-step training loss optimization & finite difference verification.
* `POST /api/shutdown`: Administrative desktop process shutdown (`X-Session-Token` required; disabled in cloud).
