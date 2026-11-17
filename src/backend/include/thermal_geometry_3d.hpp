#pragma once

#include "sparse_matrix.hpp"
#include <string>
#include <vector>
#include <chrono>

namespace workbench {

/**
 * @brief Type of physical thermal model.
 */
enum class ModelType {
    CUBE,               ///< Model B: 3D continuum analytical cube benchmark
    FINNED_HEAT_SINK    ///< Model C: 3D non-convex finned heat sink solid
};

/**
 * @brief Classification of exposed control-volume boundary faces.
 */
enum class BoundaryType {
    NONE = 0,           ///< Internal face between two active solid cells
    DIRICHLET,          ///< Prescribed temperature (conductance = 2*k*A / dx)
    ROBIN,              ///< Fluid convection (conductance = h_eff * A)
    NEUMANN             ///< Adiabatic / insulated wall (flux = 0)
};

/**
 * @brief Explicit boundary face descriptor.
 */
struct BoundaryFace {
    std::size_t cell_idx = 0;           ///< Linear active cell index [0, M - 1]
    int direction = 0;                  ///< 0: -x, 1: +x, 2: -y, 3: +y, 4: -z, 5: +z
    BoundaryType type = BoundaryType::NONE;
    double prescribed_temp = 0.0;       ///< T_bc for Dirichlet (deg C)
    double heat_transfer_coeff = 0.0;   ///< h for Robin (W/(m^2*K))
    double ambient_temp = 0.0;          ///< T_inf for Robin (deg C)
};

/**
 * @brief Structural and topological metadata for a discretized thermal model.
 */
struct GeometryStats {
    ModelType model_type = ModelType::CUBE;
    int N = 10;                         ///< Grid resolution per axis
    double cell_size = 0.0;             ///< Cell width dx = dy = dz = h (m)
    std::size_t total_cells = 0;        ///< Total bounding cells N^3
    std::size_t active_cells = 0;       ///< Unknown DOFs M (active solid cells)
    std::size_t internal_faces = 0;     ///< Shared solid-solid face pairs E
    std::size_t expected_nnz = 0;       ///< M + 2E
    std::size_t dirichlet_faces = 0;    ///< Count of exposed Dirichlet faces
    std::size_t robin_faces = 0;        ///< Count of exposed Robin convective faces
    std::size_t neumann_faces = 0;      ///< Count of exposed insulated Neumann faces
    std::size_t total_exposed_faces = 0;///< 6M - 2E
    double solid_volume = 0.0;          ///< Total solid volume M * h^3 (m^3)
};

/**
 * @brief Physical and thermal material property parameters.
 */
struct ThermalProperties {
    double thermal_conductivity = 10.0;     ///< k in W/(m*K) (default: 10.0)
    double volumetric_heat_gen = 10000.0;   ///< q in W/m^3 (default: 10,000.0)
    double ambient_temp = 25.0;             ///< T_inf in deg C (default: 25.0)
    double heat_transfer_coeff = 50.0;      ///< h in W/(m^2*K) (default: 50.0)
    double hot_temp = 100.0;                ///< T_hot in deg C (Model B x=0, Model C z=0)
    double cold_temp = 20.0;                ///< T_cold in deg C (Model B x=0.1)
};

/**
 * @brief Programmatic 3D Geometry and Cell-Centred Finite Volume Method (FVM) Assembler.
 * 
 * Supports both Model B (Continuum Cube) and Model C (Finned Heat Sink Solid)
 * at resolutions N = 10, 20, 30 with exact active-cell indexing, MECE boundary classification,
 * symmetric positive definite CSR matrix assembly, and energy balance verification.
 */
class ThermalGeometry3D {
public:
    /**
     * @brief Constructs and discretizes the specified 3D continuum model.
     * @param type ModelType::CUBE or ModelType::FINNED_HEAT_SINK
     * @param N Grid resolution per axis (N in {10, 20, 30})
     * @param props Thermal physical parameters
     */
    ThermalGeometry3D(ModelType type, int N, ThermalProperties props = ThermalProperties());

    [[nodiscard]] ModelType get_model_type() const noexcept { return type_; }
    [[nodiscard]] int get_N() const noexcept { return N_; }
    [[nodiscard]] double get_cell_size() const noexcept { return h_; }
    [[nodiscard]] const GeometryStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] const ThermalProperties& get_properties() const noexcept { return props_; }
    [[nodiscard]] const std::vector<BoundaryFace>& get_boundary_faces() const noexcept { return boundary_faces_; }

    // Coordinate mapping methods
    [[nodiscard]] bool is_active(int i, int j, int k) const noexcept;
    [[nodiscard]] int get_active_index(int i, int j, int k) const noexcept;
    void get_grid_coords(std::size_t active_idx, int& i, int& j, int& k) const;
    void get_cell_centroid(std::size_t active_idx, double& x, double& y, double& z) const;

    /**
     * @brief Assembles the cell-centred FVM discrete linear system A * T = b.
     * @param A Output symmetric positive definite CSR sparse matrix
     * @param b Output right-hand side source vector
     * @param assembly_time_ms Optional pointer to store assembly execution time in ms
     */
    void assemble_system(CSRMatrix& A, std::vector<double>& b, double* assembly_time_ms = nullptr) const;

    // Verification helpers
    [[nodiscard]] double compute_analytical_temperature_cube(double x) const noexcept;
    [[nodiscard]] double compute_max_error_cube(const std::vector<double>& T_calc) const;
    [[nodiscard]] bool verify_energy_balance_finned(const std::vector<double>& T_calc,
                                                    double& Q_gen,
                                                    double& Q_base,
                                                    double& Q_conv,
                                                    double& rel_error) const;

private:
    ModelType type_;
    int N_;
    double h_;
    ThermalProperties props_;
    GeometryStats stats_;

    std::vector<int> grid_to_active_;          ///< Size N^3, maps (i, j, k) -> active_idx or -1
    std::vector<std::size_t> active_to_grid_;  ///< Size M, maps active_idx -> flat grid id
    std::vector<BoundaryFace> boundary_faces_; ///< All exposed control-volume faces

    [[nodiscard]] std::size_t flat_grid_id(int i, int j, int k) const noexcept {
        return static_cast<std::size_t>((i * N_ + j) * N_ + k);
    }

    void build_mesh();
};

} // namespace workbench

