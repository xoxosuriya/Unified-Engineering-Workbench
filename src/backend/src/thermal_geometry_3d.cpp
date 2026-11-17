#include "thermal_geometry_3d.hpp"
#include <cmath>
#include <stdexcept>
#include <cassert>
#include <algorithm>

namespace workbench {

ThermalGeometry3D::ThermalGeometry3D(ModelType type, int N, ThermalProperties props)
    : type_(type), N_(N), props_(props) {
    if (N < 2) {
        throw std::invalid_argument("Grid resolution N must be at least 2 (got " + std::to_string(N) + ")");
    }
    h_ = 0.1 / static_cast<double>(N_);
    build_mesh();
}

bool ThermalGeometry3D::is_active(int i, int j, int k) const noexcept {
    if (i < 0 || i >= N_ || j < 0 || j >= N_ || k < 0 || k >= N_) {
        return false;
    }
    if (type_ == ModelType::CUBE) {
        return true;
    }

    // ModelType::FINNED_HEAT_SINK:
    // Baseplate occupies z in [0, 0.02] m (bottom 20% of bounding cube height)
    const int k_base = static_cast<int>(std::round(0.2 * N_));
    if (k < k_base) {
        return true; // Active baseplate cell
    }

    // Longitudinal cooling fins occupy z in (0.02, 0.10] m
    // 4 fins along X:
    // Fin 1: x in [0.00, 0.01] m -> i in [0, 0.1 N - 1]
    // Fin 2: x in [0.03, 0.04] m -> i in [0.3 N, 0.4 N - 1]
    // Fin 3: x in [0.06, 0.07] m -> i in [0.6 N, 0.7 N - 1]
    // Fin 4: x in [0.09, 0.10] m -> i in [0.9 N, N - 1]
    const int fin1_end   = static_cast<int>(std::round(0.1 * N_));
    const int fin2_start = static_cast<int>(std::round(0.3 * N_));
    const int fin2_end   = static_cast<int>(std::round(0.4 * N_));
    const int fin3_start = static_cast<int>(std::round(0.6 * N_));
    const int fin3_end   = static_cast<int>(std::round(0.7 * N_));
    const int fin4_start = static_cast<int>(std::round(0.9 * N_));

    if (i < fin1_end) return true;
    if (i >= fin2_start && i < fin2_end) return true;
    if (i >= fin3_start && i < fin3_end) return true;
    if (i >= fin4_start) return true;

    return false; // Inter-fin void channel
}

int ThermalGeometry3D::get_active_index(int i, int j, int k) const noexcept {
    if (i < 0 || i >= N_ || j < 0 || j >= N_ || k < 0 || k >= N_) {
        return -1;
    }
    return grid_to_active_[flat_grid_id(i, j, k)];
}

void ThermalGeometry3D::get_grid_coords(std::size_t active_idx, int& i, int& j, int& k) const {
    if (active_idx >= active_to_grid_.size()) {
        throw std::out_of_range("Active index " + std::to_string(active_idx) + 
                                " out of range (M=" + std::to_string(active_to_grid_.size()) + ")");
    }
    std::size_t flat_id = active_to_grid_[active_idx];
    k = static_cast<int>(flat_id % N_);
    j = static_cast<int>((flat_id / N_) % N_);
    i = static_cast<int>(flat_id / (N_ * N_));
}

void ThermalGeometry3D::get_cell_centroid(std::size_t active_idx, double& x, double& y, double& z) const {
    int i = 0, j = 0, k = 0;
    get_grid_coords(active_idx, i, j, k);
    x = (static_cast<double>(i) + 0.5) * h_;
    y = (static_cast<double>(j) + 0.5) * h_;
    z = (static_cast<double>(k) + 0.5) * h_;
}

void ThermalGeometry3D::build_mesh() {
    stats_ = GeometryStats();
    stats_.model_type = type_;
    stats_.N = N_;
    stats_.cell_size = h_;
    stats_.total_cells = static_cast<std::size_t>(N_) * N_ * N_;

    grid_to_active_.assign(stats_.total_cells, -1);
    active_to_grid_.clear();
    boundary_faces_.clear();

    // 1. Identify active solid cells and assign contiguous linear DOFs
    for (int i = 0; i < N_; ++i) {
        for (int j = 0; j < N_; ++j) {
            for (int k = 0; k < N_; ++k) {
                if (is_active(i, j, k)) {
                    int u = static_cast<int>(active_to_grid_.size());
                    grid_to_active_[flat_grid_id(i, j, k)] = u;
                    active_to_grid_.push_back(flat_grid_id(i, j, k));
                }
            }
        }
    }

    stats_.active_cells = active_to_grid_.size();
    stats_.solid_volume = static_cast<double>(stats_.active_cells) * h_ * h_ * h_;

    // 2. Classify internal face pairs and exposed control-volume boundary faces
    const int di[6] = {-1,  1,  0,  0,  0,  0};
    const int dj[6] = { 0,  0, -1,  1,  0,  0};
    const int dk[6] = { 0,  0,  0,  0, -1,  1};

    std::size_t internal_face_count = 0;

    for (std::size_t u = 0; u < stats_.active_cells; ++u) {
        int i = 0, j = 0, k = 0;
        get_grid_coords(u, i, j, k);

        for (int d = 0; d < 6; ++d) {
            int ni = i + di[d];
            int nj = j + dj[d];
            int nk = k + dk[d];

            if (is_active(ni, nj, nk)) {
                // Internal face: count each shared pair once (e.g. from the smaller index cell)
                int v = get_active_index(ni, nj, nk);
                if (static_cast<std::size_t>(v) > u) {
                    internal_face_count++;
                }
            } else {
                // Exposed boundary face: classify according to blueprint specifications
                BoundaryFace bf;
                bf.cell_idx = u;
                bf.direction = d;

                if (type_ == ModelType::CUBE) {
                    if (d == 0) { // West face (x = 0)
                        bf.type = BoundaryType::DIRICHLET;
                        bf.prescribed_temp = props_.hot_temp;
                        stats_.dirichlet_faces++;
                    } else if (d == 1) { // East face (x = 0.10 m)
                        bf.type = BoundaryType::DIRICHLET;
                        bf.prescribed_temp = props_.cold_temp;
                        stats_.dirichlet_faces++;
                    } else { // South (y=0), North (y=0.10), Bottom (z=0), Top (z=0.10)
                        bf.type = BoundaryType::NEUMANN;
                        stats_.neumann_faces++;
                    }
                } else { // ModelType::FINNED_HEAT_SINK
                    if (d == 4 && k == 0) { // Baseplate bottom face (z = 0)
                        bf.type = BoundaryType::DIRICHLET;
                        bf.prescribed_temp = props_.hot_temp;
                        stats_.dirichlet_faces++;
                    } else if ((d == 2 && j == 0) || (d == 3 && j == N_ - 1)) { // Transverse lateral end walls (y = 0, y = 0.10 m)
                        bf.type = BoundaryType::NEUMANN;
                        stats_.neumann_faces++;
                    } else { // All other fluid-exposed faces: outer walls, channel floors, fin sides, fin tips
                        bf.type = BoundaryType::ROBIN;
                        bf.heat_transfer_coeff = props_.heat_transfer_coeff;
                        bf.ambient_temp = props_.ambient_temp;
                        stats_.robin_faces++;
                    }
                }
                boundary_faces_.push_back(bf);
            }
        }
    }

    stats_.internal_faces = internal_face_count;
    stats_.expected_nnz = stats_.active_cells + 2 * stats_.internal_faces;
    stats_.total_exposed_faces = boundary_faces_.size();

    // Verify boundary face conservation assertion: N_dir + N_rob + N_neu == 6M - 2E
    assert(stats_.dirichlet_faces + stats_.robin_faces + stats_.neumann_faces == stats_.total_exposed_faces);
    assert(stats_.total_exposed_faces == 6 * stats_.active_cells - 2 * stats_.internal_faces);
}

void ThermalGeometry3D::assemble_system(CSRMatrix& A, std::vector<double>& b, double* assembly_time_ms) const {
    auto t_start = std::chrono::high_resolution_clock::now();

    const std::size_t M = stats_.active_cells;
    SparseMatrixBuilder builder(M, M);
    builder.reserve(stats_.expected_nnz);
    b.assign(M, 0.0);

    const double k_cond = props_.thermal_conductivity;
    const double h_cell = h_;
    const double A_face = h_cell * h_cell;
    const double V_cell = h_cell * h_cell * h_cell;
    const double q_gen  = props_.volumetric_heat_gen;

    // Face conductances according to blueprint formulations:
    // Internal face conductance: a_int = k * A / h = k * h
    const double a_int = k_cond * h_cell;

    // Dirichlet half-cell conductance: a_dir = 2 * k * A / h = 2 * k * h
    const double a_dir = 2.0 * k_cond * h_cell;

    // Robin effective conductance: h_eff = 1 / (h / (2*k) + 1 / h_conv)
    const double h_eff = 1.0 / ((h_cell / (2.0 * k_cond)) + (1.0 / props_.heat_transfer_coeff));
    const double a_rob = h_eff * A_face;

    const int di[6] = {-1,  1,  0,  0,  0,  0};
    const int dj[6] = { 0,  0, -1,  1,  0,  0};
    const int dk[6] = { 0,  0,  0,  0, -1,  1};

    for (std::size_t u = 0; u < M; ++u) {
        int i = 0, j = 0, k = 0;
        get_grid_coords(u, i, j, k);

        double a_diag = 0.0;

        // Volumetric heat source contribution: q * V_cell
        b[u] += q_gen * V_cell;

        for (int d = 0; d < 6; ++d) {
            int ni = i + di[d];
            int nj = j + dj[d];
            int nk = k + dk[d];

            if (is_active(ni, nj, nk)) {
                // Internal neighbor: add off-diagonal -a_int and accumulate +a_int on diagonal
                int v = get_active_index(ni, nj, nk);
                builder.add_entry(u, static_cast<std::size_t>(v), -a_int);
                a_diag += a_int;
            } else {
                // Boundary face: evaluate boundary flux contribution
                if (type_ == ModelType::CUBE) {
                    if (d == 0) { // West face (x = 0) -> Dirichlet hot
                        a_diag += a_dir;
                        b[u]   += a_dir * props_.hot_temp;
                    } else if (d == 1) { // East face (x = 0.10 m) -> Dirichlet cold
                        a_diag += a_dir;
                        b[u]   += a_dir * props_.cold_temp;
                    } // Neumann faces contribute 0
                } else { // ModelType::FINNED_HEAT_SINK
                    if (d == 4 && k == 0) { // Baseplate bottom face (z = 0) -> Dirichlet hot
                        a_diag += a_dir;
                        b[u]   += a_dir * props_.hot_temp;
                    } else if ((d == 2 && j == 0) || (d == 3 && j == N_ - 1)) {
                        // Transverse lateral end walls (y = 0, y = 0.10 m) -> Insulated Neumann (flux = 0)
                    } else { // Robin convection on all other exposed faces
                        a_diag += a_rob;
                        b[u]   += a_rob * props_.ambient_temp;
                    }
                }
            }
        }

        // Add accumulated positive diagonal entry
        builder.add_entry(u, u, a_diag);
    }

    A = builder.build(true);

    auto t_end = std::chrono::high_resolution_clock::now();
    if (assembly_time_ms) {
        *assembly_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    }
}

double ThermalGeometry3D::compute_analytical_temperature_cube(double x) const noexcept {
    // T_exact(x) = 100.0 - 800.0*x + 500.0*x*(0.1 - x)
    return 100.0 - 800.0 * x + 500.0 * x * (0.1 - x);
}

double ThermalGeometry3D::compute_max_error_cube(const std::vector<double>& T_calc) const {
    if (type_ != ModelType::CUBE) {
        throw std::invalid_argument("compute_max_error_cube is only defined for ModelType::CUBE");
    }
    if (T_calc.size() != stats_.active_cells) {
        throw std::invalid_argument("Temperature vector size (" + std::to_string(T_calc.size()) + 
                                    ") != active cells (" + std::to_string(stats_.active_cells) + ")");
    }

    double max_err = 0.0;
    for (std::size_t u = 0; u < stats_.active_cells; ++u) {
        double x = 0.0, y = 0.0, z = 0.0;
        get_cell_centroid(u, x, y, z);
        double T_exact = compute_analytical_temperature_cube(x);
        double err = std::abs(T_calc[u] - T_exact);
        if (err > max_err) {
            max_err = err;
        }
    }
    return max_err;
}

bool ThermalGeometry3D::verify_energy_balance_finned(const std::vector<double>& T_calc,
                                                     double& Q_gen,
                                                     double& Q_base,
                                                     double& Q_conv,
                                                     double& rel_error) const {
    if (type_ != ModelType::FINNED_HEAT_SINK) {
        throw std::invalid_argument("verify_energy_balance_finned is only defined for ModelType::FINNED_HEAT_SINK");
    }
    if (T_calc.size() != stats_.active_cells) {
        throw std::invalid_argument("Temperature vector size (" + std::to_string(T_calc.size()) + 
                                    ") != active cells (" + std::to_string(stats_.active_cells) + ")");
    }

    // 1. Total volumetric heat generated in solid domain: Q_gen = q * V_solid
    Q_gen = props_.volumetric_heat_gen * stats_.solid_volume;

    // Conductances
    const double k_cond = props_.thermal_conductivity;
    const double h_cell = h_;
    const double A_face = h_cell * h_cell;
    const double a_dir = 2.0 * k_cond * h_cell;
    const double h_eff = 1.0 / ((h_cell / (2.0 * k_cond)) + (1.0 / props_.heat_transfer_coeff));
    const double a_rob = h_eff * A_face;

    Q_base = 0.0;
    Q_conv = 0.0;

    // 2. Sum outward heat fluxes across all boundary faces
    for (const auto& bf : boundary_faces_) {
        double T_P = T_calc[bf.cell_idx];

        if (bf.type == BoundaryType::DIRICHLET) {
            // Net outward conduction heat flux through baseplate contact (z = 0):
            // F_base = a_dir * (T_P - T_hot)
            Q_base += a_dir * (T_P - bf.prescribed_temp);
        } else if (bf.type == BoundaryType::ROBIN) {
            // Net outward convection heat flux to ambient air:
            // F_conv = a_rob * (T_P - T_inf)
            Q_conv += a_rob * (T_P - bf.ambient_temp);
        }
    }

    // Outward flux balance: Q_base + Q_conv should equal Q_gen
    double total_outward_flux = Q_base + Q_conv;
    rel_error = (Q_gen > 0.0) ? (std::abs(total_outward_flux - Q_gen) / Q_gen) : 0.0;

    // Assert relative error <= 0.1% (0.001)
    return (rel_error <= 0.001);
}

} // namespace workbench

