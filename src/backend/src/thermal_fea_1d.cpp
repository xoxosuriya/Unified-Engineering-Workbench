#include "thermal_fea_1d.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

namespace workbench {

double ThermalFEMSolver1D::exact_solution(double x, const ThermalParams1D& p) {
    // T_exact(x) = T0 - (T0 - TL)*x/L + [q / (2*k)] * x * (L - x)
    double linear_part = p.temp_T0 - (p.temp_T0 - p.temp_TL) * (x / p.length_L);
    double source_part = (p.heat_gen_q / (2.0 * p.conductivity_k)) * x * (p.length_L - x);
    return linear_part + source_part;
}

ThermalSolveResult1D ThermalFEMSolver1D::solve(const ThermalParams1D& params) {
    if (params.num_elements_N == 0) {
        throw std::invalid_argument("Element count N must be at least 1.");
    }
    if (params.length_L <= 0.0) {
        throw std::invalid_argument("Length L must be positive.");
    }
    if (params.area_A <= 0.0) {
        throw std::invalid_argument("Cross-sectional area A must be positive.");
    }
    if (params.conductivity_k <= 0.0) {
        throw std::invalid_argument("Thermal conductivity k must be positive.");
    }

    const size_t N = params.num_elements_N;
    const size_t num_nodes = N + 1;
    const double h = params.length_L / static_cast<double>(N);

    ThermalSolveResult1D res;
    res.params = params;
    res.element_size_h = h;
    res.node_x.resize(num_nodes);
    res.node_temp_fea.resize(num_nodes, 0.0);
    res.node_temp_exact.resize(num_nodes, 0.0);
    res.node_abs_error.resize(num_nodes, 0.0);
    res.midpoints.resize(N);

    // Compute node coordinates and exact values
    for (size_t i = 0; i < num_nodes; ++i) {
        res.node_x[i] = static_cast<double>(i) * h;
        res.node_temp_exact[i] = exact_solution(res.node_x[i], params);
    }

    res.node_temp_fea[0] = params.temp_T0;
    res.node_temp_fea[N] = params.temp_TL;

    if (N == 1) {
        // Single element with 2 boundary nodes (Dirichlet on both ends)
    } else {
        const size_t M = N - 1; // Number of interior unknowns (nodes 1 to N-1)

        // -------------------------------------------------------------
        // Task C Core Requirement: Explicit Element Formulation & Assembly
        // Element stiffness: k^e = (kA/h) * [ 1, -1 ; -1,  1 ]
        // Element load:      f^e = (qAh/2) * [ 1 ; 1 ]
        // -------------------------------------------------------------
        const double k_coeff = (params.conductivity_k * params.area_A) / h;
        const double f_coeff = (params.heat_gen_q * params.area_A * h) / 2.0;

        // Global stiffness diagonals and load vector for interior unknowns
        // Interior node indices in global indexing: i = 1, 2, ..., N-1
        // Corresponding interior unknown indices:  m = 0, 1, ..., M-1 (m = i - 1)
        std::vector<double> diag(M, 0.0);
        std::vector<double> sub(M > 1 ? M - 1 : 0, 0.0);
        std::vector<double> sup(M > 1 ? M - 1 : 0, 0.0);
        std::vector<double> rhs(M, 0.0);

        // Assemble element contributions into the global interior system:
        // Element e connects node e and node e+1.
        for (size_t e = 0; e < N; ++e) {
            size_t n1 = e;
            size_t n2 = e + 1;

            // Contribution to node n1
            if (n1 >= 1 && n1 <= M) {
                size_t m1 = n1 - 1;
                diag[m1] += k_coeff; // k^e[0][0]
                rhs[m1] += f_coeff;  // f^e[0]
                if (n2 == 0) {
                    rhs[m1] -= (-k_coeff) * params.temp_T0; // Dirichlet coupling
                } else if (n2 == N) {
                    rhs[m1] -= (-k_coeff) * params.temp_TL; // Dirichlet coupling
                } else {
                    size_t m2 = n2 - 1;
                    if (m1 + 1 == m2 && m1 < sup.size()) {
                        sup[m1] += (-k_coeff); // k^e[0][1]
                    }
                }
            }

            // Contribution to node n2
            if (n2 >= 1 && n2 <= M) {
                size_t m2 = n2 - 1;
                diag[m2] += k_coeff; // k^e[1][1]
                rhs[m2] += f_coeff;  // f^e[1]
                if (n1 == 0) {
                    rhs[m2] -= (-k_coeff) * params.temp_T0; // Dirichlet coupling
                } else if (n1 == N) {
                    rhs[m2] -= (-k_coeff) * params.temp_TL; // Dirichlet coupling
                } else {
                    size_t m1 = n1 - 1;
                    if (m2 == m1 + 1 && m1 < sub.size()) {
                        sub[m1] += (-k_coeff); // k^e[1][0]
                    }
                }
            }
        }

        // Solve interior tridiagonal system:
        if (M == 1) {
            // Exactly one interior node (N = 2)
            double T_interior = rhs[0] / diag[0];
            res.node_temp_fea[1] = T_interior;
        } else {
            // Thomas algorithm (TDMA) for M >= 2 unknowns
            std::vector<double> cp(M - 1, 0.0);
            std::vector<double> dp(M, 0.0);

            cp[0] = sup[0] / diag[0];
            dp[0] = rhs[0] / diag[0];

            for (size_t i = 1; i < M - 1; ++i) {
                double denom = diag[i] - sub[i - 1] * cp[i - 1];
                cp[i] = sup[i] / denom;
                dp[i] = (rhs[i] - sub[i - 1] * dp[i - 1]) / denom;
            }

            double denom_last = diag[M - 1] - sub[M - 2] * cp[M - 2];
            dp[M - 1] = (rhs[M - 1] - sub[M - 2] * dp[M - 2]) / denom_last;

            // Back substitution
            res.node_temp_fea[M] = dp[M - 1]; // global node M (interior index M-1)
            for (int i = static_cast<int>(M) - 2; i >= 0; --i) {
                dp[i] = dp[i] - cp[i] * dp[i + 1];
                res.node_temp_fea[i + 1] = dp[i]; // global node i + 1
            }
        }
    }

    // Evaluate errors at nodes
    res.max_nodal_error = 0.0;
    for (size_t i = 0; i < num_nodes; ++i) {
        res.node_abs_error[i] = std::abs(res.node_temp_fea[i] - res.node_temp_exact[i]);
        res.max_nodal_error = std::max(res.max_nodal_error, res.node_abs_error[i]);
    }

    // Evaluate errors at element midpoints using FE linear interpolation:
    // T_h(x_mid) = (T_i + T_{i+1}) / 2
    res.max_midpoint_error = 0.0;
    for (size_t e = 0; e < N; ++e) {
        ElementMidpointResult mid;
        mid.element_index = e;
        mid.x_mid = 0.5 * (res.node_x[e] + res.node_x[e + 1]);
        mid.temp_fea_mid = 0.5 * (res.node_temp_fea[e] + res.node_temp_fea[e + 1]);
        mid.temp_exact_mid = exact_solution(mid.x_mid, params);
        mid.abs_error_mid = std::abs(mid.temp_fea_mid - mid.temp_exact_mid);

        res.max_midpoint_error = std::max(res.max_midpoint_error, mid.abs_error_mid);
        res.midpoints[e] = mid;
    }

    return res;
}

ConvergenceStudyResult ThermalFEMSolver1D::run_convergence_study(const ThermalParams1D& base_params) {
    ConvergenceStudyResult study;
    study.base_params = base_params;
    study.mesh_sizes = {10, 20, 40, 80};

    double prev_error = 0.0;
    for (size_t N : study.mesh_sizes) {
        ThermalParams1D p = base_params;
        p.num_elements_N = N;
        ThermalSolveResult1D solve_res = solve(p);
        study.individual_solves.push_back(solve_res);

        ConvergenceRow row;
        row.num_elements = N;
        row.element_size_h = solve_res.element_size_h;
        row.max_midpoint_error = solve_res.max_midpoint_error;
        // Theoretical midpoint error for uniform q, k: q * h^2 / (8 * k)
        row.theoretical_midpoint_error = (p.heat_gen_q * row.element_size_h * row.element_size_h) / (8.0 * p.conductivity_k);
        row.max_nodal_error = solve_res.max_nodal_error;

        if (prev_error > 0.0) {
            row.error_reduction_ratio = prev_error / row.max_midpoint_error;
            row.observed_convergence_rate = std::log2(row.error_reduction_ratio);
        } else {
            row.error_reduction_ratio = 1.0;
            row.observed_convergence_rate = 2.0; // Initial baseline
        }
        prev_error = row.max_midpoint_error;

        study.rows.push_back(row);
    }

    return study;
}

std::string ThermalSolveResult1D::to_json() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << "{";
    ss << "\"N\":" << params.num_elements_N << ",";
    ss << "\"h\":" << element_size_h << ",";
    ss << "\"L\":" << params.length_L << ",";
    ss << "\"A\":" << params.area_A << ",";
    ss << "\"k\":" << params.conductivity_k << ",";
    ss << "\"q\":" << params.heat_gen_q << ",";
    ss << "\"T0\":" << params.temp_T0 << ",";
    ss << "\"TL\":" << params.temp_TL << ",";
    ss << "\"max_midpoint_error\":" << max_midpoint_error << ",";
    ss << "\"max_nodal_error\":" << max_nodal_error << ",";

