# Statement of Ownership & Reused Code Disclosure

**Candidate Assessment:** Unified Engineering Workbench  
**Organization:** PASS SCIENCE WORKS | R&D Internship 2026-2027  
**Submission Date:** November 1, 2026  

---

## 1. Original Work Produced by Candidate
The following components represent original engineering designs, mathematical formulations, and C++ implementations authored specifically for this submission:
1. **C++17 Numerical & Parsing Engine (`src/backend/src/`):**
   - `stl_parser.cpp`: Hand-crafted deterministic state machine tokenizer for ASCII STL format with bounding box, area integration, and strict grammar error handling.
   - `ansys_parser.cpp`: CSV nodal parser with schema validation, coordinate verification, and absolute zero temperature constraints.
   - `thermal_fea_1d.cpp`: Galerkin 1D finite-element formulation, element stiffness/load assembly, Dirichlet boundary condition enforcement, and Thomas algorithm tridiagonal solver.
   - `thread_pool.cpp` & `multicore_bench.cpp`: Bounded worker thread pool, reproducible Mersenne Twister batch generator, and deterministic agreement verification.
   - `embedded_hardware.cpp`: High-fidelity C++ mock adapters for STM32H7, Artix-7, and Segger J-Link with state machine recovery and fault injection.
   - `backprop_engine.cpp`: Analytical forward pass, chain-rule gradient engine, simultaneous parameter update, and central finite difference checker.
2. **Architecture & Windows 11 Packaging:**
   - `launcher_win.cpp`: Zero-terminal Win32 GUI launcher with tray icon integration and automatic browser launch.
   - Top-level `CMakeLists.txt` build configuration.
3. **Frontend Application:**
   - `style.css`: Custom dark-mode glassmorphic design system.
   - `app.js`, `geometry_view.js`, `ansys_view.js`, `thermal_view.js`, `performance_view.js`, `hardware_view.js`, `backprop_view.js`: Custom application controllers, Three.js 3D point cloud colormaps, raycasting tooltip inspectors, and interactive Chart.js visualizations.
4. **Documentation & Datasets:**
   - `docs/Workbench_Report.md`: 6-page comprehensive technical engineering report.
   - `docs/handwritten_backprop/`: 2-page handwritten mathematical backpropagation derivation.
   - `data/sample_bracket.stl`: Analytically generated 3D mechanical mounting bracket.
   - `data/ansys_thermal_synthetic.csv`: Synthetic ANSYS nodal temperature dataset.

---

## 2. Reused Open-Source Libraries & Dependencies
In compliance with Section 1 ("Disclose AI assistance, reused code, dependencies and licences"), the following open-source libraries are bundled locally in `src/frontend/vendor/` with zero runtime internet requirement:

1. **Three.js (r128):**
   - Purpose: WebGL 3D rendering of ASCII STL triangular facets and ANSYS 3D nodal point clouds.
   - License: MIT License (Copyright (c) 2010-2026 Three.js Authors).
2. **OrbitControls.js (r128):**
   - Purpose: Mouse-driven 3D camera rotation, panning, and zooming.
   - License: MIT License (Copyright (c) 2010-2026 Three.js Authors).
3. **Chart.js (v4.4.7 UMD):**
   - Purpose: Interactive 2D plotting of temperature profiles, mesh convergence rates, speedup scaling, and backpropagation loss curves.
   - License: MIT License (Copyright (c) 2014-2026 Chart.js Contributors).

All application backend logic, numerical computation, file processing, and concurrency are strictly written in **standard C++17** using only the C++ Standard Library and native Windows operating system sockets (`ws2_32`).
