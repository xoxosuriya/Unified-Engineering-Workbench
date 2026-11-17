#include "comparative_benchmark_3d.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#endif

#ifndef WORKBENCH_COMPILER_INFO
#define WORKBENCH_COMPILER_INFO "MinGW GCC"
#endif

#ifndef WORKBENCH_OPT_FLAGS
#define WORKBENCH_OPT_FLAGS "-O3"
#endif

namespace workbench {

std::size_t get_process_working_set_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
#elif defined(__linux__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<std::size_t>(usage.ru_maxrss) * 1024;
    }
#endif
    return 0;
}

static BenchmarkMemoryMetrics compute_memory_metrics(const CSRMatrix& A,
                                                     const ProfileCholesky* chol,
                                                     const std::string& solver_type) {
    BenchmarkMemoryMetrics mem;
    mem.active_dofs_M = A.num_rows;
    mem.matrix_nnz = A.nnz();

    // 1. Exact CSR heap allocation: values + col_idx + row_ptr
    mem.csr_allocated_bytes = sizeof(CSRMatrix) +
        A.values.capacity() * sizeof(double) +
        A.col_idx.capacity() * sizeof(int) +
        A.row_ptr.capacity() * sizeof(std::size_t);

    // 2. Cholesky skyline factor allocation
    if (chol && chol->is_factored()) {
        const auto& m = chol->get_metrics();
        mem.factor_envelope_capacity = m.envelope_capacity;
        mem.factor_nnz = m.numerical_factor_nnz;
        mem.factor_fill_ratio = (mem.matrix_nnz > 0) ? 
            (static_cast<double>(mem.factor_nnz) / static_cast<double>(mem.matrix_nnz)) : 0.0;
        mem.factor_allocated_bytes = m.allocated_factor_bytes;
    }

    // 3. Iterative solver buffer bytes (vectors p, z, r, x, precond diag, temp arrays)
    if (solver_type == "Eigen_PCG_Jacobi") {
        // Eigen PCG allocates: p, z, r, x, inv_diag (6 vectors of length M)
        mem.iterative_buffer_bytes = 6 * A.num_rows * sizeof(double);
    } else if (solver_type == "Custom_SOR") {
        // SOR allocates: x, temp_res, diag (3 vectors of length M)
        mem.iterative_buffer_bytes = 3 * A.num_rows * sizeof(double);
    } else {
        mem.iterative_buffer_bytes = 0; // Direct solver
    }

    // 4. OS Process Working Set (distinct from application heap accounting)
    mem.process_working_set_bytes = get_process_working_set_bytes();

    return mem;
}

