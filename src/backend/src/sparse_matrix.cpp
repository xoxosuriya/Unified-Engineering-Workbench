#include "sparse_matrix.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <numeric>
#include <queue>
#include <cassert>
#include <iomanip>

namespace workbench {

// =========================================================================
// CSRMatrix Implementation
// =========================================================================

void CSRMatrix::matvec(const double* x, double* y) const {
    if (!x || !y) return;
    for (std::size_t i = 0; i < num_rows; ++i) {
        double sum = 0.0;
        const std::size_t start = row_ptr[i];
        const std::size_t end = row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            sum += values[p] * x[col_idx[p]];
        }
        y[i] = sum;
    }
}

void CSRMatrix::matvec(const std::vector<double>& x, std::vector<double>& y) const {
    if (x.size() != num_cols) {
        throw std::invalid_argument("CSRMatrix::matvec: vector x dimension mismatch (" + 
                                    std::to_string(x.size()) + " vs " + std::to_string(num_cols) + ")");
    }
    y.resize(num_rows);
    matvec(x.data(), y.data());
}

std::vector<double> CSRMatrix::get_diagonal() const {
    if (num_rows != num_cols) {
        throw std::runtime_error("CSRMatrix::get_diagonal: matrix is non-square (" + 
                                 std::to_string(num_rows) + " x " + std::to_string(num_cols) + ")");
    }
    std::vector<double> diag(num_rows, 0.0);
    for (std::size_t i = 0; i < num_rows; ++i) {
        const std::size_t start = row_ptr[i];
        const std::size_t end = row_ptr[i + 1];
        bool found = false;
        for (std::size_t p = start; p < end; ++p) {
            if (static_cast<std::size_t>(col_idx[p]) == i) {
                diag[i] = values[p];
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("CSRMatrix::get_diagonal: missing diagonal entry at row " + std::to_string(i));
        }
    }
    return diag;
}

double CSRMatrix::get_entry(std::size_t row, std::size_t col) const {
    if (row >= num_rows || col >= num_cols) {
        return 0.0;
    }
    const std::size_t start = row_ptr[row];
    const std::size_t end = row_ptr[row + 1];
    
    // Binary search within sorted column indices of row
    auto it = std::lower_bound(col_idx.begin() + start, col_idx.begin() + end, static_cast<int>(col));
    if (it != col_idx.begin() + end && *it == static_cast<int>(col)) {
        return values[std::distance(col_idx.begin(), it)];
    }
    return 0.0;
}

bool CSRMatrix::is_valid(std::string* error_msg) const {
    auto report = [error_msg](const std::string& msg) {
        if (error_msg) *error_msg = msg;
        return false;
    };

    if (row_ptr.size() != num_rows + 1) {
        return report("row_ptr size (" + std::to_string(row_ptr.size()) + 
                      ") != num_rows + 1 (" + std::to_string(num_rows + 1) + ")");
    }
    if (row_ptr[0] != 0) {
        return report("row_ptr[0] != 0");
    }
    if (col_idx.size() != values.size()) {
        return report("col_idx size (" + std::to_string(col_idx.size()) + 
                      ") != values size (" + std::to_string(values.size()) + ")");
    }
    if (row_ptr[num_rows] != values.size()) {
        return report("row_ptr[num_rows] (" + std::to_string(row_ptr[num_rows]) + 
                      ") != values size (" + std::to_string(values.size()) + ")");
    }

    for (std::size_t i = 0; i < num_rows; ++i) {
        if (row_ptr[i] > row_ptr[i + 1]) {
            return report("row_ptr is not monotonic at row " + std::to_string(i));
        }
        const std::size_t start = row_ptr[i];
        const std::size_t end = row_ptr[i + 1];
        int prev_col = -1;
        for (std::size_t p = start; p < end; ++p) {
            int col = col_idx[p];
            if (col < 0 || static_cast<std::size_t>(col) >= num_cols) {
                return report("Column index " + std::to_string(col) + " out of bounds at row " + std::to_string(i));
            }
            if (p > start && col <= prev_col) {
                return report("Column indices not strictly sorted or duplicate found at row " + std::to_string(i));
            }
            if (std::isnan(values[p]) || std::isinf(values[p])) {
                return report("NaN or Inf value detected at row " + std::to_string(i) + ", col " + std::to_string(col));
            }
            prev_col = col;
        }
    }
    return true;
}

bool CSRMatrix::is_symmetric(double rel_tol, std::string* error_msg) const {
    if (num_rows != num_cols) {
        if (error_msg) *error_msg = "Matrix is non-square (" + std::to_string(num_rows) + " x " + std::to_string(num_cols) + ")";
        return false;
    }
    for (std::size_t i = 0; i < num_rows; ++i) {
        const std::size_t start = row_ptr[i];
        const std::size_t end = row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t j = static_cast<std::size_t>(col_idx[p]);
            double a_ij = values[p];
            if (!std::isfinite(a_ij)) {
                if (error_msg) *error_msg = "Non-finite entry detected at (" + std::to_string(i) + ", " + std::to_string(j) + ")";
                return false;
            }
            if (i == j) continue;
            double a_ji = get_entry(j, i); // Returns 0.0 if counterpart is missing
            if (!std::isfinite(a_ji)) {
                if (error_msg) *error_msg = "Non-finite counterpart entry at (" + std::to_string(j) + ", " + std::to_string(i) + ")";
                return false;
            }
            double scale = std::max(std::abs(a_ij), std::abs(a_ji));
            if (scale > 0.0 && std::abs(a_ij - a_ji) > rel_tol * scale) {
                if (error_msg) {
                    std::ostringstream ss;
                    ss << "Symmetry violation at (" << i << ", " << j << "): A(" << i << "," << j << ")=" << a_ij 
                       << " != A(" << j << "," << i << ")=" << a_ji << " (diff=" << std::abs(a_ij - a_ji) 
                       << " > tol=" << (rel_tol * scale) << ")";
                    *error_msg = ss.str();
                }
                return false;
            }
        }
    }
    return true;
}

