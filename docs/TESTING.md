# Automated Test Suite & Verification Matrix

**PASS Science Works — R&D Internship Assessment 2026–2027**  
**Candidate:** Suriya Prakash S (RA2311047010144)  
**Test Suite Executable:** `workbench_tests.exe` (Windows) / `workbench_tests` (Linux)  
**Total Test Count:** **53 Automated Verification Tests**  
**Pass Rate:** **53 / 53 Passed (100% Pass Rate)**  

---

## 1. Test Suite Taxonomy & Structure

The test runner (`tests/test_runner.cpp`) is partitioned into six verification tiers:

### Tier 1: File Parsing & Robustness (Tests 1–11)
* `test_stl_parser_positive`: ASCII facet parsing, normal vector validation, and surface area numerical integration on `sample_bracket.stl`.
* `test_stl_parser_empty_file`: 0-byte file rejection with descriptive error message.
* `test_stl_parser_nan_coords`: Rejection of non-finite `NaN` / `Inf` vertex coordinates.
* `test_stl_parser_malformed_syntax`: Syntax parser state machine recovery and line-numbered syntax diagnostics.
* `test_ansys_parser_positive`: CSV schema validation on synthetic nodal temperatures.
* `test_ansys_parser_empty_file`: 0-byte CSV rejection.
* `test_ansys_parser_missing_header`: Missing column header validation.
* `test_ansys_parser_below_absolute_zero`: Thermodynamic physical constraint check ($T < -273.15^\circ\text{C}$ rejected).
* `test_disk_files_sample_bracket_and_ansys`: Real disk file integrity and provenance test.
* `test_disk_test_samples`: Automated batch evaluation of corrupted specimens in `data/test_samples/`.
* `test_parser_edge_cases`: Boundary conditions and whitespace handling.

### Tier 2: 1D Thermal FEA & Mathematical Exactness (Tests 12–14)
* `test_thermal_fea_analytical_and_convergence`: Analytical exactness on 1D rod ($< 10^{-14\ \circ}\text{C}$ nodal error) and exact $O(h^2)$ midpoint convergence.
* `test_thermal_fea_edge_cases_N1_N2`: Minimal mesh boundary conditions.
* `test_thermal_fea_input_validation`: Negative thermal conductivity, invalid lengths, and contradictory boundary conditions.

### Tier 3: Multicore Concurrency & Hardware Telemetry (Tests 15–18)
* `test_multicore_bench_agreement`: Serial vs. parallel temperature field agreement ($\max |T_{\text{serial}} - T_{\text{parallel}}| \le 10^{-8\ \circ}\text{C}$).
* `test_multicore_bench_cancellation`: Mid-flight cooperative cancellation of worker thread batch processing.
* `test_embedded_hardware_lifecycle_and_fault_recovery`: Telemetry state machine transitions, timeout fault injection, and CRC recovery.
* `test_backpropagation_gradient_check`: Analytical gradients vs. central finite differences ($\epsilon = 10^{-6}$, error $< 10^{-10}$).

### Tier 4: Security, Network Guardrails & Input Sanitization (Tests 19–23)
* `test_parameter_range_rejection`: Out-of-bounds parameter rejection across all REST routes.
* `test_benchmark_concurrency_guard_409`: HTTP 409 Conflict rejection for overlapping benchmark jobs.
* `test_benchmark_cancel_mid_run`: REST endpoint asynchronous job cancellation.
* `test_json_sanitizer`: JSON string escaping, trailing whitespace, and control character handling.
* `test_duplicate_id_and_1e300_rejection`: Overflow protection on numeric and identifier parsing.

