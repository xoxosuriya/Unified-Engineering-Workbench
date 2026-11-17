#pragma once

#include "sparse_matrix.hpp"
#include <Eigen/SparseCore>
#include <Eigen/IterativeLinearSolvers>
#include <string>
#include <vector>
#include <chrono>

namespace workbench {

/**
 * @brief Common status enum for all 3D thermal linear solvers (Profile Cholesky, PCG, SOR).
 */
enum class SolverStatus {
    SUCCESS,                    ///< Solve completed and converged within target tolerance
    CONVERGED,                  ///< Iterative solver reached target tolerance (rel_res <= tol)
    MAX_ITERATIONS_REACHED,     ///< Iterative solver reached iteration limit without meeting tolerance
    DIVERGED_RESIDUAL,          ///< Residual norm exceeded divergence threshold (> 1e4 * ||b||)
    DIVERGED_NUMERICAL,         ///< Blueprint termination category: NaN or Inf encountered
    NUMERICAL_BREAKDOWN,        ///< Zero pivot, loss of positive definiteness, or numerical failure
    FAILED_INPUT_VALIDATION,    ///< Matrix structural failure, dimension mismatch, or non-finite inputs
    FAILED_PRECONDITIONER,      ///< Preconditioner construction failed (e.g. zero/negative diagonal)
    FAILED_FACTORIZATION        ///< Direct Cholesky factorization failure
};

/**
 * @brief Unified result and telemetry record returned by all solvers.
 */
struct SolverResult {
    SolverStatus status = SolverStatus::SUCCESS;
    std::string solver_name;            ///< Identifier, e.g. "Eigen_PCG_Jacobi", "Profile_Cholesky"
    int iterations = 0;                 ///< Iterations performed (0 for direct solvers)
    double initial_residual = 0.0;      ///< ||b - A x_0||_2
    double final_residual = 0.0;        ///< ||b - A x_k||_2
    double relative_residual = 0.0;     ///< ||b - A x_k||_2 / ||b||_2 (or relative to r_0)
    double solve_time_ms = 0.0;         ///< Solve execution time in milliseconds (excluding conversion)
    double setup_time_ms = 0.0;         ///< Preconditioner or factorization time in milliseconds
    double conversion_time_ms = 0.0;    ///< Matrix conversion time in ms (CSR -> Eigen, if applicable)
    std::string error_message;
};

/**
 * @brief Converts workbench::CSRMatrix into Eigen::SparseMatrix<double, Eigen::RowMajor, int>.
 * 
 * Strict invariants verified prior to and during conversion:
 * - Structural CSR validity (row_ptr monotonicity, bounds, no duplicate columns).
 * - Dimensions within [1, INT_MAX] (required for Eigen::RowMajor 32-bit int index storage).
 * - NNZ within [0, INT_MAX].
 * - Every value is finite (neither NaN nor Inf).
 * 
 * Conversion time is measured with high-resolution clock and stored in conversion_time_ms if non-null.
 * 
 * @param A Source CSR sparse matrix.
 * @param eigen_mat Output Eigen row-major sparse matrix.
 * @param conversion_time_ms Optional pointer to store measured conversion elapsed time in ms.
 * @param error_msg Optional pointer to receive descriptive error message on validation failure.
 * @return true if conversion succeeded; false if validation failed.
 */
bool convert_csr_to_eigen(const CSRMatrix& A,
                          Eigen::SparseMatrix<double, Eigen::RowMajor, int>& eigen_mat,
                          double* conversion_time_ms = nullptr,
                          std::string* error_msg = nullptr);

/**
 * @brief Common abstract base class for 3D thermal linear solvers.
 */
class IThermalSolver3D {
public:
    virtual ~IThermalSolver3D() = default;

    [[nodiscard]] virtual std::string get_name() const = 0;

    /**
     * @brief Solves A * x = b.
     * @param A Sparse coefficient matrix in CSR format.
     * @param b Right-hand side vector.
     * @param x Output solution vector.
     * @param x0 Optional initial guess for iterative solvers. If nullptr, x0 = 0 is assumed.
     * @return Common SolverResult containing convergence, residual, and timing metrics.
     */
    virtual SolverResult solve(const CSRMatrix& A,
                               const std::vector<double>& b,
                               std::vector<double>& x,
                               const std::vector<double>* x0 = nullptr) = 0;
};

/**
 * @brief Configuration parameters for Eigen-backed Jacobi-preconditioned Conjugate Gradient.
 */
struct PCGConfig {
    double tolerance = 1e-6;            ///< Target relative residual tolerance: ||r_k||_2 / ||b||_2
    int max_iterations = 5000;          ///< Maximum allowed iterations
    bool check_spd = true;              ///< Verify strictly positive diagonal before solving
};

/**
 * @brief Eigen-backed Preconditioned Conjugate Gradient (PCG) solver with Jacobi (diagonal) preconditioner.
 */
class EigenPCGSolver : public IThermalSolver3D {
public:
    explicit EigenPCGSolver(PCGConfig config = PCGConfig());