bool CSRMatrix::has_strictly_positive_diagonal(std::string* error_msg) const {
    if (num_rows != num_cols) {
        if (error_msg) *error_msg = "Matrix is non-square";
        return false;
    }
    for (std::size_t i = 0; i < num_rows; ++i) {
        double d = get_entry(i, i);
        if (d <= 0.0 || std::isnan(d)) {
            if (error_msg) {
                *error_msg = "Non-positive diagonal entry A(" + std::to_string(i) + "," + 
                             std::to_string(i) + ") = " + std::to_string(d);
            }
            return false;
        }
    }
    return true;
}

bool CSRMatrix::is_weakly_diagonally_dominant(double rel_tol, std::string* error_msg) const {
    if (num_rows != num_cols) {
        if (error_msg) *error_msg = "Matrix is non-square";
        return false;
    }
    for (std::size_t i = 0; i < num_rows; ++i) {
        double diag = 0.0;
        double off_diag_sum = 0.0;
        const std::size_t start = row_ptr[i];
        const std::size_t end = row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t j = static_cast<std::size_t>(col_idx[p]);
            if (j == i) {
                diag = values[p];
            } else {
                off_diag_sum += std::abs(values[p]);
            }
        }
        if (diag <= 0.0) {
            if (error_msg) *error_msg = "Diagonal <= 0 at row " + std::to_string(i);
            return false;
        }
        // Scale-relative weak diagonal dominance condition: A_ii >= sum_{j!=i} |A_ij| - rel_tol * A_ii
        if (diag < off_diag_sum - rel_tol * diag) {
            if (error_msg) {
                std::ostringstream ss;
                ss << "Diagonal dominance violated at row " << i << ": A_ii=" << diag 
                   << " < sum|A_ij|=" << off_diag_sum << " (diff=" << (off_diag_sum - diag) << ")";
                *error_msg = ss.str();
            }
            return false;
        }
    }
    return true;
}

double CSRMatrix::norm_inf() const {
    if (num_rows == 0) {
        return 0.0;
    }
    double max_row_sum = 0.0;
    for (std::size_t i = 0; i < num_rows; ++i) {
        double row_sum = 0.0;
        const std::size_t start = row_ptr[i];
        const std::size_t end = row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            row_sum += std::abs(values[p]);
        }
        if (row_sum > max_row_sum) {
            max_row_sum = row_sum;
        }
    }
    return max_row_sum;
}

// =========================================================================
// SparseMatrixBuilder Implementation
// =========================================================================

SparseMatrixBuilder::SparseMatrixBuilder(std::size_t rows, std::size_t cols)
    : num_rows_(rows), num_cols_(cols) {
    if (rows > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        cols > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("SparseMatrixBuilder: dimension exceeds int index range (INT_MAX)");
    }
    if (rows >= std::numeric_limits<std::size_t>::max() - 1) {
        throw std::invalid_argument("SparseMatrixBuilder: row count causes size_t overflow");
    }
}

