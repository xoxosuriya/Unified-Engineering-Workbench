#include "thermal_solvers_3d.hpp"
#include <cmath>
#include <limits>
#include <algorithm>

namespace workbench {

bool convert_csr_to_eigen(const CSRMatrix& A,
                          Eigen::SparseMatrix<double, Eigen::RowMajor, int>& eigen_mat,
                          double* conversion_time_ms,
                          std::string* error_msg) {
    auto report_err = [&](const std::string& msg) {
        if (error_msg) *error_msg = msg;
        if (conversion_time_ms) *conversion_time_ms = 0.0;
        return false;
    };

    auto t_start = std::chrono::high_resolution_clock::now();

    // 1. Structural CSR validation
    std::string val_err;
    if (!A.is_valid(&val_err)) {
        return report_err("CSR structural validation failed: " + val_err);
    }

    // 2. Bounds and dimension checks for 32-bit integer indexing in Eigen
    if (A.num_rows == 0 || A.num_cols == 0) {
        return report_err("Matrix dimensions must be non-zero");
    }
    if (A.num_rows > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        A.num_cols > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return report_err("Matrix dimensions exceed INT_MAX for Eigen int indexing");
    }
    if (A.nnz() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return report_err("Matrix NNZ exceeds INT_MAX for Eigen int indexing");
    }

    const int rows = static_cast<int>(A.num_rows);
    const int cols = static_cast<int>(A.num_cols);

    // 3. Invariant and non-finite value checks
    for (std::size_t i = 0; i < A.num_rows; ++i) {
        const std::size_t start = A.row_ptr[i];
        const std::size_t end = A.row_ptr[i + 1];
        if (start > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            end > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return report_err("Row pointer exceeds INT_MAX at row " + std::to_string(i));
        }
        for (std::size_t p = start; p < end; ++p) {
            int c = A.col_idx[p];
            if (c < 0 || c >= cols) {
                return report_err("Column index " + std::to_string(c) + " out of bounds at row " + std::to_string(i));
            }
            double val = A.values[p];
            if (!std::isfinite(val)) {
                return report_err("Non-finite value detected at row " + std::to_string(i) + ", col " + std::to_string(c));
            }
        }
    }

    // 4. Fast O(NNZ) compressed insertion using Eigen low-level sequential API
    eigen_mat.resize(rows, cols);
    eigen_mat.reserve(static_cast<Eigen::Index>(A.nnz()));

    for (int i = 0; i < rows; ++i) {
        eigen_mat.startVec(i);
        const std::size_t start = A.row_ptr[i];
        const std::size_t end = A.row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            eigen_mat.insertBack(i, A.col_idx[p]) = A.values[p];
        }
    }
    eigen_mat.finalize();

    auto t_end = std::chrono::high_resolution_clock::now();
    if (conversion_time_ms) {
        *conversion_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    }
    if (error_msg) error_msg->clear();
    return true;
}

// =========================================================================
// EigenPCGSolver Implementation
// =========================================================================

EigenPCGSolver::EigenPCGSolver(PCGConfig config)
    : config_(config) {}

