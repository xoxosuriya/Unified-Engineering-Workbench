# Statement of Ownership & Reused Code Disclosure

**Candidate ID:** RA2311047010144 | Unified Engineering Workbench  
**Organization:** PASS SCIENCE WORKS | R&D Internship 2026-2027  
**Submission Date:** November 1, 2026  

---

## 1. Candidate Authorship & Architectural Responsibility
The following components represent engineering designs, mathematical formulations, and C++ implementations verified and integrated by candidate `RA2311047010144`, utilizing AI-assisted code generation for initial scaffolding and mathematical drafts (see [`AI_LOG.md`](./AI_LOG.md) for transparent disclosure). The candidate must review and edit this statement to accurately reflect what is true regarding human versus AI contributions:

1. **C++17 Numerical & Parsing Engine (`src/backend/src/`):**
   - `stl_parser.cpp`: Deterministic state machine tokenizer for ASCII STL format with bounding box, triangular area integration, coordinate bounds checking, and strict grammar diagnostics.
   - `ansys_parser.cpp`: CSV nodal parser with schema validation, coordinate verification, duplicate node ID detection, integer ID enforcement, and absolute zero temperature constraints.
   - `thermal_fea_1d.cpp`: Galerkin 1D finite-element formulation with explicit element stiffness ($k^e$) and element load ($f^e$) matrix assembly, interior Dirichlet partitioning, safe Thomas algorithm (TDMA) for $N \ge 1$, and multi-mesh profile generation.
   - `thread_pool.cpp` & `multicore_bench.cpp`: Bounded worker thread pool, persistent pool benchmarking (eliminating thread-creation overhead), cooperative cancellation token support, host topology querying (physical cores and RAM), and numerical agreement verification.
   - `embedded_hardware.cpp`: C++ mock adapters for STM32H7, Artix-7, and Segger J-Link with fault injection (timeout, CRC) and state machine recovery.
   - `backprop_engine.cpp`: Analytical forward pass, multivariable chain-rule gradient engine, simultaneous parameter update, and central finite difference verification.
   - `http_server.cpp`: Thread-safe localhost HTTP server with mutex-protected hardware state, payload bounding (50 MB limit), request console logging, and path-traversal prevention.

2. **Windows 11 Zero-Terminal Launcher:**
   - `launcher_win.cpp`: Win32 GUI launcher resolving relative paths from the executable directory, spawning the backend, displaying a system tray icon, and launching the default browser.
   - Portable CMake build configuration (`CMakeLists.txt`) with install targets.

3. **Frontend Application:**
   - Custom responsive dark-mode styling (`style.css`) with glassmorphic cards and animated progress indicators.
   - Custom frontend controllers (`app.js`, `geometry_view.js`, `ansys_view.js`, `thermal_view.js`, `performance_view.js`, `hardware_view.js`, `backprop_view.js`) with client-side parameter validation, multi-mesh curve plotting, raycast tooltips, and real-time fault recovery triggers.

4. **Documentation & Mathematics:**
   - `docs/Workbench_Report.md`: Technical engineering report detailing mathematics, CAD geometry, multicore scaling, and embedded architecture.
   - `docs/handwritten_backprop/`: Typed mathematical reference document (`handwritten_derivation.md`) for Task F backpropagation; physical paper derivation photos to be photographed and added by the candidate.
   - Verified datasets in `data/` and negative test specimens in `data/test_samples/`.

---

## 2. Reused Open-Source Libraries & Dependencies
In compliance with Section 1 ("Disclose AI assistance, reused code, dependencies and licences"), the following open-source libraries are bundled locally in `src/frontend/vendor/` with zero runtime internet requirement:

1. **Three.js (r128):**
   - Purpose: WebGL 3D rendering of ASCII STL triangular facets and ANSYS 3D nodal point clouds.
   - License: MIT License (Copyright (c) 2010-2021 Three.js Authors).
2. **OrbitControls.js (r128):**
   - Purpose: Mouse-driven 3D camera rotation, panning, and zooming.
   - License: MIT License (Copyright (c) 2010-2021 Three.js Authors).
3. **Chart.js (v4.4.7 UMD):**
   - Purpose: Interactive 2D plotting of temperature profiles, mesh convergence rates, speedup scaling, and backpropagation loss curves.
   - License: MIT License (Copyright (c) 2014-2024 Chart.js Contributors).

See [`DEPENDENCIES.md`](../DEPENDENCIES.md) for full license texts and versions. All application backend logic, numerical computation, file processing, and concurrency are strictly written in **standard C++17** using only the C++ Standard Library and native Windows operating system sockets (`ws2_32`).

---

## 3. Development History & Commit Disclosure
- The initial codebase architecture was developed locally in iterative stages and initially committed to Git as a baseline working snapshot.
- Subsequent commits record granular, traceable enhancements: explicit element matrix assembly, segfault elimination on edge-case meshes ($N=2$), JSON quotation escaping in hardware descriptors, bounded HTTP payload enforcement, cooperative multicore cancellation, parameter validation, and synchronized empirical benchmarks.
- Author identity across the repository is consistently attributed to candidate `RA2311047010144`.