ComparativeBenchmarkSuite run_comparative_benchmark_suite(
    const std::vector<int>& resolutions,
    int timing_repeats,
    bool verbose,
    double solver_tolerance) {

    ComparativeBenchmarkSuite suite;
    if (timing_repeats <= 0) timing_repeats = 5;

    const std::vector<std::pair<ModelType, std::string>> geometries = {
        {ModelType::CUBE, "Cube"},
        {ModelType::FINNED_HEAT_SINK, "Finned_Heat_Sink"}
    };

    for (const auto& [geom_type, geom_name] : geometries) {
        for (int N : resolutions) {
            if (verbose) {
                std::cout << "\n=================================================================\n";
                std::cout << " BENCHMARKING: " << geom_name << " (N = " << N << ")\n";
                std::cout << "=================================================================\n";
            }

            // Step 1: Discrete assembly (assembled once per geometry/N for experimental fairness)
            ThermalGeometry3D geom(geom_type, N);
            CSRMatrix A;
            std::vector<double> b;
            double t_asm_ms = 0.0;
            geom.assemble_system(A, b, &t_asm_ms);

            const std::size_t M = A.num_rows;
            const std::size_t nnz = A.nnz();
            const double b_norm = vector_norm2(b);

            if (verbose) {
                std::cout << "  Active DOFs M = " << M << ", NNZ = " << nnz 
                          << ", Assembly Time = " << std::fixed << std::setprecision(3) << t_asm_ms << " ms\n";
            }

            std::vector<double> x_chol_final;
            std::vector<double> x_pcg_final;
            std::vector<double> x_sor_final;

            // =========================================================
            // Solver 1: Custom Profile Cholesky with RCM
            // =========================================================
            {
                if (verbose) std::cout << "  -> Running Profile Cholesky (RCM)...\n";
                BenchmarkCaseResult case_res;
                case_res.geometry_name = geom_name;
                case_res.N = N;
                case_res.solver_name = "Profile_Cholesky_RCM";

                // Warmup run (index 0)
                {
                    ProfileCholesky chol_warmup;
                    BenchmarkRunRecord rec;
                    rec.geometry_name = geom_name;
                    rec.N = N;
                    rec.solver_name = "Profile_Cholesky_RCM";
                    rec.run_type = "warmup";
                    rec.run_index = 0;
                    rec.timings.t_asm_ms = t_asm_ms;

                    // Warmup envelope timing
                    auto t_env_w0 = std::chrono::high_resolution_clock::now();
                    std::vector<std::size_t> col_start_w(M, 0);
                    std::vector<std::size_t> profile_row_ptr_w(M + 1, 0);
                    for (std::size_t i = 0; i < M; ++i) {
                        std::size_t min_col = i;
                        const std::size_t start = A.row_ptr[i];
                        const std::size_t end = A.row_ptr[i + 1];
                        for (std::size_t p = start; p < end; ++p) {
                            std::size_t j = static_cast<std::size_t>(A.col_idx[p]);
                            if (j <= i && j < min_col) min_col = j;
                        }
                        col_start_w[i] = min_col;
                        profile_row_ptr_w[i + 1] = profile_row_ptr_w[i] + (i - min_col + 1);
                    }
                    auto t_env_w1 = std::chrono::high_resolution_clock::now();
                    rec.timings.t_env_ms = std::chrono::duration<double, std::milli>(t_env_w1 - t_env_w0).count();

                    auto t0 = std::chrono::high_resolution_clock::now();
                    ProfileCholesky::Status stat = chol_warmup.factor(A, true);
                    auto t1 = std::chrono::high_resolution_clock::now();
                    rec.timings.t_fac_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                    std::vector<double> x_w;
                    auto t2 = std::chrono::high_resolution_clock::now();
                    bool ok = chol_warmup.solve(b, x_w);
                    auto t3 = std::chrono::high_resolution_clock::now();
                    rec.timings.t_sol_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
                    rec.timings.t_total_solve_ms = rec.timings.t_fac_ms + rec.timings.t_sol_ms;

                    rec.iterations_str = "N/A";
                    rec.iterations = 0;
                    rec.status = (stat == ProfileCholesky::Status::SUCCESS && ok) ? 
                        SolverStatus::SUCCESS : SolverStatus::FAILED_FACTORIZATION;

                    // Evaluate residual
                    std::vector<double> Ax(M, 0.0);
                    A.matvec(x_w.data(), Ax.data());
                    double r2 = 0.0;
                    for (std::size_t i = 0; i < M; ++i) { double r = b[i] - Ax[i]; r2 += r * r; }
                    rec.final_residual = std::sqrt(r2);
                    rec.relative_residual = (b_norm > 0.0) ? (rec.final_residual / b_norm) : 0.0;
                    rec.memory = compute_memory_metrics(A, &chol_warmup, rec.solver_name);

                    case_res.runs.push_back(rec);
                }

                // 5 Measured Runs (indices 1..timing_repeats)
                for (int rep = 1; rep <= timing_repeats; ++rep) {
                    ProfileCholesky chol;
                    BenchmarkRunRecord rec;
                    rec.geometry_name = geom_name;
                    rec.N = N;
                    rec.solver_name = "Profile_Cholesky_RCM";
                    rec.run_type = "measured";
                    rec.run_index = rep;
                    rec.timings.t_asm_ms = t_asm_ms;

                    // Measure RCM ordering time independently
                    auto t_rcm0 = std::chrono::high_resolution_clock::now();
                    auto rcm_res = RCMOrdering::compute(A);
                    auto t_rcm1 = std::chrono::high_resolution_clock::now();
                    rec.timings.t_rcm_ms = std::chrono::duration<double, std::milli>(t_rcm1 - t_rcm0).count();
                    (void)rcm_res;

                    // Measure envelope computation time independently
                    auto t_env0 = std::chrono::high_resolution_clock::now();
                    std::vector<std::size_t> col_start_m(M, 0);
                    std::vector<std::size_t> profile_row_ptr_m(M + 1, 0);
                    for (std::size_t i = 0; i < M; ++i) {
                        std::size_t min_col = i;
                        const std::size_t start = A.row_ptr[i];
                        const std::size_t end = A.row_ptr[i + 1];
                        for (std::size_t p = start; p < end; ++p) {
                            std::size_t j = static_cast<std::size_t>(A.col_idx[p]);
                            if (j <= i && j < min_col) min_col = j;
                        }
                        col_start_m[i] = min_col;
                        profile_row_ptr_m[i + 1] = profile_row_ptr_m[i] + (i - min_col + 1);
                    }
                    auto t_env1 = std::chrono::high_resolution_clock::now();
                    rec.timings.t_env_ms = std::chrono::duration<double, std::milli>(t_env1 - t_env0).count();

                    // Measure numerical factorization time independently
                    auto t_fac0 = std::chrono::high_resolution_clock::now();
                    ProfileCholesky::Status stat = chol.factor(A, true);
                    auto t_fac1 = std::chrono::high_resolution_clock::now();
                    rec.timings.t_fac_ms = std::chrono::duration<double, std::milli>(t_fac1 - t_fac0).count();

                    // Measure forward/backward triangular solve time independently
                    std::vector<double> x_sol;
                    auto t_sol0 = std::chrono::high_resolution_clock::now();
                    bool ok = chol.solve(b, x_sol);
                    auto t_sol1 = std::chrono::high_resolution_clock::now();
                    rec.timings.t_sol_ms = std::chrono::duration<double, std::milli>(t_sol1 - t_sol0).count();
                    rec.timings.t_total_solve_ms = rec.timings.t_fac_ms + rec.timings.t_sol_ms;

                    rec.iterations_str = "N/A";
                    rec.iterations = 0;
                    rec.status = (stat == ProfileCholesky::Status::SUCCESS && ok) ? 
                        SolverStatus::SUCCESS : SolverStatus::FAILED_FACTORIZATION;

                    // Evaluate true residual
                    std::vector<double> Ax(M, 0.0);
                    A.matvec(x_sol.data(), Ax.data());
                    double r2 = 0.0;
                    for (std::size_t i = 0; i < M; ++i) { double r = b[i] - Ax[i]; r2 += r * r; }
                    rec.final_residual = std::sqrt(r2);
                    rec.relative_residual = (b_norm > 0.0) ? (rec.final_residual / b_norm) : 0.0;
                    rec.memory = compute_memory_metrics(A, &chol, rec.solver_name);

                    case_res.runs.push_back(rec);
                    if (rep == 1) {
                        x_chol_final = x_sol;
                        case_res.solution = x_sol;
                    }
                }

                // Compute median record
                std::vector<BenchmarkRunRecord> measured_runs(case_res.runs.begin() + 1, case_res.runs.end());
                std::sort(measured_runs.begin(), measured_runs.end(), [](const auto& a, const auto& b) {
                    return a.timings.t_total_solve_ms < b.timings.t_total_solve_ms;
                });
                case_res.median_record = measured_runs[measured_runs.size() / 2];
                case_res.median_record.run_type = "median";
                case_res.median_record.run_index = -1;

                suite.cases.push_back(case_res);

                if (verbose) {
                    std::cout << "    [Cholesky Median] t_fac = " << std::fixed << std::setprecision(3) 
                              << case_res.median_record.timings.t_fac_ms << " ms, t_sol = " 
                              << case_res.median_record.timings.t_sol_ms << " ms, envelope = " 
                              << case_res.median_record.memory.factor_envelope_capacity 
                              << " (" << (case_res.median_record.memory.factor_allocated_bytes / (1024*1024)) << " MB)\n";
                }
            }

            // =========================================================
            // Solver 2: Eigen PCG with Jacobi Preconditioner
            // =========================================================
            {
                if (verbose) std::cout << "  -> Running Eigen PCG (Jacobi)...\n";
                BenchmarkCaseResult case_res;
                case_res.geometry_name = geom_name;
                case_res.N = N;
                case_res.solver_name = "Eigen_PCG_Jacobi";

                PCGConfig pcg_cfg;
                pcg_cfg.tolerance = solver_tolerance;
                pcg_cfg.max_iterations = 10000;
                EigenPCGSolver pcg(pcg_cfg);

                // Warmup run (index 0)
                {
                    BenchmarkRunRecord rec;
                    rec.geometry_name = geom_name;
                    rec.N = N;
                    rec.solver_name = "Eigen_PCG_Jacobi";
                    rec.run_type = "warmup";
                    rec.run_index = 0;
                    rec.timings.t_asm_ms = t_asm_ms;

                    std::vector<double> x_w;
                    SolverResult s_res = pcg.solve(A, b, x_w);
                    rec.timings.t_convert_ms = s_res.conversion_time_ms;
                    rec.timings.t_setup_ms = s_res.setup_time_ms;
                    rec.timings.t_sol_ms = s_res.solve_time_ms;
                    rec.timings.t_total_solve_ms = rec.timings.t_convert_ms + rec.timings.t_setup_ms + rec.timings.t_sol_ms;
                    rec.iterations = s_res.iterations;
                    rec.iterations_str = std::to_string(s_res.iterations);
                    rec.initial_residual = s_res.initial_residual;
                    rec.final_residual = s_res.final_residual;
                    rec.relative_residual = s_res.relative_residual;
                    rec.status = s_res.status;
                    rec.memory = compute_memory_metrics(A, nullptr, rec.solver_name);

                    case_res.runs.push_back(rec);
                }

                // 5 Measured Runs
                for (int rep = 1; rep <= timing_repeats; ++rep) {
                    BenchmarkRunRecord rec;
                    rec.geometry_name = geom_name;
                    rec.N = N;
                    rec.solver_name = "Eigen_PCG_Jacobi";
                    rec.run_type = "measured";
                    rec.run_index = rep;
                    rec.timings.t_asm_ms = t_asm_ms;

                    std::vector<double> x_sol;
                    SolverResult s_res = pcg.solve(A, b, x_sol);
                    rec.timings.t_convert_ms = s_res.conversion_time_ms;
                    rec.timings.t_setup_ms = s_res.setup_time_ms;
                    rec.timings.t_sol_ms = s_res.solve_time_ms;
                    rec.timings.t_total_solve_ms = rec.timings.t_convert_ms + rec.timings.t_setup_ms + rec.timings.t_sol_ms;
                    rec.iterations = s_res.iterations;
                    rec.iterations_str = std::to_string(s_res.iterations);
                    rec.initial_residual = s_res.initial_residual;
                    rec.final_residual = s_res.final_residual;
                    rec.relative_residual = s_res.relative_residual;
                    rec.status = s_res.status;
                    rec.memory = compute_memory_metrics(A, nullptr, rec.solver_name);

                    case_res.runs.push_back(rec);
                    if (rep == 1) {
                        x_pcg_final = x_sol;
                        case_res.solution = x_sol;
                    }
                }

                // Compute median record based on total solve time
                std::vector<BenchmarkRunRecord> measured_runs(case_res.runs.begin() + 1, case_res.runs.end());
                std::sort(measured_runs.begin(), measured_runs.end(), [](const auto& a, const auto& b) {
                    return a.timings.t_sol_ms < b.timings.t_sol_ms;
                });
                case_res.median_record = measured_runs[measured_runs.size() / 2];
                case_res.median_record.run_type = "median";
                case_res.median_record.run_index = -1;

                suite.cases.push_back(case_res);

                if (verbose) {
                    std::cout << "    [PCG Median] iters = " << case_res.median_record.iterations 
                              << ", t_convert = " << case_res.median_record.timings.t_convert_ms << " ms"
                              << ", t_setup = " << case_res.median_record.timings.t_setup_ms << " ms"
                              << ", t_sol = " << case_res.median_record.timings.t_sol_ms << " ms"
                              << ", rel_res = " << case_res.median_record.relative_residual << "\n";
                }
            }

            // =========================================================
            // Solver 3: Custom Successive Over-Relaxation (SOR)
            // =========================================================
            {
                if (verbose) std::cout << "  -> Running Custom SOR Omega Sweep & Benchmark...\n";
                // Execute empirical parameter sweep across omega in [1.10, 1.95]
                OmegaSweepResult sweep = run_sor_omega_sweep(A, b, 1e-6, 10000, timing_repeats, N);
                BenchmarkSorSweepRecord sweep_rec;
                sweep_rec.geometry_name = geom_name;
                sweep_rec.N = N;
                sweep_rec.active_dofs_M = M;
                sweep_rec.sweep = sweep;
                suite.sor_sweeps.push_back(sweep_rec);

                const double opt_omega = (sweep.omega_iter >= 1.10 && sweep.omega_iter <= 1.95) ? 
                    sweep.omega_iter : 1.70;

                BenchmarkCaseResult case_res;
                case_res.geometry_name = geom_name;
                case_res.N = N;
                case_res.solver_name = "Custom_SOR";

                SORSolver sor(opt_omega, solver_tolerance, 10000);

                // Warmup run (index 0)
                {
                    BenchmarkRunRecord rec;
                    rec.geometry_name = geom_name;
                    rec.N = N;
                    rec.solver_name = "Custom_SOR";
                    rec.run_type = "warmup";
                    rec.run_index = 0;
                    rec.timings.t_asm_ms = t_asm_ms;

                    std::vector<double> x_w;
                    SolverResult s_res = sor.solve(A, b, x_w);
                    rec.timings.t_sol_ms = s_res.solve_time_ms;
                    rec.timings.t_total_solve_ms = s_res.solve_time_ms;
                    rec.iterations = s_res.iterations;
                    rec.iterations_str = std::to_string(s_res.iterations);
                    rec.initial_residual = s_res.initial_residual;
                    rec.final_residual = s_res.final_residual;
                    rec.relative_residual = s_res.relative_residual;
                    rec.status = s_res.status;
                    rec.memory = compute_memory_metrics(A, nullptr, rec.solver_name);

                    case_res.runs.push_back(rec);
                }

                // 5 Measured Runs
                for (int rep = 1; rep <= timing_repeats; ++rep) {
                    BenchmarkRunRecord rec;
                    rec.geometry_name = geom_name;
                    rec.N = N;
                    rec.solver_name = "Custom_SOR";
                    rec.run_type = "measured";
                    rec.run_index = rep;
                    rec.timings.t_asm_ms = t_asm_ms;

                    std::vector<double> x_sol;
                    SolverResult s_res = sor.solve(A, b, x_sol);
                    rec.timings.t_sol_ms = s_res.solve_time_ms;
                    rec.timings.t_total_solve_ms = s_res.solve_time_ms;
                    rec.iterations = s_res.iterations;
                    rec.iterations_str = std::to_string(s_res.iterations);
                    rec.initial_residual = s_res.initial_residual;
                    rec.final_residual = s_res.final_residual;
                    rec.relative_residual = s_res.relative_residual;
                    rec.status = s_res.status;
                    rec.memory = compute_memory_metrics(A, nullptr, rec.solver_name);

                    case_res.runs.push_back(rec);
                    if (rep == 1) {
                        x_sor_final = x_sol;
                        case_res.solution = x_sol;
                    }
                }

                // Compute median record
                std::vector<BenchmarkRunRecord> measured_runs(case_res.runs.begin() + 1, case_res.runs.end());
                std::sort(measured_runs.begin(), measured_runs.end(), [](const auto& a, const auto& b) {
                    return a.timings.t_sol_ms < b.timings.t_sol_ms;
                });
                case_res.median_record = measured_runs[measured_runs.size() / 2];
                case_res.median_record.run_type = "median";
                case_res.median_record.run_index = -1;

                suite.cases.push_back(case_res);

                if (verbose) {
                    std::cout << "    [SOR Median (omega=" << std::fixed << std::setprecision(2) << opt_omega << ")] iters = " 
                              << case_res.median_record.iterations << ", t_sol = " 
                              << case_res.median_record.timings.t_sol_ms << " ms, rel_res = " 
                              << case_res.median_record.relative_residual << "\n";
                }
            }

            // =========================================================
            // Cross-Solver Agreement and Energy Conservation Checks
            // =========================================================
            if (!x_chol_final.empty() && !x_pcg_final.empty() && !x_sor_final.empty()) {
                double diff_chol_pcg = 0.0;
                double diff_pcg_sor  = 0.0;
                double diff_chol_sor = 0.0;

                for (std::size_t i = 0; i < M; ++i) {
                    diff_chol_pcg = std::max(diff_chol_pcg, std::abs(x_chol_final[i] - x_pcg_final[i]));
                    diff_pcg_sor  = std::max(diff_pcg_sor,  std::abs(x_pcg_final[i]  - x_sor_final[i]));
                    diff_chol_sor = std::max(diff_chol_sor, std::abs(x_chol_final[i] - x_sor_final[i]));
                }
                double max_local_diff = std::max({diff_chol_pcg, diff_pcg_sor, diff_chol_sor});
                suite.max_pairwise_diff_C = std::max(suite.max_pairwise_diff_C, max_local_diff);

                if (verbose) {
                    std::cout << "    [Pairwise Max Agreement] " << std::scientific << std::setprecision(3)
                              << max_local_diff << " deg C (Threshold: < 1e-4 deg C)\n";
                }
            }

            // Analytical Cube Error Check at N=20
            if (geom_type == ModelType::CUBE && N == 20) {
                suite.cube_analytical_error_N20 = geom.compute_max_error_cube(x_pcg_final);
                if (verbose) {
                    std::cout << "    [Cube N=20 Max Error] " << suite.cube_analytical_error_N20 
                              << " deg C (Threshold: < 0.05 deg C)\n";
                }
            }

            // Energy Conservation Balance at N=30 for Finned Sink
            if (geom_type == ModelType::FINNED_HEAT_SINK && N == 30) {
                double qg = 0.0, qb = 0.0, qc = 0.0, rel_err = 0.0;
                bool eb_ok = geom.verify_energy_balance_finned(x_pcg_final, qg, qb, qc, rel_err);
                (void)eb_ok;
                suite.finned_energy_balance_rel_err_N30 = rel_err;
                if (verbose) {
                    std::cout << "    [Finned N=30 Energy Balance Rel Error] " << std::scientific 
                              << rel_err * 100.0 << "% (Threshold: <= 0.1%)\n";
                }
            }
        }
    }

    // Verify all rows contain finite values
    suite.all_valid_finite = true;
    for (const auto& c : suite.cases) {
        for (const auto& r : c.runs) {
            if (!std::isfinite(r.timings.t_sol_ms) || !std::isfinite(r.relative_residual) ||
                !std::isfinite(r.timings.t_total_solve_ms)) {
                suite.all_valid_finite = false;
            }
        }
    }

    return suite;
}