SolverResult EigenPCGSolver::solve(const CSRMatrix& A,
                                   const std::vector<double>& b,
                                   std::vector<double>& x,
                                   const std::vector<double>* x0) {
    SolverResult result;
    result.solver_name = get_name();
    x.assign(b.size(), 0.0);

    // 1. Dimensionality and basic validation
    if (A.num_rows != A.num_cols || A.num_rows == 0) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Matrix must be square and non-empty (" + 
                               std::to_string(A.num_rows) + " x " + std::to_string(A.num_cols) + ")";
        return result;
    }
    if (b.size() != A.num_rows) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "RHS dimension mismatch: b size (" + std::to_string(b.size()) + 
                               ") != matrix dim (" + std::to_string(A.num_rows) + ")";
        return result;
    }

    // 2. Reject non-finite RHS values immediately
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (!std::isfinite(b[i])) {
            result.status = SolverStatus::FAILED_INPUT_VALIDATION;
            result.error_message = "RHS contains non-finite value at index " + std::to_string(i);
            return result;
        }
    }

    // 3. Initial guess validation if provided
    if (x0 != nullptr) {
        if (x0->size() != b.size()) {
            result.status = SolverStatus::FAILED_INPUT_VALIDATION;
            result.error_message = "Initial guess size mismatch: x0 (" + std::to_string(x0->size()) + 
                                   ") != b (" + std::to_string(b.size()) + ")";
            return result;
        }
        for (std::size_t i = 0; i < x0->size(); ++i) {
            if (!std::isfinite((*x0)[i])) {
                result.status = SolverStatus::FAILED_INPUT_VALIDATION;
                result.error_message = "Initial guess contains non-finite value at index " + std::to_string(i);
                return result;
            }
        }
    }

    // 4. Preconditioner prerequisite check: diagonal entries must be strictly positive
    if (config_.check_spd) {
        std::string diag_err;
        if (!A.has_strictly_positive_diagonal(&diag_err)) {
            result.status = SolverStatus::FAILED_PRECONDITIONER;
            result.error_message = "Jacobi preconditioner failed: diagonal entries must be strictly positive (" + diag_err + ")";
            return result;
        }
    }

    // 5. Convert CSRMatrix to Eigen RowMajor SparseMatrix with dedicated timing
    Eigen::SparseMatrix<double, Eigen::RowMajor, int> eigen_A;
    std::string conv_err;
    bool conv_ok = convert_csr_to_eigen(A, eigen_A, &result.conversion_time_ms, &conv_err);
    if (!conv_ok) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "CSR to Eigen conversion failed: " + conv_err;
        return result;
    }

    // 6. Compute initial residual r0 = b - A * x0
    double b_norm = vector_norm2(b);
    std::vector<double> r0(b.size());
    if (x0 != nullptr) {
        std::vector<double> Ax0(b.size(), 0.0);
        A.matvec(x0->data(), Ax0.data());
        for (std::size_t i = 0; i < b.size(); ++i) {
            r0[i] = b[i] - Ax0[i];
        }
    } else {
        r0 = b; // x0 = 0 -> r0 = b
    }
    result.initial_residual = vector_norm2(r0);

    // If initial residual is zero (or within machine noise), problem is already solved
    if (result.initial_residual <= 10.0 * EPS_MACH * std::max(b_norm, 1.0)) {
        if (x0 != nullptr) {
            x = *x0;
        } else {
            x.assign(b.size(), 0.0);
        }
        result.final_residual = result.initial_residual;
        result.relative_residual = 0.0;
        result.iterations = 0;
        result.status = SolverStatus::SUCCESS;
        return result;
    }

    // 7. Setup Eigen ConjugateGradient with DiagonalPreconditioner (Jacobi)
    using SpMat = Eigen::SparseMatrix<double, Eigen::RowMajor, int>;
    Eigen::ConjugateGradient<SpMat, Eigen::Lower | Eigen::Upper, Eigen::DiagonalPreconditioner<double>> cg;
    cg.setTolerance(config_.tolerance);
    cg.setMaxIterations(config_.max_iterations);

    auto t_setup_start = std::chrono::high_resolution_clock::now();
    cg.compute(eigen_A);
    auto t_setup_end = std::chrono::high_resolution_clock::now();
    result.setup_time_ms = std::chrono::duration<double, std::milli>(t_setup_end - t_setup_start).count();

    if (cg.info() != Eigen::Success) {
        result.status = SolverStatus::FAILED_PRECONDITIONER;
        result.error_message = "Eigen CG compute/preconditioner failed";
        return result;
    }

    // 8. Execute solve iterations with dedicated solve timer
    Eigen::Map<const Eigen::VectorXd> b_eigen(b.data(), static_cast<Eigen::Index>(b.size()));
    Eigen::VectorXd x_eigen;

    auto t_solve_start = std::chrono::high_resolution_clock::now();
    if (x0 != nullptr) {
        Eigen::Map<const Eigen::VectorXd> x0_eigen(x0->data(), static_cast<Eigen::Index>(x0->size()));
        x_eigen = cg.solveWithGuess(b_eigen, x0_eigen);
    } else {
        x_eigen = cg.solve(b_eigen);
    }
    auto t_solve_end = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration<double, std::milli>(t_solve_end - t_solve_start).count();

    result.iterations = static_cast<int>(cg.iterations());

    // 9. Copy solution back and verify numerical validity
    for (std::size_t i = 0; i < b.size(); ++i) {
        double val = x_eigen[static_cast<Eigen::Index>(i)];
        if (!std::isfinite(val)) {
            result.status = SolverStatus::NUMERICAL_BREAKDOWN;
            result.error_message = "PCG produced non-finite value in solution vector at index " + std::to_string(i);
            x.assign(b.size(), 0.0);
            return result;
        }
        x[i] = val;
    }

    // 10. Objectively evaluate true final residual using independent CSR matvec
    std::vector<double> Ax(b.size(), 0.0);
    A.matvec(x.data(), Ax.data());
    std::vector<double> r_final(b.size());
    for (std::size_t i = 0; i < b.size(); ++i) {
        r_final[i] = b[i] - Ax[i];
    }
    result.final_residual = vector_norm2(r_final);

    if (b_norm > 0.0) {
        result.relative_residual = result.final_residual / b_norm;
    } else {
        result.relative_residual = (result.initial_residual > 0.0) ? (result.final_residual / result.initial_residual) : 0.0;
    }

    // 11. Evaluate convergence status against Eigen info and residual bounds
    if (result.relative_residual > 1e4) {
        result.status = SolverStatus::DIVERGED_RESIDUAL;
        result.error_message = "PCG residual diverged (relative residual: " + std::to_string(result.relative_residual) + ")";
        return result;
    }

    if (cg.info() == Eigen::Success) {
        result.status = SolverStatus::SUCCESS;
    } else if (cg.info() == Eigen::NoConvergence) {
        result.status = SolverStatus::MAX_ITERATIONS_REACHED;
        result.error_message = "PCG reached maximum iterations (" + std::to_string(config_.max_iterations) + 
                               ") without meeting tolerance (" + std::to_string(config_.tolerance) + ")";
    } else if (cg.info() == Eigen::NumericalIssue) {
        result.status = SolverStatus::NUMERICAL_BREAKDOWN;
        result.error_message = "PCG encountered numerical issue during iteration";
    } else {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Eigen reported invalid input during solve";
    }

    return result;
}

