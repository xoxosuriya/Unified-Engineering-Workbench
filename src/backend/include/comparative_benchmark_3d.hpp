#pragma once

#include "sparse_matrix.hpp"
#include "thermal_solvers_3d.hpp"
#include "thermal_geometry_3d.hpp"
#include <string>
#include <vector>
#include <chrono>

namespace workbench {

/**
 * @brief Stage-by-stage execution timings for a single solver run.
 */
struct BenchmarkStageTimings {
    double t_asm_ms = 0.0;          ///< Geometry and FVM matrix/RHS assembly time (ms)
    double t_convert_ms = 0.0;      ///< CSR to Eigen format conversion time (ms)
    double t_rcm_ms = 0.0;          ///< RCM ordering permutation computation time (ms)
    double t_env_ms = 0.0;          ///< Skyline envelope allocation & symbolic prep time (ms)
    double t_fac_ms = 0.0;          ///< Numeric Cholesky factorization time (ms)
    double t_setup_ms = 0.0;        ///< Preconditioner construction time (ms)
    double t_sol_ms = 0.0;          ///< Triangular substitution solve or iterative solve time (ms)
    double t_total_solve_ms = 0.0;  ///< Total solver time (setup/fac + sol, excluding assembly)
};

/**
 * @brief Memory and structural metrics for a discretized problem and solver.
 */
struct BenchmarkMemoryMetrics {
    std::size_t active_dofs_M = 0;
    std::size_t matrix_nnz = 0;
    std::size_t factor_envelope_capacity = 0;   ///< Skyline profile envelope capacity
    std::size_t factor_nnz = 0;                 ///< Nonzeros in triangular factor
    double factor_fill_ratio = 0.0;             ///< factor_nnz / matrix_nnz
    std::size_t csr_allocated_bytes = 0;        ///< Application heap memory for CSRMatrix
    std::size_t factor_allocated_bytes = 0;     ///< Application heap memory for Cholesky factor
    std::size_t iterative_buffer_bytes = 0;     ///< Application heap memory for iterative vectors
    std::size_t process_working_set_bytes = 0;  ///< OS-reported process working set (PSAPI)
};

/**
 * @brief Telemetry for a single measured run.
 */
struct BenchmarkRunRecord {
    std::string geometry_name;      ///< "Cube" or "Finned_Heat_Sink"
    int N = 10;
    std::string solver_name;        ///< "Profile_Cholesky_RCM", "Eigen_PCG_Jacobi", "Custom_SOR"
    std::string run_type;           ///< "warmup", "measured", or "median"
    int run_index = 0;              ///< 0 = warmup, 1..5 = measured runs, -1 = median
    BenchmarkStageTimings timings;
    std::string iterations_str;     ///< "N/A" for direct solvers, or integer count for iterative
    int iterations = 0;
    double initial_residual = 0.0;
    double final_residual = 0.0;
    double relative_residual = 0.0;
    SolverStatus status = SolverStatus::SUCCESS;
    BenchmarkMemoryMetrics memory;
};

/**
 * @brief Aggregated benchmark results for a specific (Geometry, N, Solver) configuration.
 */
struct BenchmarkCaseResult {
    std::string geometry_name;
    int N = 10;
    std::string solver_name;
    std::vector<BenchmarkRunRecord> runs;       ///< Warmup (index 0) + 5 measured runs
    BenchmarkRunRecord median_record;           ///< Median record based on t_sol_ms
    std::vector<double> solution;               ///< Converged solution vector
};

/**
 * @brief Telemetry and parameter sweep result for SOR on a specific geometry and resolution.
 */
struct BenchmarkSorSweepRecord {
    std::string geometry_name;                  ///< "Cube" or "Finned_Heat_Sink"
    int N = 10;
    std::size_t active_dofs_M = 0;
    OmegaSweepResult sweep;
};

/**
 * @brief Complete benchmark suite configuration and outputs.
 */
struct ComparativeBenchmarkSuite {
    std::vector<BenchmarkCaseResult> cases;
    std::vector<BenchmarkSorSweepRecord> sor_sweeps;

    // Cross-solver validation metrics
    double max_pairwise_diff_C = 0.0;
    double cube_analytical_error_N20 = 0.0;
    double finned_energy_balance_rel_err_N30 = 0.0;
    bool all_valid_finite = true;

    // Export functions
    [[nodiscard]] std::string to_detailed_csv() const;
    [[nodiscard]] std::string to_summary_csv() const;
    [[nodiscard]] std::string to_sor_sweep_csv() const;
    [[nodiscard]] std::string to_metadata_json() const;
    bool export_all(const std::string& output_dir) const;
};

/**
 * @brief Measures current process working set size in bytes via OS API.
 */
[[nodiscard]] std::size_t get_process_working_set_bytes();

/**
 * @brief Executes the complete Round 2 Phase 2D comparative benchmark across
 *        Model B (Cube) and Model C (Finned Sink) at resolutions N = 10, 20, 30.
 * 
 * Guarantees:
 * - Identical system (A, b) solved by all 3 solvers per (Geometry, N).
 * - 1 warmup run + 5 measured runs.
 * - Median reporting.
 * - Strict separation of assembly, conversion, ordering, factorization, and solve timings.
 * - Exact application memory accounting distinct from OS working set.
 * - Direct solvers report iterations as "N/A".
 * - Solution agreement and energy conservation validation.
 */
[[nodiscard]] ComparativeBenchmarkSuite run_comparative_benchmark_suite(
    const std::vector<int>& resolutions = {10, 20, 30},
    int timing_repeats = 5,
    bool verbose = true,
    double solver_tolerance = 1e-8);

} // namespace workbench
