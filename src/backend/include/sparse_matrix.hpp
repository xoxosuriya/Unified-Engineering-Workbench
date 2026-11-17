#pragma once

#include <vector>
#include <string>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace workbench {

// IEEE-754 double precision machine epsilon: 2.2204460492503131e-16
inline constexpr double EPS_MACH = 2.2204460492503131e-16;

/**
 * @brief Compressed Sparse Row (CSR) matrix representation.
 * 
 * Matrix dimensions use std::size_t. Column indices use int (int32_t)
 * to balance indexing range with cache compactness. Floating-point values
 * are stored strictly as IEEE-754 double precision.
 */
struct CSRMatrix {
    std::size_t num_rows = 0;
    std::size_t num_cols = 0;
    std::vector<std::size_t> row_ptr; // Size: num_rows + 1, monotonically non-decreasing
    std::vector<int> col_idx;         // Size: nnz(), column indices in [0, num_cols - 1]
    std::vector<double> values;       // Size: nnz()

    [[nodiscard]] std::size_t nnz() const noexcept {
        return values.size();
    }

    /**
     * @brief Matrix-vector product y = A * x.
     * Evaluates in exactly 2 * NNZ floating-point operations.
     */
    void matvec(const double* x, double* y) const;
    void matvec(const std::vector<double>& x, std::vector<double>& y) const;

    /**
     * @brief Extracts diagonal vector D where D[i] = A_ii.
     * Throws std::runtime_error if matrix is not square or if a diagonal entry is missing.
     */
    [[nodiscard]] std::vector<double> get_diagonal() const;

    /**
     * @brief Direct query of entry A(i, j). Returns 0.0 if not stored.
     */
    [[nodiscard]] double get_entry(std::size_t row, std::size_t col) const;

    /**
     * @brief Validates CSR structural integrity:
     * - row_ptr size is num_rows + 1
     * - row_ptr[0] == 0 and row_ptr[num_rows] == nnz()
     * - row_ptr is monotonically non-decreasing
     * - col_idx.size() == values.size()
     * - column indices within [0, num_cols - 1]
     * - column indices strictly sorted in ascending order per row with zero duplicates
     */
    [[nodiscard]] bool is_valid(std::string* error_msg = nullptr) const;

    /**
     * @brief Checks symmetry A_ij == A_ji within scale-relative tolerance:
     * |A_ij - A_ji| <= rel_tol * max(|A_ij|, |A_ji|).
     * Every stored entry is checked for finiteness; any non-finite entry (NaN/Inf)
     * is rejected immediately. Every stored off-diagonal entry is checked against its
     * counterpart; any missing counterpart is treated as 0.0 and rejected if nonzero.
     */
    [[nodiscard]] bool is_symmetric(double rel_tol = 10.0 * EPS_MACH, std::string* error_msg = nullptr) const;

    /**
     * @brief Checks strictly positive diagonal: A_ii > 0 for all i.
     */
    [[nodiscard]] bool has_strictly_positive_diagonal(std::string* error_msg = nullptr) const;

    /**
     * @brief Validates weak diagonal dominance using absolute values of off-diagonal entries:
     * A_ii >= sum_{j != i} |A_ij| - rel_tol * A_ii, with A_ii > 0.
     */
    [[nodiscard]] bool is_weakly_diagonally_dominant(double rel_tol = 10.0 * EPS_MACH, std::string* error_msg = nullptr) const;

    /**
     * @brief Computes matrix infinity norm: max_i ( sum_j |A_ij| ).
     */
    [[nodiscard]] double norm_inf() const;
};

/**
 * @brief Builder for assembling CSR matrices from unordered (row, col, val) coordinate triplets.
 */
class SparseMatrixBuilder {
public:
    struct Triplet {
        std::size_t row = 0;
        std::size_t col = 0;
        double val = 0.0;
    };

    SparseMatrixBuilder(std::size_t rows, std::size_t cols);

    void add_entry(std::size_t row, std::size_t col, double val);
    void reserve(std::size_t expected_nnz);

    [[nodiscard]] CSRMatrix build(bool sum_duplicates = true) const;
    [[nodiscard]] std::size_t num_rows() const noexcept { return num_rows_; }
    [[nodiscard]] std::size_t num_cols() const noexcept { return num_cols_; }

private:
    std::size_t num_rows_ = 0;
    std::size_t num_cols_ = 0;
    mutable std::vector<Triplet> triplets_;
};

/**
 * @brief Reverse Cuthill-McKee (RCM) ordering engine for symmetric sparse matrices.
 */
class RCMOrdering {
public:
    struct PermutationResult {
        std::vector<std::size_t> perm;     // Permutation: perm[new_idx] = old_idx
        std::vector<std::size_t> inv_perm; // Inverse: inv_perm[old_idx] = new_idx
        std::size_t original_bandwidth = 0;
        std::size_t permuted_bandwidth = 0;
        std::size_t original_profile = 0;
        std::size_t permuted_profile = 0;
        bool is_valid = false;
    };

    /**
     * @brief Computes RCM permutation using pseudo-peripheral root selection and BFS level sets.
     */
    static PermutationResult compute(const CSRMatrix& A);

    /**
     * @brief Validates that a permutation vector of length M is a bijection on {0, ..., M-1}.
     */
    static bool validate_permutation(const std::vector<std::size_t>& perm);

    /**
     * @brief Inverts a permutation vector: inv_perm[perm[i]] = i.
     */
    static std::vector<std::size_t> invert_permutation(const std::vector<std::size_t>& perm);