### Tier 5: Sparse Linear Algebra & 3D Geometry (Tests 24–38)
* `test_sparse_matrix_spd_properties`: Symmetry, diagonal dominance, and positive-definiteness verification of native CSR.
* `test_direct_cholesky_factorization_exactness`: Verification that factored profile $\mathbf{L} \mathbf{L}^T = \mathbf{A}$.
* `test_cholesky_pivot_scale_invariance`: Matrix diagonal scaling invariance under Cholesky decomposition.
* `test_csr_structural_robustness`: Non-zero indexing bounds and matrix-vector multiplication correctness.
* `test_profile_cholesky_skyline_fillin_and_rcm`: Reverse Cuthill–McKee bandwidth minimization on degree-adjacency graphs.
* `test_phase1_hardening_and_edge_cases`: Zero diagonal guard, asymmetric matrix rejection, and empty graph handling.
* `test_eigen_conversion_and_pcg_foundation`: Conversion from native CSR to `Eigen::SparseMatrix` and basic PCG solve.
* `test_thermal_geometry_3d_cube_and_finned_mesh_hierarchy`: Structured Cartesian grid generation for $N \in \{10, 20, 30\}$.
* `test_cube_analytical_exactness_fvm`: Nodal error against 3D continuous exact solution scaling as $E_{\text{an}} = 1.25 / N^2\ ^\circ\text{C}$ ($O(h^2)$).
* `test_finned_geometry_energy_conservation_fvm`: Global energy conservation $\left| \sum Q_{\text{base}} - \sum Q_{\text{conv}} \right| < 10^{-12}\text{ W}$.
* `test_sor_convergence_and_solver_agreement`: Cross-solver pointwise agreement between Profile Cholesky and SOR ($< 10^{-4\ \circ}\text{C}$).
* `test_sor_parameters_initial_guess_and_four_termination_categories`: Divergence guard on $\omega \ge 2.0$, stagnation detection, and maximum iteration bounds.
* `test_comparative_benchmark_suite_orchestration`: Five-run median timing aggregation and CSV artifact generation.
* `test_matrix_memory_accounting_formulas`: Verification of CSR formula $(M+1)\times 8 + NNZ \times 12$ and Cholesky envelope size.
* `test_sor_omega_sweep_optimal_parameter`: Verification of $\omega^* = 1.70$ on Model B Cube.

### Tier 6: 3D REST API Endpoints & Transport Guardrails (Tests 39–53)
* `test_thermal3d_api_models_endpoint`: Schema validation for `/api/thermal3d/models`.
* `test_thermal3d_api_config_endpoint`: Schema validation for `/api/thermal3d/config`.
* `test_thermal3d_api_health_endpoint`: Schema validation for `/api/thermal3d/health`.
* `test_thermal3d_api_solve_cube_cholesky_success`: Model B Cube solve with Profile Cholesky ($N=10$).
* `test_thermal3d_api_solve_finned_pcg_success`: Model C Finned Sink solve with PCG ($N=10$).
* `test_thermal3d_api_solve_cube_sor_convergence`: Model B Cube solve with SOR ($N=10$).
* `test_thermal3d_api_unsupported_model_rejection_400`: Rejection of invalid geometry IDs.
* `test_thermal3d_api_unsupported_resolution_rejection_400`: Rejection of non-supported resolutions ($N \notin \{10, 20, 30\}$).
* `test_thermal3d_api_invalid_solver_rejection_400`: Rejection of unknown solver IDs.
* `test_thermal3d_api_invalid_options_and_malformed_json_400`: Malformed JSON syntax and parameter type mismatch handling.
* `test_thermal3d_api_field_arrays_and_statistics`: Verification of returned temperature array lengths and min/max/average statistics.
* `test_thermal3d_api_solver_failure_usable_field_false`: Verification that non-converged solves set `usable_field: false`.
* `test_thermal3d_api_deterministic_concurrency_guard_409`: Overlapping solve request rejection (HTTP 409 Conflict).
* `test_thermal3d_api_duplicate_key_rejection_400`: Malformed JSON duplicate key rejection.
* `test_thermal3d_api_origin_validation_and_rejection`: CORS origin rejection for unauthorized third-party origins.
* `test_thermal3d_api_serialization_safety_and_non_finite_guard`: Prevention of non-finite floating point leakage in responses.

---

## 2. Running Tests

### Local Execution (Windows):
```powershell
./workbench_tests.exe
```

### Local / Container Execution (Linux):
```bash
./workbench_tests
```

### Continuous Integration (GitHub Actions):
The automated test suite runs on every pull request and push to `main` across Linux container runners in `.github/workflows/ci.yml`.