std::string ComparativeBenchmarkSuite::to_detailed_csv() const {
    std::ostringstream ss;
    ss << "geometry,N,active_dofs_M,matrix_nnz,solver,run_type,run_index,"
       << "t_asm_ms,t_convert_ms,t_sym_rcm_ms,t_env_ms,t_fac_ms,t_setup_ms,t_sol_ms,t_total_solve_ms,"
       << "iterations,initial_residual,final_residual,relative_residual,status,"
       << "csr_allocated_bytes,factor_envelope_capacity,factor_allocated_bytes,factor_fill_ratio,"
       << "iterative_buffer_bytes,process_working_set_bytes\n";

    for (const auto& c : cases) {
        // Output all individual runs
        for (const auto& r : c.runs) {
            ss << r.geometry_name << ","
               << r.N << ","
               << r.memory.active_dofs_M << ","
               << r.memory.matrix_nnz << ","
               << r.solver_name << ","
               << r.run_type << ","
               << r.run_index << ","
               << std::fixed << std::setprecision(4)
               << r.timings.t_asm_ms << ","
               << r.timings.t_convert_ms << ","
               << r.timings.t_rcm_ms << ","
               << r.timings.t_env_ms << ","
               << r.timings.t_fac_ms << ","
               << r.timings.t_setup_ms << ","
               << r.timings.t_sol_ms << ","
               << r.timings.t_total_solve_ms << ","
               << r.iterations_str << ","
               << std::scientific << std::setprecision(6)
               << r.initial_residual << ","
               << r.final_residual << ","
               << r.relative_residual << ","
               << (r.status == SolverStatus::SUCCESS || r.status == SolverStatus::CONVERGED ? "CONVERGED" : "FAILED") << ","
               << r.memory.csr_allocated_bytes << ","
               << r.memory.factor_envelope_capacity << ","
               << r.memory.factor_allocated_bytes << ","
               << std::fixed << std::setprecision(4) << r.memory.factor_fill_ratio << ","
               << r.memory.iterative_buffer_bytes << ","
               << r.memory.process_working_set_bytes << "\n";
        }

        // Output median row
        const auto& m = c.median_record;
        ss << m.geometry_name << ","
           << m.N << ","
           << m.memory.active_dofs_M << ","
           << m.memory.matrix_nnz << ","
           << m.solver_name << ","
           << "median,"
           << -1 << ","
           << std::fixed << std::setprecision(4)
           << m.timings.t_asm_ms << ","
           << m.timings.t_convert_ms << ","
           << m.timings.t_rcm_ms << ","
           << m.timings.t_env_ms << ","
           << m.timings.t_fac_ms << ","
           << m.timings.t_setup_ms << ","
           << m.timings.t_sol_ms << ","
           << m.timings.t_total_solve_ms << ","
           << m.iterations_str << ","
           << std::scientific << std::setprecision(6)
           << m.initial_residual << ","
           << m.final_residual << ","
           << m.relative_residual << ","
           << (m.status == SolverStatus::SUCCESS || m.status == SolverStatus::CONVERGED ? "CONVERGED" : "FAILED") << ","
           << m.memory.csr_allocated_bytes << ","
           << m.memory.factor_envelope_capacity << ","
           << m.memory.factor_allocated_bytes << ","
           << std::fixed << std::setprecision(4) << m.memory.factor_fill_ratio << ","
           << m.memory.iterative_buffer_bytes << ","
           << m.memory.process_working_set_bytes << "\n";
    }

    return ss.str();
}