void SparseMatrixBuilder::add_entry(std::size_t row, std::size_t col, double val) {
    if (row >= num_rows_ || col >= num_cols_) {
        throw std::out_of_range("SparseMatrixBuilder::add_entry: index out of bounds (" + 
                                std::to_string(row) + ", " + std::to_string(col) + ")");
    }
    if (!std::isfinite(val)) {
        throw std::invalid_argument("SparseMatrixBuilder::add_entry: value must be finite (got NaN or Inf)");
    }
    if (row > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        col > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::out_of_range("SparseMatrixBuilder::add_entry: index exceeds int maximum");
    }
    if (triplets_.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("SparseMatrixBuilder::add_entry: triplet count exceeds INT_MAX");
    }
    triplets_.push_back({row, col, val});
}

void SparseMatrixBuilder::reserve(std::size_t expected_nnz) {
    triplets_.reserve(expected_nnz);
}

CSRMatrix SparseMatrixBuilder::build(bool sum_duplicates) const {
    CSRMatrix mat;
    mat.num_rows = num_rows_;
    mat.num_cols = num_cols_;
    mat.row_ptr.assign(num_rows_ + 1, 0);

    if (triplets_.empty()) {
        return mat;
    }

    // Sort triplets primarily by row, secondarily by col
    std::sort(triplets_.begin(), triplets_.end(), [](const Triplet& a, const Triplet& b) {
        if (a.row != b.row) return a.row < b.row;
        return a.col < b.col;
    });

    std::vector<Triplet> unique_triplets;
    unique_triplets.reserve(triplets_.size());

    if (sum_duplicates) {
        for (const auto& t : triplets_) {
            if (!unique_triplets.empty() && 
                unique_triplets.back().row == t.row && 
                unique_triplets.back().col == t.col) {
                unique_triplets.back().val += t.val;
                if (!std::isfinite(unique_triplets.back().val)) {
                    throw std::runtime_error("SparseMatrixBuilder::build: Non-finite value accumulated during duplicate merging");
                }
            } else {
                unique_triplets.push_back(t);
            }
        }
    } else {
        unique_triplets = triplets_;
    }

    // Count nonzeros per row
    for (const auto& t : unique_triplets) {
        mat.row_ptr[t.row + 1]++;
    }

    // Prefix sum to compute row_ptr offsets
    for (std::size_t i = 0; i < num_rows_; ++i) {
        mat.row_ptr[i + 1] += mat.row_ptr[i];
    }

    mat.col_idx.resize(unique_triplets.size());
    mat.values.resize(unique_triplets.size());

    for (std::size_t idx = 0; idx < unique_triplets.size(); ++idx) {
        mat.col_idx[idx] = static_cast<int>(unique_triplets[idx].col);
        mat.values[idx] = unique_triplets[idx].val;
    }

    std::string err;
    if (!mat.is_valid(&err)) {
        throw std::runtime_error("SparseMatrixBuilder::build: Built CSR matrix failed validation: " + err);
    }

    return mat;
}

// =========================================================================
// RCMOrdering Implementation
// =========================================================================

bool RCMOrdering::validate_permutation(const std::vector<std::size_t>& perm) {
    std::size_t n = perm.size();
    std::vector<bool> seen(n, false);
    for (std::size_t v : perm) {
        if (v >= n || seen[v]) return false;
        seen[v] = true;
    }
    return true;
}

std::vector<std::size_t> RCMOrdering::invert_permutation(const std::vector<std::size_t>& perm) {
    std::size_t n = perm.size();
    std::vector<std::size_t> inv(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        inv[perm[i]] = i;
    }
    return inv;
}