// =========================================================================
// ProfileCholeskySolver Implementation
// =========================================================================

ProfileCholeskySolver::ProfileCholeskySolver(bool use_rcm)
    : use_rcm_(use_rcm) {}

SolverResult ProfileCholeskySolver::solve(const CSRMatrix& A,
                                         const std::vector<double>& b,
                                         std::vector<double>& x,
                                         const std::vector<double>* x0) {
    (void)x0; // Direct solver does not utilize initial iterate
    SolverResult result;
    result.solver_name = get_name();
    result.iterations = 0; // Direct solver reports 0 iterations
    x.assign(b.size(), 0.0);

    // 1. Dimensionality validation
    if (A.num_rows != A.num_cols || A.num_rows == 0) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Matrix must be square and non-empty (" + 
                               std::to_string(A.num_rows) + " x " + std::to_string(A.num_cols) + ")";
        return result;
    }
    if (b.size() != A.num_rows) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "RHS dimension mismatch: b size (" + std::to_string(b.size()) + 
                               ") != matrix dim (" + std::to_string(A.num_rows) + ")";
        return result;
    }

    // 2. Reject non-finite RHS values immediately
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (!std::isfinite(b[i])) {
            result.status = SolverStatus::FAILED_INPUT_VALIDATION;
            result.error_message = "RHS contains non-finite value at index " + std::to_string(i);
            return result;
        }
    }

    double b_norm = vector_norm2(b);
    result.initial_residual = b_norm;

    // 3. Profile Cholesky Factorization with dedicated setup timer
    ProfileCholesky chol;
    auto t_setup_start = std::chrono::high_resolution_clock::now();
    ProfileCholesky::Status f_stat = chol.factor(A, use_rcm_);
    auto t_setup_end = std::chrono::high_resolution_clock::now();
    result.setup_time_ms = std::chrono::duration<double, std::milli>(t_setup_end - t_setup_start).count();

    if (f_stat != ProfileCholesky::Status::SUCCESS) {
        result.status = SolverStatus::FAILED_FACTORIZATION;
        result.error_message = chol.get_metrics().error_message;
        return result;
    }

    // 4. Substitution solve with dedicated solve timer
    auto t_solve_start = std::chrono::high_resolution_clock::now();
    bool ok = chol.solve(b, x);
    auto t_solve_end = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration<double, std::milli>(t_solve_end - t_solve_start).count();

    if (!ok) {
        result.status = SolverStatus::NUMERICAL_BREAKDOWN;
        result.error_message = "Profile Cholesky forward/backward substitution solve failed";
        x.assign(b.size(), 0.0);
        return result;
    }

    // 5. Compute true final residual and relative residual
    std::vector<double> Ax(b.size(), 0.0);
    A.matvec(x.data(), Ax.data());
    std::vector<double> r(b.size());
    for (std::size_t i = 0; i < b.size(); ++i) {
        r[i] = b[i] - Ax[i];
    }
    result.final_residual = vector_norm2(r);
    result.relative_residual = (b_norm > 0.0) ? (result.final_residual / b_norm) : 0.0;
    result.status = SolverStatus::SUCCESS;
    return result;
}