    /**
     * @brief Symmetrically permutes CSRMatrix A according to P: A_perm = P * A * P^T.
     * Throws std::invalid_argument if dimensions mismatch, if perm/inv_perm are not bijections,
     * or if inv_perm is not the exact inverse of perm (inv_perm[perm[i]] != i).
     */
    static CSRMatrix permute_matrix(const CSRMatrix& A, 
                                   const std::vector<std::size_t>& perm, 
                                   const std::vector<std::size_t>& inv_perm);

    /**
     * @brief Permutes vector b by permutation P: b_perm[i] = b[perm[i]].
     * Throws std::invalid_argument if b.size() != perm.size() or if perm is not a bijection.
     */
    static std::vector<double> permute_vector(const std::vector<double>& b, 
                                              const std::vector<std::size_t>& perm);

    /**
     * @brief Unpermutes vector x_perm back to original order: x[perm[i]] = x_perm[i].
     * Throws std::invalid_argument if x_perm.size() != inv_perm.size() or if inv_perm is not a bijection.
     */
    static std::vector<double> unpermute_vector(const std::vector<double>& x_perm, 
                                                const std::vector<std::size_t>& inv_perm);
};

/**
 * @brief Profile/Skyline Cholesky factorization and metrics container.
 */
class ProfileCholesky {
public:
    enum class Status {
        SUCCESS,
        FAILED_DIMENSION_MISMATCH,
        FAILED_NON_SPD,
        FAILED_ZERO_PIVOT,
        FAILED_NUMERICAL_INSTABILITY
    };

    struct Metrics {
        std::size_t matrix_dim = 0;
        std::size_t matrix_nnz = 0;
        std::size_t envelope_capacity = 0;         // N_env = sum(i - col_start[i] + 1)
        std::size_t numerical_factor_nnz = 0;      // NNZ_L satisfying |L_ij| > eps_drop * sqrt(L_ii * L_jj)
        double fill_in_ratio = 0.0;                // NNZ_L / (0.5 * (NNZ_A - M) + M)
        std::size_t allocated_factor_bytes = 0;    // Factor memory footprint in bytes
        std::size_t factorization_flops = 0;       // Total floating-point operations in factorization
        double min_pivot = 0.0;
        double max_pivot = 0.0;
        double reconstruction_error = 0.0;         // ||A - L L^T||_inf / ||A||_inf
        double backward_error = 0.0;               // ||b - A x||_2 / (||A||_inf * ||x||_2 + ||b||_2)
        Status status = Status::SUCCESS;
        std::string error_message;
    };

    ProfileCholesky() = default;

    /**
     * @brief Factors symmetric positive definite matrix A into L * L^T in profile storage.
     * If use_rcm is true, automatically permutes A via RCM before factorization.
     */
    Status factor(const CSRMatrix& A, bool use_rcm = true);

    /**
     * @brief Solves A * x = b using forward and backward substitution through L * L^T.
     * Automatically handles forward and inverse RCM permutations if RCM was enabled.
     * Rejects non-finite RHS values and asserts that all output values are finite.
     */
    bool solve(const std::vector<double>& b, std::vector<double>& x) const;
    bool solve(const double* b, double* x) const;

    [[nodiscard]] const Metrics& get_metrics() const noexcept { return metrics_; }
    [[nodiscard]] const std::vector<std::size_t>& get_col_start() const noexcept { return col_start_; }
    [[nodiscard]] const std::vector<std::size_t>& get_profile_row_ptr() const noexcept { return profile_row_ptr_; }
    [[nodiscard]] const std::vector<double>& get_profile_values() const noexcept { return profile_values_; }
    [[nodiscard]] const RCMOrdering::PermutationResult& get_rcm_result() const noexcept { return rcm_result_; }
    [[nodiscard]] bool is_factored() const noexcept { return metrics_.status == Status::SUCCESS; }

    /**
     * @brief Computes factor reconstruction error ||A_target - L L^T||_inf / ||A_target||_inf
     * using the full symmetric residual matrix across all rows.
     * 
     * IMPORTANT: A_target must be in the identical ordering as the factor. If RCM ordering was
     * enabled during factorization (use_rcm == true), A_target must be the permuted matrix
     * A_perm = P * A * P^T (as obtained via RCMOrdering::permute_matrix).
     * 
     * Validates that A_target is structurally valid, square, matches factor dimension, is symmetric,
     * and has a finite, non-zero infinity norm. Returns -1.0 on invalid input, dimension mismatch,
     * nonsymmetric matrix, non-finite values, or zero norm.
     */
    [[nodiscard]] double compute_reconstruction_error(const CSRMatrix& A_target) const;

    /**
     * @brief Computes backward error ||b - A x||_2 / (||A||_inf * ||x||_2 + ||b||_2).
     */
    [[nodiscard]] double compute_backward_error(const CSRMatrix& A, 
                                                const std::vector<double>& b, 
                                                const std::vector<double>& x) const;

private:
    std::size_t dim_ = 0;
    std::vector<std::size_t> col_start_;         // col_start[i] = min column index stored in row i
    std::vector<std::size_t> profile_row_ptr_;   // Skyline row pointers into profile_values_
    std::vector<double> profile_values_;         // Allocated envelope storage for lower factor L
    bool use_rcm_ = false;
    RCMOrdering::PermutationResult rcm_result_;
    Metrics metrics_;
};

// =========================================================================
// Mathematical Vector Helper Functions
// =========================================================================

[[nodiscard]] double vector_dot(const double* a, const double* b, std::size_t n);
[[nodiscard]] double vector_dot(const std::vector<double>& a, const std::vector<double>& b);

[[nodiscard]] double vector_norm2(const double* a, std::size_t n);
[[nodiscard]] double vector_norm2(const std::vector<double>& a);

[[nodiscard]] double vector_norm_inf(const double* a, std::size_t n);
[[nodiscard]] double vector_norm_inf(const std::vector<double>& a);

} // namespace workbench