RCMOrdering::PermutationResult RCMOrdering::compute(const CSRMatrix& A) {
    PermutationResult res;
    if (!A.is_valid()) {
        res.is_valid = false;
        return res;
    }
    const std::size_t n = A.num_rows;
    if (n == 0 || A.num_cols != n) {
        res.is_valid = false;
        return res;
    }
    if (!A.is_symmetric(10.0 * EPS_MACH)) {
        res.is_valid = false;
        return res;
    }

    // Compute original bandwidth and profile
    std::size_t orig_bw = 0;
    std::size_t orig_prof = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t min_col = i;
        const std::size_t start = A.row_ptr[i];
        const std::size_t end = A.row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t j = static_cast<std::size_t>(A.col_idx[p]);
            if (j <= i && j < min_col) {
                min_col = j;
            }
            std::size_t diff = (i > j) ? (i - j) : (j - i);
            if (diff > orig_bw) orig_bw = diff;
        }
        orig_prof += (i - min_col + 1);
    }
    res.original_bandwidth = orig_bw;
    res.original_profile = orig_prof;

    // Build undirected adjacency graph with degree
    std::vector<std::vector<std::size_t>> adj(n);
    std::vector<std::size_t> degrees(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t start = A.row_ptr[i];
        const std::size_t end = A.row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t j = static_cast<std::size_t>(A.col_idx[p]);
            if (j != i) {
                adj[i].push_back(j);
            }
        }
        std::sort(adj[i].begin(), adj[i].end());
        adj[i].erase(std::unique(adj[i].begin(), adj[i].end()), adj[i].end());
        degrees[i] = adj[i].size();
    }

    std::vector<bool> visited(n, false);
    std::vector<std::size_t> cm_order;
    cm_order.reserve(n);

    // Process all connected components
    for (std::size_t comp_start = 0; comp_start < n; ++comp_start) {
        if (visited[comp_start]) continue;

        // Find pseudo-peripheral root via George-Liu iterative BFS
        std::size_t root = comp_start;
        // Start from node with minimum degree in current component
        {
            std::queue<std::size_t> q;
            std::vector<std::size_t> comp_nodes;
            q.push(comp_start);
            std::vector<bool> in_comp(n, false);
            in_comp[comp_start] = true;

            while (!q.empty()) {
                std::size_t u = q.front();
                q.pop();
                comp_nodes.push_back(u);
                for (std::size_t v : adj[u]) {
                    if (!visited[v] && !in_comp[v]) {
                        in_comp[v] = true;
                        q.push(v);
                    }
                }
            }

            std::size_t min_deg = std::numeric_limits<std::size_t>::max();
            for (std::size_t u : comp_nodes) {
                if (degrees[u] < min_deg) {
                    min_deg = degrees[u];
                    root = u;
                }
            }
        }

        // Refine pseudo-peripheral node (up to 5 iterations)
        for (int iter = 0; iter < 5; ++iter) {
            std::queue<std::size_t> q;
            std::vector<int> dist(n, -1);
            q.push(root);
            dist[root] = 0;
            int max_dist = 0;
            std::vector<std::size_t> last_level;

            while (!q.empty()) {
                std::size_t u = q.front();
                q.pop();
                if (dist[u] > max_dist) {
                    max_dist = dist[u];
                    last_level.clear();
                }
                if (dist[u] == max_dist) {
                    last_level.push_back(u);
                }
                for (std::size_t v : adj[u]) {
                    if (!visited[v] && dist[v] == -1) {
                        dist[v] = dist[u] + 1;
                        q.push(v);
                    }
                }
            }

            // Pick candidate in last level with minimum degree
            std::size_t best_cand = root;
            std::size_t min_deg = std::numeric_limits<std::size_t>::max();
            for (std::size_t cand : last_level) {
                if (degrees[cand] < min_deg) {
                    min_deg = degrees[cand];
                    best_cand = cand;
                }
            }
            if (best_cand == root) break;
            root = best_cand;
        }

        // BFS from root to construct Cuthill-McKee order
        std::queue<std::size_t> q;
        q.push(root);
        visited[root] = true;

        while (!q.empty()) {
            std::size_t u = q.front();
            q.pop();
            cm_order.push_back(u);

            // Get unvisited neighbors and sort by degree ascending; tie-break by index
            std::vector<std::size_t> unvisited_nbrs;
            for (std::size_t v : adj[u]) {
                if (!visited[v]) {
                    unvisited_nbrs.push_back(v);
                }
            }
            std::sort(unvisited_nbrs.begin(), unvisited_nbrs.end(), [&degrees](std::size_t a, std::size_t b) {
                if (degrees[a] != degrees[b]) return degrees[a] < degrees[b];
                return a < b;
            });

            for (std::size_t v : unvisited_nbrs) {
                if (!visited[v]) {
                    visited[v] = true;
                    q.push(v);
                }
            }
        }
    }

    // Reverse Cuthill-McKee ordering: perm[i] = cm_order[n - 1 - i]
    res.perm.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        res.perm[i] = cm_order[n - 1 - i];
    }
    res.inv_perm = invert_permutation(res.perm);
    res.is_valid = validate_permutation(res.perm);

    // Compute permuted bandwidth and profile
    std::size_t perm_bw = 0;
    std::size_t perm_prof = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t orig_row = res.perm[i];
        std::size_t min_col = i;
        const std::size_t start = A.row_ptr[orig_row];
        const std::size_t end = A.row_ptr[orig_row + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t orig_col = static_cast<std::size_t>(A.col_idx[p]);
            std::size_t perm_col = res.inv_perm[orig_col];
            if (perm_col <= i && perm_col < min_col) {
                min_col = perm_col;
            }
            std::size_t diff = (i > perm_col) ? (i - perm_col) : (perm_col - i);
            if (diff > perm_bw) perm_bw = diff;
        }
        perm_prof += (i - min_col + 1);
    }
    res.permuted_bandwidth = perm_bw;
    res.permuted_profile = perm_prof;

    return res;
}