    // Arrays
    ss << "\"x\":[";
    for (size_t i = 0; i < node_x.size(); ++i) {
        if (i > 0) ss << ",";
        ss << node_x[i];
    }
    ss << "],";

    ss << "\"T_fea\":[";
    for (size_t i = 0; i < node_temp_fea.size(); ++i) {
        if (i > 0) ss << ",";
        ss << node_temp_fea[i];
    }
    ss << "],";

    ss << "\"T_exact\":[";
    for (size_t i = 0; i < node_temp_exact.size(); ++i) {
        if (i > 0) ss << ",";
        ss << node_temp_exact[i];
    }
    ss << "],";

    ss << "\"nodal_error\":[";
    for (size_t i = 0; i < node_abs_error.size(); ++i) {
        if (i > 0) ss << ",";
        ss << node_abs_error[i];
    }
    ss << "],";

    ss << "\"midpoints\":[";
    for (size_t i = 0; i < midpoints.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{\"elem\":" << midpoints[i].element_index << ","
           << "\"x_mid\":" << midpoints[i].x_mid << ","
           << "\"T_fea_mid\":" << midpoints[i].temp_fea_mid << ","
           << "\"T_exact_mid\":" << midpoints[i].temp_exact_mid << ","
           << "\"abs_error\":" << midpoints[i].abs_error_mid << "}";
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

std::string ThermalSolveResult1D::to_csv() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << "# 1D Thermal FEA Solution (Uniform Rod, Linear 2-Node Elements)\n";
    ss << "# Parameters: L=" << params.length_L << " m, A=" << params.area_A 
       << " m^2, k=" << params.conductivity_k << " W/(m K), q=" << params.heat_gen_q 
       << " W/m^3, T(0)=" << params.temp_T0 << " C, T(L)=" << params.temp_TL 
       << " C, N=" << params.num_elements_N << " elements, h=" << element_size_h << " m\n";
    ss << "# Max Midpoint Error: " << max_midpoint_error << " C, Max Nodal Error: " << max_nodal_error << " C\n";
    ss << "node_index,x_m,T_fea_C,T_exact_C,nodal_abs_error_C\n";
    for (size_t i = 0; i < node_x.size(); ++i) {
        ss << i << "," << node_x[i] << "," << node_temp_fea[i] << "," 
           << node_temp_exact[i] << "," << node_abs_error[i] << "\n";
    }

    ss << "\n# Midpoint Evaluation (FE Linear Interpolation vs Exact Analytical)\n";
    ss << "element_index,x_mid_m,T_fea_mid_C,T_exact_mid_C,midpoint_abs_error_C\n";
    for (const auto& mid : midpoints) {
        ss << mid.element_index << "," << mid.x_mid << "," << mid.temp_fea_mid 
           << "," << mid.temp_exact_mid << "," << mid.abs_error_mid << "\n";
    }
    return ss.str();
}

std::string ConvergenceStudyResult::to_json() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << "{";
    ss << "\"base_params\":{";
    ss << "\"L\":" << base_params.length_L << ",";
    ss << "\"A\":" << base_params.area_A << ",";
    ss << "\"k\":" << base_params.conductivity_k << ",";
    ss << "\"q\":" << base_params.heat_gen_q << ",";
    ss << "\"T0\":" << base_params.temp_T0 << ",";
    ss << "\"TL\":" << base_params.temp_TL;
    ss << "},";

    ss << "\"rows\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{\"N\":" << rows[i].num_elements << ","
           << "\"h\":" << rows[i].element_size_h << ","
           << "\"max_midpoint_error\":" << rows[i].max_midpoint_error << ","
           << "\"theoretical_midpoint_error\":" << rows[i].theoretical_midpoint_error << ","
           << "\"max_nodal_error\":" << rows[i].max_nodal_error << ","
           << "\"reduction_ratio\":" << rows[i].error_reduction_ratio << ","
           << "\"observed_rate\":" << rows[i].observed_convergence_rate << "}";
    }
    ss << "],";