std::string ComparativeBenchmarkSuite::to_summary_csv() const {
    std::ostringstream ss;
    ss << "geometry,N,M,NNZ,solver,"
       << "median_t_asm_ms,median_t_convert_ms,median_t_sym_rcm_ms,median_t_fac_ms,median_t_setup_ms,"
       << "median_t_sol_ms,median_t_total_ms,iterations,final_relative_residual,status,"
       << "csr_mem_mb,factor_mem_mb,factor_fill_ratio,iterative_mem_mb,working_set_mb\n";

    for (const auto& c : cases) {
        const auto& m = c.median_record;
        double csr_mb = static_cast<double>(m.memory.csr_allocated_bytes) / (1024.0 * 1024.0);
        double fac_mb = static_cast<double>(m.memory.factor_allocated_bytes) / (1024.0 * 1024.0);
        double iter_mb = static_cast<double>(m.memory.iterative_buffer_bytes) / (1024.0 * 1024.0);
        double ws_mb = static_cast<double>(m.memory.process_working_set_bytes) / (1024.0 * 1024.0);

        ss << m.geometry_name << ","
           << m.N << ","
           << m.memory.active_dofs_M << ","
           << m.memory.matrix_nnz << ","
           << m.solver_name << ","
           << std::fixed << std::setprecision(3)
           << m.timings.t_asm_ms << ","
           << m.timings.t_convert_ms << ","
           << m.timings.t_rcm_ms << ","
           << m.timings.t_fac_ms << ","
           << m.timings.t_setup_ms << ","
           << m.timings.t_sol_ms << ","
           << m.timings.t_total_solve_ms << ","
           << m.iterations_str << ","
           << std::scientific << std::setprecision(4)
           << m.relative_residual << ","
           << (m.status == SolverStatus::SUCCESS || m.status == SolverStatus::CONVERGED ? "CONVERGED" : "FAILED") << ","
           << std::fixed << std::setprecision(3)
           << csr_mb << ","
           << fac_mb << ","
           << std::setprecision(2) << m.memory.factor_fill_ratio << ","
           << std::setprecision(3) << iter_mb << ","
           << ws_mb << "\n";
    }

    return ss.str();
}