CSRMatrix RCMOrdering::permute_matrix(const CSRMatrix& A, 
                                     const std::vector<std::size_t>& perm, 
                                     const std::vector<std::size_t>& inv_perm) {
    std::size_t n = A.num_rows;
    if (n != A.num_cols) {
        throw std::invalid_argument("RCMOrdering::permute_matrix: matrix must be square (" + 
                                    std::to_string(n) + " x " + std::to_string(A.num_cols) + ")");
    }
    if (perm.size() != n || inv_perm.size() != n) {
        throw std::invalid_argument("RCMOrdering::permute_matrix: permutation size mismatch (" + 
                                    std::to_string(perm.size()) + " vs matrix dim " + std::to_string(n) + ")");
    }
    if (!validate_permutation(perm) || !validate_permutation(inv_perm)) {
        throw std::invalid_argument("RCMOrdering::permute_matrix: invalid permutation (not a bijection)");
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (inv_perm[perm[i]] != i) {
            throw std::invalid_argument("RCMOrdering::permute_matrix: perm and inv_perm are not mutual inverses at index " + std::to_string(i));
        }
    }
    (void)perm;
    SparseMatrixBuilder builder(n, n);
    builder.reserve(A.nnz());

    for (std::size_t old_i = 0; old_i < n; ++old_i) {
        std::size_t new_i = inv_perm[old_i];
        const std::size_t start = A.row_ptr[old_i];
        const std::size_t end = A.row_ptr[old_i + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t old_j = static_cast<std::size_t>(A.col_idx[p]);
            std::size_t new_j = inv_perm[old_j];
            builder.add_entry(new_i, new_j, A.values[p]);
        }
    }
    return builder.build(true);
}

std::vector<double> RCMOrdering::permute_vector(const std::vector<double>& b, 
                                                const std::vector<std::size_t>& perm) {
    if (b.size() != perm.size()) {
        throw std::invalid_argument("RCMOrdering::permute_vector: size mismatch (b: " + 
                                    std::to_string(b.size()) + " vs perm: " + std::to_string(perm.size()) + ")");
    }
    if (!validate_permutation(perm)) {
        throw std::invalid_argument("RCMOrdering::permute_vector: invalid permutation (not a bijection)");
    }
    std::vector<double> b_perm(b.size());
    for (std::size_t i = 0; i < b.size(); ++i) {
        b_perm[i] = b[perm[i]];
    }
    return b_perm;
}

std::vector<double> RCMOrdering::unpermute_vector(const std::vector<double>& x_perm, 
                                                  const std::vector<std::size_t>& inv_perm) {
    if (x_perm.size() != inv_perm.size()) {
        throw std::invalid_argument("RCMOrdering::unpermute_vector: size mismatch (x_perm: " + 
                                    std::to_string(x_perm.size()) + " vs inv_perm: " + std::to_string(inv_perm.size()) + ")");
    }
    if (!validate_permutation(inv_perm)) {
        throw std::invalid_argument("RCMOrdering::unpermute_vector: invalid permutation (not a bijection)");
    }
    std::vector<double> x(x_perm.size());
    for (std::size_t i = 0; i < x_perm.size(); ++i) {
        x[i] = x_perm[inv_perm[i]];
    }
    return x;
}

// =========================================================================
// ProfileCholesky Implementation
// =========================================================================