    ss << "\"solves\":[";
    for (size_t i = 0; i < individual_solves.size(); ++i) {
        if (i > 0) ss << ",";
        ss << individual_solves[i].to_json();
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

std::string ConvergenceStudyResult::to_csv() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8);
    ss << "mesh_N,element_size_h_m,max_midpoint_error_C,theoretical_midpoint_error_C,max_nodal_error_C,error_reduction_ratio,observed_convergence_rate\n";
    for (const auto& r : rows) {
        ss << r.num_elements << "," << r.element_size_h << "," 
           << r.max_midpoint_error << "," << r.theoretical_midpoint_error << "," 
           << r.max_nodal_error << "," << r.error_reduction_ratio << "," 
           << r.observed_convergence_rate << "\n";
    }
    return ss.str();
}

std::string ConvergenceStudyResult::to_profile_csv() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "x_m,T_exact_C,T_FE_N10_C,T_FE_N20_C,T_FE_N40_C,T_FE_N80_C\n";

    const size_t num_pts = 101; // 101 evaluation points from x = 0 to L
    const double L = base_params.length_L;

    // Helper to evaluate FE solution at arbitrary x via linear element interpolation
    auto eval_fe = [](const ThermalSolveResult1D& res, double x) -> double {
        if (x <= res.node_x.front()) return res.node_temp_fea.front();
        if (x >= res.node_x.back()) return res.node_temp_fea.back();
        size_t N = res.params.num_elements_N;
        double h = res.element_size_h;
        size_t e = static_cast<size_t>(x / h);
        if (e >= N) e = N - 1;
        double x_left = res.node_x[e];
        double xi = (x - x_left) / h;
        if (xi < 0.0) xi = 0.0;
        if (xi > 1.0) xi = 1.0;
        return (1.0 - xi) * res.node_temp_fea[e] + xi * res.node_temp_fea[e + 1];
    };

    for (size_t i = 0; i < num_pts; ++i) {
        double x = (static_cast<double>(i) / static_cast<double>(num_pts - 1)) * L;
        double t_exact = ThermalFEMSolver1D::exact_solution(x, base_params);

        ss << x << "," << t_exact;
        for (const auto& solve : individual_solves) {
            ss << "," << eval_fe(solve, x);
        }
        ss << "\n";
    }

    return ss.str();
}

} // namespace workbench