std::string ComparativeBenchmarkSuite::to_sor_sweep_csv() const {
    std::ostringstream ss;
    ss << "geometry,N,active_dofs_M,omega,iterations,run1_ms,run2_ms,run3_ms,run4_ms,run5_ms,"
       << "median_runtime_ms,relative_residual,status,is_omega_iter,is_omega_time\n";

    for (const auto& rec : sor_sweeps) {
        const auto& sweep = rec.sweep;
        for (const auto& pt : sweep.points) {
            bool is_iter = (pt.omega == sweep.omega_iter);
            bool is_time = (pt.omega == sweep.omega_time);

            ss << rec.geometry_name << ","
               << rec.N << ","
               << rec.active_dofs_M << ","
               << std::fixed << std::setprecision(2) << pt.omega << ","
               << pt.iterations << ",";
            for (int r = 0; r < 5; ++r) {
                double t_val = (r < static_cast<int>(pt.runtimes_ms.size())) ? pt.runtimes_ms[r] : 0.0;
                ss << std::fixed << std::setprecision(3) << t_val << ",";
            }
            ss << std::fixed << std::setprecision(3) << pt.median_runtime_ms << ","
               << std::scientific << std::setprecision(4) << pt.relative_residual << ","
               << (pt.status == SolverStatus::CONVERGED ? "CONVERGED" : "DIVERGED") << ","
               << (is_iter ? 1 : 0) << ","
               << (is_time ? 1 : 0) << "\n";
        }
    }

    return ss.str();
}