ProfileCholesky::Status ProfileCholesky::factor(const CSRMatrix& A, bool use_rcm) {
    metrics_ = Metrics();
    use_rcm_ = use_rcm;

    std::string val_err;
    if (!A.is_valid(&val_err)) {
        metrics_.status = Status::FAILED_DIMENSION_MISMATCH;
        metrics_.error_message = "CSR structural validation failed: " + val_err;
        return metrics_.status;
    }

    if (A.num_rows != A.num_cols || A.num_rows == 0) {
        metrics_.status = Status::FAILED_DIMENSION_MISMATCH;
        metrics_.error_message = "Matrix must be square and non-empty (" + 
                                 std::to_string(A.num_rows) + " x " + std::to_string(A.num_cols) + ")";
        return metrics_.status;
    }

    if (!A.is_symmetric(10.0 * EPS_MACH, &val_err)) {
        metrics_.status = Status::FAILED_NON_SPD;
        metrics_.error_message = "Matrix is not symmetric: " + val_err;
        return metrics_.status;
    }

    if (!A.has_strictly_positive_diagonal(&val_err)) {
        metrics_.status = Status::FAILED_NON_SPD;
        metrics_.error_message = "Matrix diagonal is not strictly positive: " + val_err;
        return metrics_.status;
    }

    dim_ = A.num_rows;
    metrics_.matrix_dim = dim_;
    metrics_.matrix_nnz = A.nnz();

    // Reorder matrix with RCM if enabled
    CSRMatrix A_fact;
    if (use_rcm_) {
        rcm_result_ = RCMOrdering::compute(A);
        if (!rcm_result_.is_valid) {
            metrics_.status = Status::FAILED_NUMERICAL_INSTABILITY;
            metrics_.error_message = "Failed to compute valid RCM permutation";
            return metrics_.status;
        }
        A_fact = RCMOrdering::permute_matrix(A, rcm_result_.perm, rcm_result_.inv_perm);
    } else {
        A_fact = A;
    }

    // Compute profile envelope: col_start[i] = min column index stored in row i (for j <= i)
    col_start_.assign(dim_, 0);
    profile_row_ptr_.assign(dim_ + 1, 0);

    for (std::size_t i = 0; i < dim_; ++i) {
        std::size_t min_col = i;
        const std::size_t start = A_fact.row_ptr[i];
        const std::size_t end = A_fact.row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t j = static_cast<std::size_t>(A_fact.col_idx[p]);
            if (j <= i && j < min_col) {
                min_col = j;
            }
        }
        col_start_[i] = min_col;
        profile_row_ptr_[i + 1] = profile_row_ptr_[i] + (i - min_col + 1);
    }

    metrics_.envelope_capacity = profile_row_ptr_[dim_];
    profile_values_.assign(metrics_.envelope_capacity, 0.0);

    // Copy lower triangle of A_fact into envelope storage
    for (std::size_t i = 0; i < dim_; ++i) {
        const std::size_t start = A_fact.row_ptr[i];
        const std::size_t end = A_fact.row_ptr[i + 1];
        for (std::size_t p = start; p < end; ++p) {
            std::size_t j = static_cast<std::size_t>(A_fact.col_idx[p]);
            if (j <= i) {
                std::size_t offset = profile_row_ptr_[i] + (j - col_start_[i]);
                profile_values_[offset] = A_fact.values[p];
            }
        }
    }

    // Profile Cholesky factorization algorithm: A_fact = L * L^T
    std::size_t flops = 0;
    double min_pivot = std::numeric_limits<double>::infinity();
    double max_pivot = -std::numeric_limits<double>::infinity();

    for (std::size_t j = 0; j < dim_; ++j) {
        const std::size_t start_j = col_start_[j];
        const std::size_t row_j_base = profile_row_ptr_[j];

        // 1. Off-diagonal elements L_{j, k} for k in [start_j, j - 1]
        for (std::size_t k = start_j; k < j; ++k) {
            const std::size_t start_k = col_start_[k];
            const std::size_t m_min = std::max(start_j, start_k);
            
            double dot_sum = 0.0;
            if (m_min < k) {
                const std::size_t row_k_base = profile_row_ptr_[k];
                for (std::size_t m = m_min; m < k; ++m) {
                    dot_sum += profile_values_[row_j_base + (m - start_j)] * 
                               profile_values_[row_k_base + (m - start_k)];
                    flops += 2; // multiply-accumulate
                }
            }

            double L_kk = profile_values_[profile_row_ptr_[k] + (k - start_k)];
            if (L_kk <= 0.0 || std::isnan(L_kk)) {
                metrics_.status = Status::FAILED_NON_SPD;
                metrics_.error_message = "Non-positive diagonal encountered during factor solve at row " + std::to_string(k);
                return metrics_.status;
            }

            std::size_t jk_offset = row_j_base + (k - start_j);
            double val = (profile_values_[jk_offset] - dot_sum) / L_kk;
            profile_values_[jk_offset] = val;
            flops += 2; // subtract and divide
        }

        // 2. Diagonal element L_{j, j}
        double orig_Ajj = profile_values_[row_j_base + (j - start_j)];
        if (orig_Ajj <= 0.0 || std::isnan(orig_Ajj)) {
            metrics_.status = Status::FAILED_NON_SPD;
            metrics_.error_message = "Non-positive diagonal entry A(" + std::to_string(j) + "," + 
                                     std::to_string(j) + ") = " + std::to_string(orig_Ajj);
            return metrics_.status;
        }

        double diag_sum = 0.0;
        for (std::size_t m = start_j; m < j; ++m) {
            double v = profile_values_[row_j_base + (m - start_j)];
            diag_sum += v * v;
            flops += 2; // multiply and accumulate
        }

        double D_jj = orig_Ajj - diag_sum;
        flops += 2; // subtract and sqrt

        // Strictly scale-relative pivot validation: D_jj > 10 * eps_mach * orig_Ajj
        double pivot_floor = 10.0 * EPS_MACH * orig_Ajj;
        if (std::isnan(D_jj) || std::isinf(D_jj) || D_jj <= pivot_floor) {
            metrics_.status = (D_jj <= 0.0) ? Status::FAILED_NON_SPD : Status::FAILED_ZERO_PIVOT;
            metrics_.error_message = "Pivot D(" + std::to_string(j) + "," + std::to_string(j) + 
                                     ") = " + std::to_string(D_jj) + " failed scale-relative threshold (" + 
                                     std::to_string(pivot_floor) + ")";
            return metrics_.status;
        }

        if (D_jj < min_pivot) min_pivot = D_jj;
        if (D_jj > max_pivot) max_pivot = D_jj;

        profile_values_[row_j_base + (j - start_j)] = std::sqrt(D_jj);
    }

    metrics_.min_pivot = min_pivot;
    metrics_.max_pivot = max_pivot;
    metrics_.factorization_flops = flops;

    // Numerical factor non-zero count NNZ_L (entries above scale-invariant drop tolerance)
    std::size_t nnz_L = 0;
    double eps_drop = 10.0 * EPS_MACH;

    for (std::size_t i = 0; i < dim_; ++i) {
        const std::size_t start_i = col_start_[i];
        const std::size_t base_i = profile_row_ptr_[i];
        double L_ii = profile_values_[base_i + (i - start_i)];
        for (std::size_t j = start_i; j <= i; ++j) {
            double val = profile_values_[base_i + (j - start_i)];
            double L_jj = profile_values_[profile_row_ptr_[j] + (j - col_start_[j])];
            // Scale-invariant threshold: L_ij and sqrt(L_ii * L_jj) both scale as sqrt(scale(A))
            double threshold = eps_drop * std::sqrt(L_ii * L_jj);
            if (std::abs(val) > threshold) {
                nnz_L++;
            }
        }
    }
    metrics_.numerical_factor_nnz = nnz_L;

    // Fill-in ratio relative to lower triangle of A: r_fill = NNZ_L / (0.5 * (NNZ_A - M) + M)
    double A_lower_nnz = 0.5 * (static_cast<double>(A.nnz()) - static_cast<double>(dim_)) + static_cast<double>(dim_);
    if (A_lower_nnz <= 0.0) A_lower_nnz = 1.0;
    metrics_.fill_in_ratio = static_cast<double>(nnz_L) / A_lower_nnz;

    // Memory footprint: profile values + row ptrs + col start
    metrics_.allocated_factor_bytes = metrics_.envelope_capacity * sizeof(double) + 
                                      (dim_ + 1) * sizeof(std::size_t) + 
                                      dim_ * sizeof(std::size_t);

    metrics_.status = Status::SUCCESS;
    metrics_.error_message = "Factorization converged successfully";
    return metrics_.status;
}

