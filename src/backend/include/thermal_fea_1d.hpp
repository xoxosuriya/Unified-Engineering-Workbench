#pragma once

#include <vector>
#include <string>

namespace workbench {

struct ThermalParams1D {
    double length_L = 1.0;          // Rod length [m]
    double area_A = 1e-4;           // Cross-sectional area [m^2]
    double conductivity_k = 10.0;   // Thermal conductivity [W/(m*K)]
    double heat_gen_q = 10000.0;    // Volumetric heat generation [W/m^3]
    double temp_T0 = 100.0;         // Left boundary prescribed temperature [C]
    double temp_TL = 20.0;          // Right boundary prescribed temperature [C]
    size_t num_elements_N = 10;     // Number of uniform 2-node linear elements
};

struct ElementMidpointResult {
    size_t element_index = 0;
    double x_mid = 0.0;             // Midpoint coordinate [m]
    double temp_fea_mid = 0.0;       // FE interpolated temperature: (T_i + T_i+1) / 2 [C]
    double temp_exact_mid = 0.0;     // Analytical temperature at midpoint [C]
    double abs_error_mid = 0.0;      // |temp_fea_mid - temp_exact_mid| [C]
};

struct ThermalSolveResult1D {
    ThermalParams1D params;
    std::vector<double> node_x;         // Node coordinates [m] (size N + 1)
    std::vector<double> node_temp_fea;  // FE computed temperatures at nodes [C]
    std::vector<double> node_temp_exact;// Analytical temperatures at nodes [C]
    std::vector<double> node_abs_error; // Nodal absolute error [C]
    
    std::vector<ElementMidpointResult> midpoints; // Midpoint evaluation
    
    double max_midpoint_error = 0.0;    // Maximum absolute error evaluated at element midpoints [C]
    double max_nodal_error = 0.0;       // Maximum absolute error evaluated at nodes [C]
    double element_size_h = 0.0;        // Element size h = L / N [m]

    std::string to_json() const;
    std::string to_csv() const;
};

struct ConvergenceRow {
    size_t num_elements = 0;
    double element_size_h = 0.0;
    double max_midpoint_error = 0.0;
    double theoretical_midpoint_error = 0.0; // q * h^2 / (8 * k)
    double max_nodal_error = 0.0;
    double error_reduction_ratio = 0.0;      // e_{h_prev} / e_h (ideal is 4.0 for O(h^2))
    double observed_convergence_rate = 0.0;  // log2(e_{prev} / e) (ideal is 2.0)
};

struct ConvergenceStudyResult {
    ThermalParams1D base_params;
    std::vector<size_t> mesh_sizes;          // Standard: {10, 20, 40, 80}
    std::vector<ConvergenceRow> rows;
    std::vector<ThermalSolveResult1D> individual_solves;

    std::string to_json() const;
    std::string to_csv() const;
};

class ThermalFEMSolver1D {
public:
    // Compute exact analytical solution at coordinate x
    // T_exact(x) = T0 - (T0 - TL)*x/L + [q / (2*k)] * x * (L - x)
    static double exact_solution(double x, const ThermalParams1D& p);

    // Solve 1D thermal steady-state BVP using 2-node linear elements
    static ThermalSolveResult1D solve(const ThermalParams1D& params);

    // Run convergence study for N = 10, 20, 40, 80
    static ConvergenceStudyResult run_convergence_study(const ThermalParams1D& base_params = ThermalParams1D());
};

} // namespace workbench