    [[nodiscard]] std::string get_name() const override { return "Eigen_PCG_Jacobi"; }

    [[nodiscard]] const PCGConfig& get_config() const noexcept { return config_; }
    void set_config(const PCGConfig& config) noexcept { config_ = config; }

    SolverResult solve(const CSRMatrix& A,
                       const std::vector<double>& b,
                       std::vector<double>& x,
                       const std::vector<double>* x0 = nullptr) override;

private:
    PCGConfig config_;
};

/**
 * @brief Adapter for approved Phase 1 ProfileCholesky implementing IThermalSolver3D.
 */
class ProfileCholeskySolver : public IThermalSolver3D {
public:
    explicit ProfileCholeskySolver(bool use_rcm = true);

    [[nodiscard]] std::string get_name() const override { 
        return use_rcm_ ? "Profile_Cholesky_RCM" : "Profile_Cholesky_Natural"; 
    }

    SolverResult solve(const CSRMatrix& A,
                       const std::vector<double>& b,
                       std::vector<double>& x,
                       const std::vector<double>* x0 = nullptr) override;

private:
    bool use_rcm_ = true;
};

/**
 * @brief Configuration parameters for Successive Over-Relaxation (SOR) solver.
 */
struct SORConfig {
    double omega = 1.0;                 ///< Relaxation factor omega (default 1.0 = Gauss-Seidel)
    double tolerance = 1e-6;            ///< Relative residual stopping tolerance ||r_k||_2 / ||b||_2 (default 1e-6)
    int max_iterations = 10000;         ///< Maximum iteration limit (default 10,000)
    double divergence_threshold = 1e4;  ///< Relative residual divergence threshold (default 1e4)
};

/**
 * @brief Custom Successive Over-Relaxation (SOR) solver for assembled CSR systems.
 * 
 * Supports:
 * - Configurable relaxation factor omega in (0, 2).
 * - Configurable relative residual tolerance (default 1e-6).
 * - Maximum iteration limit (default 10,000).
 * - Zero initial guess or validated user initial guess x0.
 * - Calculation and reporting of true relative residual ||b - A x_k||_2 / ||b||_2.
 * - Four MECE termination categories: CONVERGED, DIVERGED_RESIDUAL, DIVERGED_NUMERICAL, MAX_ITERATIONS_REACHED.
 * - Input validation: dimensions, finite values, positive diagonal, valid parameters.
 */
class SORSolver : public IThermalSolver3D {
public:
    explicit SORSolver(SORConfig config = SORConfig());
    explicit SORSolver(double omega, double tolerance = 1e-6, int max_iterations = 10000);

    [[nodiscard]] std::string get_name() const override { return "Custom_SOR"; }

    [[nodiscard]] const SORConfig& get_config() const noexcept { return config_; }
    void set_config(const SORConfig& config) noexcept { config_ = config; }
    void set_omega(double omega) noexcept { config_.omega = omega; }
    [[nodiscard]] double get_omega() const noexcept { return config_.omega; }