bool ProfileCholesky::solve(const double* b, double* x) const {
    if (!b || !x || metrics_.status != Status::SUCCESS || dim_ == 0) {
        return false;
    }

    // Reject non-finite RHS values immediately
    for (std::size_t i = 0; i < dim_; ++i) {
        if (!std::isfinite(b[i])) {
            return false;
        }
    }

    std::vector<double> b_perm(dim_);
    if (use_rcm_) {
        for (std::size_t i = 0; i < dim_; ++i) {
            b_perm[i] = b[rcm_result_.perm[i]];
        }
    } else {
        std::copy(b, b + dim_, b_perm.begin());
    }

    std::vector<double> y(dim_, 0.0);

    // Forward solve: L * y = b_perm
    for (std::size_t i = 0; i < dim_; ++i) {
        double sum = b_perm[i];
        const std::size_t start_i = col_start_[i];
        const std::size_t base_i = profile_row_ptr_[i];
        for (std::size_t j = start_i; j < i; ++j) {
            sum -= profile_values_[base_i + (j - start_i)] * y[j];
        }
        double L_ii = profile_values_[base_i + (i - start_i)];
        y[i] = sum / L_ii;
    }

    // Backward solve: L^T * z = y (overwriting y with z)
    for (std::size_t idx = 0; idx < dim_; ++idx) {
        std::size_t i = dim_ - 1 - idx;
        const std::size_t start_i = col_start_[i];
        const std::size_t base_i = profile_row_ptr_[i];
        double L_ii = profile_values_[base_i + (i - start_i)];
        y[i] /= L_ii;
        double z_i = y[i];
        for (std::size_t j = start_i; j < i; ++j) {
            y[j] -= profile_values_[base_i + (j - start_i)] * z_i;
        }
    }

    // Unpermute solution: x[perm[i]] = y[i]
    if (use_rcm_) {
        for (std::size_t i = 0; i < dim_; ++i) {
            x[rcm_result_.perm[i]] = y[i];
        }
    } else {
        std::copy(y.begin(), y.end(), x);
    }

    // Verify all computed solution values are finite before returning success
    for (std::size_t i = 0; i < dim_; ++i) {
        if (!std::isfinite(x[i])) {
            return false;
        }
    }

    return true;
}