// =========================================================================
// SORSolver Implementation
// =========================================================================

SORSolver::SORSolver(SORConfig config)
    : config_(config) {}

SORSolver::SORSolver(double omega, double tolerance, int max_iterations) {
    config_.omega = omega;
    config_.tolerance = tolerance;
    config_.max_iterations = max_iterations;
}

SolverResult SORSolver::solve(const CSRMatrix& A,
                              const std::vector<double>& b,
                              std::vector<double>& x,
                              const std::vector<double>* x0) {
    SolverResult result;
    result.solver_name = get_name();
    x.assign(b.size(), 0.0);

    // 1. Parameter validation
    if (!std::isfinite(config_.omega) || config_.omega <= 0.0) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Invalid relaxation factor omega: must be finite and positive (got " + 
                               std::to_string(config_.omega) + ")";
        return result;
    }
    if (!std::isfinite(config_.tolerance) || config_.tolerance <= 0.0) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Invalid tolerance: must be finite and positive (got " + 
                               std::to_string(config_.tolerance) + ")";
        return result;
    }
    if (config_.max_iterations <= 0) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Invalid max_iterations: must be > 0 (got " + 
                               std::to_string(config_.max_iterations) + ")";
        return result;
    }
    if (!std::isfinite(config_.divergence_threshold) || config_.divergence_threshold <= 0.0) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Invalid divergence_threshold: must be finite and positive (got " + 
                               std::to_string(config_.divergence_threshold) + ")";
        return result;
    }

    // 2. Matrix dimensionality and basic checks
    if (A.num_rows != A.num_cols || A.num_rows == 0) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Matrix must be square and non-empty (" + 
                               std::to_string(A.num_rows) + " x " + std::to_string(A.num_cols) + ")";
        return result;
    }
    if (b.size() != A.num_rows) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "RHS dimension mismatch: b size (" + std::to_string(b.size()) + 
                               ") != matrix dim (" + std::to_string(A.num_rows) + ")";
        return result;
    }

    // 3. Reject non-finite RHS values immediately
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (!std::isfinite(b[i])) {
            result.status = SolverStatus::FAILED_INPUT_VALIDATION;
            result.error_message = "RHS contains non-finite value at index " + std::to_string(i);
            return result;
        }
    }

    // 4. Initial guess validation if provided
    if (x0 != nullptr) {
        if (x0->size() != b.size()) {
            result.status = SolverStatus::FAILED_INPUT_VALIDATION;
            result.error_message = "Initial guess size mismatch: x0 (" + std::to_string(x0->size()) + 
                                   ") != b (" + std::to_string(b.size()) + ")";
            return result;
        }
        for (std::size_t i = 0; i < x0->size(); ++i) {
            if (!std::isfinite((*x0)[i])) {
                result.status = SolverStatus::FAILED_INPUT_VALIDATION;
                result.error_message = "Initial guess contains non-finite value at index " + std::to_string(i);
                return result;
            }
        }
    }

    // 5. Structural CSR validation
    std::string val_err;
    if (!A.is_valid(&val_err)) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "CSR structural validation failed: " + val_err;
        return result;
    }

    // 6. Extract diagonal entries and verify positivity & finite values
    const std::size_t n = A.num_rows;
    std::vector<double> diag(n, 0.0);
    std::vector<bool> diag_found(n, false);

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t start = A.row_ptr[i];
        const std::size_t end = A.row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            double val = A.values[p];
            if (!std::isfinite(val)) {
                result.status = SolverStatus::FAILED_INPUT_VALIDATION;
                result.error_message = "Matrix contains non-finite value at row " + std::to_string(i);
                return result;
            }
            if (A.col_idx[p] == static_cast<int>(i)) {
                diag[i] = val;
                diag_found[i] = true;
            }
        }
        if (!diag_found[i] || diag[i] <= 0.0) {
            result.status = SolverStatus::FAILED_PRECONDITIONER;
            result.error_message = "SOR requires strictly positive diagonal entries; non-positive/missing at row " + 
                                   std::to_string(i);
            return result;
        }
    }

    // 7. Initial guess setup and initial residual calculation
    if (x0 != nullptr) {
        x = *x0;
    } else {
        x.assign(n, 0.0);
    }

    double b_norm = vector_norm2(b);
    if (!std::isfinite(b_norm)) {
        result.status = SolverStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "RHS Euclidean norm is non-finite";
        return result;
    }

    std::vector<double> r0(n);
    if (x0 != nullptr) {
        std::vector<double> Ax0(n, 0.0);
        A.matvec(x.data(), Ax0.data());
        for (std::size_t i = 0; i < n; ++i) {
            r0[i] = b[i] - Ax0[i];
        }
    } else {
        r0 = b;
    }
    result.initial_residual = vector_norm2(r0);
    if (!std::isfinite(result.initial_residual)) {
        result.status = SolverStatus::DIVERGED_NUMERICAL;
        result.error_message = "Initial residual norm is non-finite";
        return result;
    }

    const double norm_ref = (b_norm > 0.0) ? b_norm : result.initial_residual;

    // Check if initial guess already satisfies tolerance
    if (result.initial_residual <= 10.0 * EPS_MACH * std::max(b_norm, 1.0) || norm_ref == 0.0) {
        result.final_residual = result.initial_residual;
        result.relative_residual = 0.0;
        result.iterations = 0;
        result.status = SolverStatus::CONVERGED;
        return result;
    }

    // 8. Iteration loop
    const double omega = config_.omega;
    auto t_start = std::chrono::high_resolution_clock::now();

    for (int k = 1; k <= config_.max_iterations; ++k) {
        result.iterations = k;

        // In-place SOR update sweep across rows
        for (std::size_t i = 0; i < n; ++i) {
            double sum = b[i];
            const std::size_t start = A.row_ptr[i];
            const std::size_t end = A.row_ptr[i + 1];
            for (std::size_t p = start; p < end; ++p) {
                int j = A.col_idx[p];
                if (j != static_cast<int>(i)) {
                    sum -= A.values[p] * x[j];
                }
            }
            double x_gs = sum / diag[i];
            double x_new = x[i] + omega * (x_gs - x[i]);
            if (!std::isfinite(x_new)) {
                result.status = SolverStatus::DIVERGED_NUMERICAL;
                result.error_message = "DIVERGED_NUMERICAL: Non-finite iterate encountered at iteration " + 
                                       std::to_string(k) + ", row " + std::to_string(i);
                auto t_end = std::chrono::high_resolution_clock::now();
                result.solve_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
                return result;
            }
            x[i] = x_new;
        }

        // True residual computation: r_k = b - A * x_k
        std::vector<double> Ax(n, 0.0);
        A.matvec(x.data(), Ax.data());
        double r_sum_sq = 0.0;
        bool has_nan_res = false;
        for (std::size_t i = 0; i < n; ++i) {
            double ri = b[i] - Ax[i];
            if (!std::isfinite(ri)) {
                has_nan_res = true;
                break;
            }
            r_sum_sq += ri * ri;
        }

        if (has_nan_res) {
            result.status = SolverStatus::DIVERGED_NUMERICAL;
            result.error_message = "DIVERGED_NUMERICAL: Non-finite residual component at iteration " + std::to_string(k);
            auto t_end = std::chrono::high_resolution_clock::now();
            result.solve_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            return result;
        }

        double r_k_norm = std::sqrt(r_sum_sq);
        double rel_res = r_k_norm / norm_ref;
        result.final_residual = r_k_norm;
        result.relative_residual = rel_res;

        if (rel_res <= config_.tolerance) {
            result.status = SolverStatus::CONVERGED;
            auto t_end = std::chrono::high_resolution_clock::now();
            result.solve_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            return result;
        }

        if (rel_res > config_.divergence_threshold) {
            result.status = SolverStatus::DIVERGED_RESIDUAL;
            result.error_message = "DIVERGED_RESIDUAL: Relative residual (" + std::to_string(rel_res) + 
                                   ") exceeded threshold (" + std::to_string(config_.divergence_threshold) + 
                                   ") at iteration " + std::to_string(k);
            auto t_end = std::chrono::high_resolution_clock::now();
            result.solve_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            return result;
        }
    }

    // Reached iteration limit
    result.status = SolverStatus::MAX_ITERATIONS_REACHED;
    result.error_message = "MAX_ITERATIONS_REACHED: Iteration limit (" + std::to_string(config_.max_iterations) + 
                           ") reached without convergence (rel_res = " + std::to_string(result.relative_residual) + ")";
    auto t_end = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