    /**
     * @brief Solves A * x = b via custom SOR iteration.
     * @param A Sparse coefficient matrix in CSR format (must be square, valid, positive diagonal).
     * @param b Right-hand side vector.
     * @param x Output solution vector.
     * @param x0 Optional initial guess. If nullptr, x0 = 0 is assumed.
     * @return SolverResult with status, iterations, initial/final/relative residuals, and timing.
     */
    SolverResult solve(const CSRMatrix& A,
                       const std::vector<double>& b,
                       std::vector<double>& x,
                       const std::vector<double>* x0 = nullptr) override;

private:
    SORConfig config_;
};

/**
 * @brief Status enum for spectral-radius power iteration.
 */
enum class SpectralRadiusStatus {
    CONVERGED,                  ///< Relative eigenpair residual <= tolerance
    NOT_CONVERGED,              ///< Maximum iterations reached without meeting tolerance
    FAILED_INPUT_VALIDATION     ///< Input matrix invalid, non-square, or non-positive diagonal
};

/**
 * @brief Result of power iteration spectral radius estimation.
 */
struct SpectralRadiusResult {
    SpectralRadiusStatus status = SpectralRadiusStatus::CONVERGED;
    double spectral_radius = 0.0;        ///< Estimated Jacobi spectral radius rho_hat(B) = sqrt(mu)
    double dominant_eigenvalue_c2 = 0.0; ///< mu = dominant eigenvalue of symmetric operator C^2
    double relative_residual = 0.0;      ///< ||C^2 v - mu v||_2 / mu
    int iterations = 0;                  ///< Iterations performed (<= max_iterations)
    double execution_time_ms = 0.0;      ///< Execution time in milliseconds
    std::vector<double> eigenvector;     ///< Dominant eigenvector v of C^2 (normalized to unit 2-norm)
    std::string error_message;
};

/**
 * @brief Estimates the Jacobi spectral radius rho(B) via matrix-free power iteration on C^2.
 * 
 * Uses the algebraically exact similarity relationship:
 *   C = D^(1/2) B D^(-1/2) = I - D^(-1/2) A D^(-1/2)
 * where B = I - D^(-1) A is the Jacobi iteration matrix.
 * Since C is symmetric and similar to B, rho(B) = rho(C) = sqrt(lambda_max(C^2)).
 * 
 * Power iteration is performed on the symmetric positive semi-definite operator C^2
 * with a deterministic initial vector v_0 = [1/sqrt(n), ..., 1/sqrt(n)]^T.
 * 
 * @param A Symmetric positive definite CSR sparse matrix.
 * @param tolerance Relative eigenpair residual tolerance ||C^2 v - mu v||_2 / mu (default: 1e-5).
 * @param max_iterations Maximum power iterations (default: 1000).
 * @return SpectralRadiusResult with estimated spectral radius, residual, iterations, and status.
 */
SpectralRadiusResult estimate_spectral_radius_jacobi(const CSRMatrix& A,
                                                     double tolerance = 1e-5,
                                                     int max_iterations = 1000);

/**
 * @brief Computes theoretical optimal SOR relaxation parameter from a given spectral radius.
 *   omega_opt = 2 / (1 + sqrt(1 - rho^2))
 * @param rho Spectral radius rho in [0, 1).
 * @return Optimal omega in [1, 2).
 */
[[nodiscard]] double compute_optimal_omega_from_spectral_radius(double rho);

/**
 * @brief Computes theoretical idealized Jacobi spectral radius for structured Cartesian 3D cube grid.
 *   rho_ideal = (2 + cos(pi / N)) / 3
 * Note: This is an idealized analytical reference, NOT the exact discrete spectral radius.
 * @param N Grid resolution per axis.
 * @return Idealized Jacobi spectral radius.
 */
[[nodiscard]] double compute_idealized_cube_spectral_radius(int N);

/**
 * @brief Telemetry for a single relaxation parameter in the empirical omega sweep.
 */
struct OmegaSweepPoint {
    double omega = 0.0;
    int iterations = 0;                     ///< Iteration count to reach tolerance
    std::vector<double> runtimes_ms;        ///< Runtimes recorded across repeat runs
    double median_runtime_ms = 0.0;         ///< Median runtime in milliseconds
    SolverStatus status = SolverStatus::CONVERGED;
    double relative_residual = 0.0;         ///< True relative residual at termination
};

/**
 * @brief Summary result of the empirical SOR omega sweep.
 */
struct OmegaSweepResult {
    std::vector<OmegaSweepPoint> points;
    double omega_iter = 0.0;                ///< Omega yielding lowest iteration count among converged
    int min_iterations = 0;
    double omega_time = 0.0;                ///< Omega yielding lowest median runtime among converged
    double min_median_runtime_ms = 0.0;
    double omega_ideal = 0.0;               ///< Theoretical idealized reference Young formula
    double estimated_spectral_radius = 0.0; ///< Discrete spectral radius estimated by power iteration
    double idealized_spectral_radius = 0.0; ///< Continuous idealized formula (2 + cos(pi/N)) / 3
};

/**
 * @brief Executes an empirical SOR parameter sweep over omega in [1.10, 1.95] in steps of 0.05.
 * 
 * For each omega:
 * - Runs the custom SOR solver 5 times.
 * - Records iteration counts, runtimes, median runtime, and termination status.
 * - Identifies omega_iter (minimum iterations) and omega_time (minimum median runtime).
 * - Computes omega_ideal for reference without conflating with empirical findings.
 * 
 * @param A Coefficient matrix in CSR format.
 * @param b Right-hand side vector.
 * @param tolerance Relative residual tolerance (default: 1e-6).
 * @param max_iterations Maximum iteration limit (default: 10,000).
 * @param repeats Number of timing repetitions per omega (default: 5).
 * @param grid_N Grid resolution N for idealized formula reference (default: 10).
 * @param x0 Optional initial guess (nullptr = zero initial guess).
 * @return OmegaSweepResult containing all sweep points and optimal metrics.
 */
OmegaSweepResult run_sor_omega_sweep(const CSRMatrix& A,
                                     const std::vector<double>& b,
                                     double tolerance = 1e-6,
                                     int max_iterations = 10000,
                                     int repeats = 5,
                                     int grid_N = 10,
                                     const std::vector<double>* x0 = nullptr);

} // namespace workbench