bool ProfileCholesky::solve(const std::vector<double>& b, std::vector<double>& x) const {
    if (metrics_.status != Status::SUCCESS || dim_ == 0 || b.size() != dim_) {
        x.assign(dim_, 0.0);
        return false;
    }
    x.assign(dim_, 0.0);
    bool ok = solve(b.data(), x.data());
    if (!ok) {
        x.assign(dim_, 0.0);
        return false;
    }
    return true;
}

double ProfileCholesky::compute_reconstruction_error(const CSRMatrix& A_target) const {
    // Return documented failure value -1.0 if factor is not ready or input is invalid
    if (metrics_.status != Status::SUCCESS || dim_ == 0) {
        return -1.0;
    }

    if (!A_target.is_valid()) {
        return -1.0;
    }

    if (A_target.num_rows != A_target.num_cols || A_target.num_rows != dim_) {
        return -1.0;
    }

    if (!A_target.is_symmetric(10.0 * EPS_MACH)) {
        return -1.0;
    }

    double a_norm = A_target.norm_inf();
    if (!std::isfinite(a_norm) || a_norm <= 0.0) {
        return -1.0;
    }

    std::vector<double> row_diff_sums(dim_, 0.0);

    for (std::size_t i = 0; i < dim_; ++i) {
        const std::size_t start_i = col_start_[i];
        const std::size_t base_i = profile_row_ptr_[i];

        for (std::size_t j = 0; j <= i; ++j) {
            double a_ij = A_target.get_entry(i, j);
            if (!std::isfinite(a_ij)) {
                return -1.0;
            }
            double recon_ij = 0.0;
            const std::size_t start_j = col_start_[j];
            std::size_t m_min = std::max(start_i, start_j);

            if (m_min <= j) {
                const std::size_t base_j = profile_row_ptr_[j];
                for (std::size_t m = m_min; m <= j; ++m) {
                    recon_ij += profile_values_[base_i + (m - start_i)] * 
                                profile_values_[base_j + (m - start_j)];
                }
            }

            double diff = std::abs(a_ij - recon_ij);
            row_diff_sums[i] += diff;
            if (i != j) {
                row_diff_sums[j] += diff;
            }
        }
    }

    double max_row_sum = 0.0;
    for (std::size_t i = 0; i < dim_; ++i) {
        if (!std::isfinite(row_diff_sums[i])) {
            return -1.0;
        }
        if (row_diff_sums[i] > max_row_sum) {
            max_row_sum = row_diff_sums[i];
        }
    }
    return max_row_sum / a_norm;
}

double ProfileCholesky::compute_backward_error(const CSRMatrix& A, 
                                              const std::vector<double>& b, 
                                              const std::vector<double>& x) const {
    if (A.num_rows != dim_ || b.size() != dim_ || x.size() != dim_) {
        return 1.0;
    }
    std::vector<double> r(dim_, 0.0);
    A.matvec(x.data(), r.data());
    for (std::size_t i = 0; i < dim_; ++i) {
        r[i] = b[i] - r[i];
    }
    double r_norm = vector_norm2(r);
    double a_norm = A.norm_inf();
    double x_norm = vector_norm2(x);
    double b_norm = vector_norm2(b);
    double denom = a_norm * x_norm + b_norm;
    if (denom <= 0.0) return 0.0;
    return r_norm / denom;
}

// =========================================================================
// Mathematical Vector Helper Functions
// =========================================================================

double vector_dot(const double* a, const double* b, std::size_t n) {
    if (!a || !b) {
        throw std::invalid_argument("vector_dot: null pointer argument");
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

double vector_dot(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("vector_dot: dimension mismatch (" + 
                                    std::to_string(a.size()) + " vs " + std::to_string(b.size()) + ")");
    }
    return vector_dot(a.data(), b.data(), a.size());
}

double vector_norm2(const double* a, std::size_t n) {
    if (!a) {
        throw std::invalid_argument("vector_norm2: null pointer argument");
    }
    return std::sqrt(vector_dot(a, a, n));
}

double vector_norm2(const std::vector<double>& a) {
    return vector_norm2(a.data(), a.size());
}

double vector_norm_inf(const double* a, std::size_t n) {
    if (!a) {
        throw std::invalid_argument("vector_norm_inf: null pointer argument");
    }
    double max_val = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double v = std::abs(a[i]);
        if (v > max_val) max_val = v;
    }
    return max_val;
}

double vector_norm_inf(const std::vector<double>& a) {
    return vector_norm_inf(a.data(), a.size());
}

} // namespace workbench