// =========================================================================
// Spectral Radius Estimation Implementation
// =========================================================================

SpectralRadiusResult estimate_spectral_radius_jacobi(const CSRMatrix& A,
                                                     double tolerance,
                                                     int max_iterations) {
    SpectralRadiusResult result;
    auto t_start = std::chrono::high_resolution_clock::now();

    // 1. Input validation
    if (A.num_rows != A.num_cols || A.num_rows == 0) {
        result.status = SpectralRadiusStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Matrix must be square and non-empty (" + 
                               std::to_string(A.num_rows) + " x " + std::to_string(A.num_cols) + ")";
        return result;
    }
    std::string val_err;
    if (!A.is_valid(&val_err)) {
        result.status = SpectralRadiusStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "CSR structural validation failed: " + val_err;
        return result;
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        result.status = SpectralRadiusStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "Tolerance must be finite and positive (got " + std::to_string(tolerance) + ")";
        return result;
    }
    if (max_iterations <= 0) {
        result.status = SpectralRadiusStatus::FAILED_INPUT_VALIDATION;
        result.error_message = "max_iterations must be > 0 (got " + std::to_string(max_iterations) + ")";
        return result;
    }

    const std::size_t n = A.num_rows;
    std::vector<double> inv_sqrt_d(n, 0.0);
    std::vector<bool> diag_found(n, false);

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t start = A.row_ptr[i];
        const std::size_t end = A.row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            double val = A.values[p];
            if (!std::isfinite(val)) {
                result.status = SpectralRadiusStatus::FAILED_INPUT_VALIDATION;
                result.error_message = "Matrix contains non-finite value at row " + std::to_string(i);
                return result;
            }
            if (A.col_idx[p] == static_cast<int>(i)) {
                if (val <= 0.0) {
                    result.status = SpectralRadiusStatus::FAILED_INPUT_VALIDATION;
                    result.error_message = "Diagonal entry at row " + std::to_string(i) + " must be strictly positive";
                    return result;
                }
                inv_sqrt_d[i] = 1.0 / std::sqrt(val);
                diag_found[i] = true;
            }
        }
        if (!diag_found[i]) {
            result.status = SpectralRadiusStatus::FAILED_INPUT_VALIDATION;
            result.error_message = "Missing diagonal entry at row " + std::to_string(i);
            return result;
        }
    }

    // 2. Symmetric matrix-free operator C = I - D^(-1/2) A D^(-1/2)
    // C * v = v - D^(-1/2) * (A * (D^(-1/2) * v))
    auto apply_C = [&](const std::vector<double>& vin, std::vector<double>& vout) {
        std::vector<double> u(n);
        for (std::size_t i = 0; i < n; ++i) {
            u[i] = vin[i] * inv_sqrt_d[i];
        }
        std::vector<double> z(n, 0.0);
        A.matvec(u.data(), z.data());
        for (std::size_t i = 0; i < n; ++i) {
            vout[i] = vin[i] - inv_sqrt_d[i] * z[i];
        }
    };

    // C^2 * v = C * (C * v)
    auto apply_C2 = [&](const std::vector<double>& vin, std::vector<double>& vout) {
        std::vector<double> w1(n);
        apply_C(vin, w1);
        apply_C(w1, vout);
    };

    // 3. Deterministic initial unit vector v_0 = [1/sqrt(n), ..., 1/sqrt(n)]^T
    std::vector<double> v(n, 1.0 / std::sqrt(static_cast<double>(n)));

    // 4. Power iteration on C^2
    for (int k = 1; k <= max_iterations; ++k) {
        result.iterations = k;
        std::vector<double> w2(n);
        apply_C2(v, w2);

        // Rayleigh quotient on symmetric C^2: mu = v^T * (C^2 v)
        double mu = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            mu += v[i] * w2[i];
        }
        if (mu < 0.0) mu = 0.0; // Positive semi-definite safeguard

        // Eigenpair residual: r = w2 - mu * v
        double r_sum_sq = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double r = w2[i] - mu * v[i];
            r_sum_sq += r * r;
        }
        double r_norm = std::sqrt(r_sum_sq);
        double rel_res = (mu > 0.0) ? (r_norm / mu) : r_norm;

        result.dominant_eigenvalue_c2 = mu;
        result.spectral_radius = std::sqrt(mu);
        result.relative_residual = rel_res;

        if (rel_res <= tolerance) {
            result.status = SpectralRadiusStatus::CONVERGED;
            result.eigenvector = v;
            auto t_end = std::chrono::high_resolution_clock::now();
            result.execution_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            return result;
        }

        // Normalize iterate
        double w2_norm = vector_norm2(w2);
        if (w2_norm < 1e-15) {
            result.status = SpectralRadiusStatus::CONVERGED;
            result.spectral_radius = 0.0;
            result.dominant_eigenvalue_c2 = 0.0;
            result.relative_residual = 0.0;
            result.eigenvector = v;
            auto t_end = std::chrono::high_resolution_clock::now();
            result.execution_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            return result;
        }
        for (std::size_t i = 0; i < n; ++i) {
            v[i] = w2[i] / w2_norm;
        }
    }

    result.status = SpectralRadiusStatus::NOT_CONVERGED;
    result.error_message = "Power iteration reached max iterations (" + std::to_string(max_iterations) + 
                           ") without meeting eigenpair tolerance (" + std::to_string(tolerance) + 
                           "); final relative residual = " + std::to_string(result.relative_residual);
    result.eigenvector = v;
    auto t_end = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

