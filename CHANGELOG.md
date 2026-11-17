# Changelog: Unified Engineering Workbench

All notable changes to the Unified Engineering Workbench are documented in this file.

---

## [Round 2 Integration] - 2026-11-17

### Added
- **3D Finite Volume Method (FVM) Continuum Engine:**
  - Formulated and implemented cell-centred FVM for 3D steady-state heat conduction ($-\nabla \cdot (k \nabla T) = q'''$).
  - Implemented Model B (Continuum Cube, $M = N^3$ DOFs) validated against exact continuous analytical solution.
  - Implemented Model C (3D Finned Heat Sink Solid, $M = 0.52 N^3$ DOFs) with baseplate heat flux and 4 longitudinal cooling fins with Robin convection.
- **Sparse Linear Algebra & Solvers:**
  - Custom Compressed Sparse Row (CSR) matrix representation (`sparse_matrix.hpp` / `.cpp`).
  - Custom Profile / Skyline Cholesky factorization with Reverse Cuthill–McKee (RCM) degree-adjacency bandwidth minimization.
  - Integration of Eigen 3.4.0 Jacobi-preconditioned Conjugate Gradient (PCG) solver.
  - Custom Successive Over-Relaxation (SOR) with matrix-free spectral radius optimization and David Young theoretical validation ($\omega^* = 1.70$).
- **Multi-Solver Scalability Benchmarks:**
  - Scalability profiling across $N \in \{10, 20, 30\}$ ($M = 1,000$ to $27,000$ DOFs).
  - Documented $105.9\times$ solve-kernel speedup and $82.7\times$ live server daemon speedup for PCG over Profile Cholesky.
  - Documented $45.1\times$ Cholesky envelope expansion over CSR matrix storage and $29.3\times$ structural memory reduction for PCG.
- **Cross-Platform Linux & Cloud Portability:**
  - Abstracted Windows WinSock2 and POSIX socket networking layer in `http_server.cpp`.
  - Multi-stage Linux Docker build (`Dockerfile`) and Render deployment blueprint (`render.yaml`).
  - Automated CI/CD workflows for GitHub Actions: static GitHub Pages deployment (`deploy-pages.yml`) and Linux build/test verification (`ci.yml`).
  - Secure Origin allowlist, Host header validation, and cloud shutdown disabling.
- **Interactive 3D Thermal Visualization:**
  - New 3D FVM Viewport (`src/frontend/js/views/thermal3d.js`) with Three.js rendering.
  - Interactive multi-plane voxel slicing across X, Y, and Z axes ($0\% \to 100\%$).
  - Interactive voxel gap / explosion slider ($0\% \to 100\%$).
  - Real-time comparative execution dock and API contract inspector.
- **Comprehensive Documentation & Reports:**
  - 24-page publication-grade R&D technical report (`docs/reports/round-2/RA2311047010144_Suriya_Prakash_S_PASS_RnD_Technical_Report.pdf`).
  - Modular documentation: `ARCHITECTURE.md`, `API_REFERENCE.md`, `DEPLOYMENT.md`, `TESTING.md`, `AI_ASSISTANCE.md`.
  - Extended test suite to 53 automated unit and numerical verification tests (100% pass rate).

### Changed
- Preserved all historical Round 1 modules (1D FEA rod solver, STL parser, ANSYS nodal parser, multicore bench, embedded hardware, backpropagation).
- Updated root `CMakeLists.txt` to conditionally build Windows-specific launcher on Windows and portable backend on Linux.
- Updated frontend status indicator to truthfully distinguish `CONNECTING`, `PRODUCTION ONLINE`, `BACKEND UNAVAILABLE`, and `DEMO MODE`.

---

## [Round 1 Initial Release] - 2026-11-03

### Added
- Historical 1D thermal finite-element solver with Galerkin formulation and Thomas algorithm (TDMA).
- State-machine ASCII STL parser and surface area boundary integrator.
- ANSYS synthetic nodal temperature CSV validator with physical thermodynamic constraints.
- Bounded thread-pool multicore benchmark with serial-parallel agreement verification.
- Mock embedded hardware telemetry testbench (STM32H7 MCU, Artix-7 FPGA, J-Link probe).
- Analytical backpropagation engine with central finite-difference gradient verification.
- Windows desktop double-clickable launcher (`Launch.exe`) with WinSock2 local server.
- Initial 23-test automated C++ test suite.