std::string ComparativeBenchmarkSuite::to_metadata_json() const {
    std::ostringstream ss;
    ss << "{\n"
       << "  \"benchmark_suite\": \"Round 2 Phase 2D Comparative Benchmarking\",\n"
       << "  \"timestamp\": \"" << __DATE__ << " " << __TIME__ << "\",\n"
       << "  \"compiler\": \"" << WORKBENCH_COMPILER_INFO << "\",\n"
       << "  \"optimization_flags\": \"" << WORKBENCH_OPT_FLAGS << "\",\n"
       << "  \"working_set_bytes\": " << get_process_working_set_bytes() << ",\n"
       << "  \"validation\": {\n"
       << "    \"max_pairwise_solution_difference_C\": " << max_pairwise_diff_C << ",\n"
       << "    \"pairwise_tolerance_C\": 1e-4,\n"
       << "    \"pairwise_agreement_passed\": " << (max_pairwise_diff_C < 1e-4 ? "true" : "false") << ",\n"
       << "    \"cube_analytical_error_N20_C\": " << cube_analytical_error_N20 << ",\n"
       << "    \"cube_analytical_tolerance_C\": 0.05,\n"
       << "    \"cube_analytical_passed\": " << (cube_analytical_error_N20 < 0.05 ? "true" : "false") << ",\n"
       << "    \"finned_energy_balance_rel_err_N30\": " << finned_energy_balance_rel_err_N30 << ",\n"
       << "    \"finned_energy_tolerance\": 0.001,\n"
       << "    \"finned_energy_balance_passed\": " << (finned_energy_balance_rel_err_N30 <= 0.001 ? "true" : "false") << ",\n"
       << "    \"all_finite_measurements\": " << (all_valid_finite ? "true" : "false") << "\n"
       << "  }\n"
       << "}\n";
    return ss.str();
}

bool ComparativeBenchmarkSuite::export_all(const std::string& output_dir) const {
    std::filesystem::create_directories(output_dir);

    std::ofstream f_det(output_dir + "/benchmark_solvers_3d.csv");
    if (!f_det.is_open()) return false;
    f_det << to_detailed_csv();

    std::ofstream f_sum(output_dir + "/benchmark_summary_3d.csv");
    if (!f_sum.is_open()) return false;
    f_sum << to_summary_csv();

    std::ofstream f_sor(output_dir + "/benchmark_sor_sweep_3d.csv");
    if (!f_sor.is_open()) return false;
    f_sor << to_sor_sweep_csv();

    std::ofstream f_meta(output_dir + "/benchmark_metadata_3d.json");
    if (!f_meta.is_open()) return false;
    f_meta << to_metadata_json();

    return true;
}

} // namespace workbench