double compute_optimal_omega_from_spectral_radius(double rho) {
    if (!std::isfinite(rho) || rho < 0.0 || rho >= 1.0) {
        throw std::invalid_argument("Spectral radius rho must be in [0, 1), got " + std::to_string(rho));
    }
    return 2.0 / (1.0 + std::sqrt(1.0 - rho * rho));
}

double compute_idealized_cube_spectral_radius(int N) {
    if (N < 2) {
        throw std::invalid_argument("Grid resolution N must be at least 2, got " + std::to_string(N));
    }
    constexpr double kPi = 3.141592653589793238462643383279502884;
    return (2.0 + std::cos(kPi / static_cast<double>(N))) / 3.0;
}

// =========================================================================
// Empirical SOR Omega Sweep Implementation
// =========================================================================

OmegaSweepResult run_sor_omega_sweep(const CSRMatrix& A,
                                     const std::vector<double>& b,
                                     double tolerance,
                                     int max_iterations,
                                     int repeats,
                                     int grid_N,
                                     const std::vector<double>* x0) {
    OmegaSweepResult res;
    if (repeats <= 0) repeats = 5;

    // 1. Spectral radius reference computation
    auto spec_res = estimate_spectral_radius_jacobi(A, 1e-5, 1000);
    res.estimated_spectral_radius = spec_res.spectral_radius;

    if (grid_N >= 2) {
        res.idealized_spectral_radius = compute_idealized_cube_spectral_radius(grid_N);
        res.omega_ideal = compute_optimal_omega_from_spectral_radius(res.idealized_spectral_radius);
    }

    // 2. Iterate over empirical sweep: omega in [1.10, 1.95] with step 0.05 (18 points)
    for (int step = 0; step <= 17; ++step) {
        double w = 1.10 + step * 0.05;
        OmegaSweepPoint pt;
        pt.omega = w;
        pt.runtimes_ms.reserve(repeats);

        for (int rep = 0; rep < repeats; ++rep) {
            SORSolver solver(w, tolerance, max_iterations);
            std::vector<double> x_sol;
            SolverResult s_res = solver.solve(A, b, x_sol, x0);
            pt.runtimes_ms.push_back(s_res.solve_time_ms);
            pt.iterations = s_res.iterations;
            pt.status = s_res.status;
            pt.relative_residual = s_res.relative_residual;
        }

        // Calculate median runtime
        std::vector<double> sorted_times = pt.runtimes_ms;
        std::sort(sorted_times.begin(), sorted_times.end());
        pt.median_runtime_ms = sorted_times[sorted_times.size() / 2];

        res.points.push_back(pt);
    }

    // 3. Extract omega_iter and omega_time among converged points
    int min_iters = std::numeric_limits<int>::max();
    double min_time = std::numeric_limits<double>::infinity();

    for (const auto& pt : res.points) {
        if (pt.status == SolverStatus::CONVERGED || pt.status == SolverStatus::SUCCESS) {
            if (pt.iterations < min_iters) {
                min_iters = pt.iterations;
                res.omega_iter = pt.omega;
                res.min_iterations = min_iters;
            }
            if (pt.median_runtime_ms < min_time) {
                min_time = pt.median_runtime_ms;
                res.omega_time = pt.omega;
                res.min_median_runtime_ms = min_time;
            }
        }
    }

    return res;
}

} // namespace workbench

