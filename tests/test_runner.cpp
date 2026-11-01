#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include "stl_parser.hpp"
#include "ansys_parser.hpp"
#include "thermal_fea_1d.hpp"
#include "multicore_bench.hpp"
#include "embedded_hardware.hpp"
#include "backprop_engine.hpp"
#include <atomic>

using namespace workbench;

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[RUN] " << #fn << "...\n"; \
        int prev_failed = g_tests_failed; \
        fn(); \
        if (g_tests_failed == prev_failed) { \
            std::cout << "  [PASS] " << #fn << "\n"; \
            g_tests_passed++; \
        } \
    } while(0)

// -------------------------------------------------------------
// 1. STL Parser Tests (Positive & Negative)
// -------------------------------------------------------------
void test_stl_parser_positive() {
    std::string valid_stl = 
        "solid test_triangle\n"
        "  facet normal 0.0 0.0 1.0\n"
        "    outer loop\n"
        "      vertex 0.0 0.0 0.0\n"
        "      vertex 1.0 0.0 0.0\n"
        "      vertex 0.0 1.0 0.0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid test_triangle\n";

    StlParseResult res = StlParser::parse_string(valid_stl);
    TEST_ASSERT(res.success, "Valid STL must parse successfully: " + res.error_message);
    TEST_ASSERT(res.facet_count == 1, "Facet count must be 1");
    TEST_ASSERT(std::abs(res.surface_area - 0.5) < 1e-9, "Surface area of (0,0)-(1,0)-(0,1) must be 0.5");
    TEST_ASSERT(res.min_bound.x == 0.0 && res.max_bound.x == 1.0, "Bounding box min/max X correct");
    TEST_ASSERT(res.solid_name == "test_triangle", "Solid name parsed correctly");
}

void test_stl_parser_empty_file() {
    StlParseResult res = StlParser::parse_string("");
    TEST_ASSERT(!res.success, "Empty STL must be rejected");
    TEST_ASSERT(res.error_message.find("empty") != std::string::npos, "Error message must state empty file");
}

void test_stl_parser_nan_coords() {
    std::string nan_stl = 
        "solid nan_solid\n"
        "  facet normal 0.0 0.0 1.0\n"
        "    outer loop\n"
        "      vertex NaN 0.0 0.0\n"
        "      vertex 1.0 0.0 0.0\n"
        "      vertex 0.0 1.0 0.0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid nan_solid\n";

    StlParseResult res = StlParser::parse_string(nan_stl);
    TEST_ASSERT(!res.success, "STL with NaN coordinate must be rejected");
    TEST_ASSERT(res.error_message.find("NaN") != std::string::npos, "Error message must mention NaN");
}

void test_stl_parser_malformed_syntax() {
    std::string bad_syntax = 
        "solid broken\n"
        "  facet normal 0.0 0.0 1.0\n"
        "    vertex 0.0 0.0 0.0\n"
        "endsolid broken\n";

    StlParseResult res = StlParser::parse_string(bad_syntax);
    TEST_ASSERT(!res.success, "Malformed syntax (missing outer loop) must be rejected");
}

// -------------------------------------------------------------
// 2. ANSYS CSV Parser Tests (Positive & Negative)
// -------------------------------------------------------------
void test_ansys_parser_positive() {
    std::string valid_csv = 
        "# Synthetic ANSYS Nodal Temperatures\n"
        "node_id,x_m,y_m,z_m,temperature_C\n"
        "1,0.00,0.00,0.00,100.0\n"
        "2,0.10,0.00,0.00,137.0\n"
        "3,0.20,0.00,0.00,164.0\n";

    AnsysParseResult res = AnsysParser::parse_string(valid_csv);
    TEST_ASSERT(res.success, "Valid ANSYS CSV must parse successfully: " + res.error_message);
    TEST_ASSERT(res.node_count == 3, "Node count must be 3");
    TEST_ASSERT(res.min_temp_C == 100.0 && res.max_temp_C == 164.0, "Min/max temp correct");
    TEST_ASSERT(res.is_synthetic, "Synthetic header must be recognized");
}

void test_ansys_parser_empty_file() {
    AnsysParseResult res = AnsysParser::parse_string("");
    TEST_ASSERT(!res.success, "Empty CSV must be rejected");
    TEST_ASSERT(res.error_message.find("empty") != std::string::npos, "Error message must mention empty");
}

void test_ansys_parser_missing_header() {
    std::string bad_header = 
        "id,x,y,z,temp\n"
        "1,0,0,0,100\n";

    AnsysParseResult res = AnsysParser::parse_string(bad_header);
    TEST_ASSERT(!res.success, "CSV with incorrect header must be rejected");
    TEST_ASSERT(res.error_message.find("Invalid CSV header") != std::string::npos, "Actionable header error message");
}

void test_ansys_parser_below_absolute_zero() {
    std::string sub_zero = 
        "node_id,x_m,y_m,z_m,temperature_C\n"
        "1,0.00,0.00,0.00,-300.0\n";

    AnsysParseResult res = AnsysParser::parse_string(sub_zero);
    TEST_ASSERT(!res.success, "Temperature below absolute zero (-300 C) must be rejected");
    TEST_ASSERT(res.error_message.find("absolute zero") != std::string::npos, "Error mentions absolute zero");
}

// -------------------------------------------------------------
// 3. 1D Thermal FEA Numerical Tests
// -------------------------------------------------------------
void test_thermal_fea_analytical_and_convergence() {
    ThermalParams1D p;
    p.length_L = 1.0;
    p.area_A = 1e-4;
    p.conductivity_k = 10.0;
    p.heat_gen_q = 10000.0;
    p.temp_T0 = 100.0;
    p.temp_TL = 20.0;
    p.num_elements_N = 10;

    ThermalSolveResult1D sol = ThermalFEMSolver1D::solve(p);
    
    // Boundary conditions
    TEST_ASSERT(std::abs(sol.node_temp_fea.front() - 100.0) < 1e-12, "T(0) == 100 C exact");
    TEST_ASSERT(std::abs(sol.node_temp_fea.back() - 20.0) < 1e-12, "T(L) == 20 C exact");

    // Nodal superconvergence: Nodal error must be near machine epsilon
    TEST_ASSERT(sol.max_nodal_error < 1e-10, "1D linear FEA with uniform heat generation is nodally exact (< 1e-10 C)");

    // Midpoint error theoretical check: q * h^2 / (8 * k)
    // For N = 10, h = 0.1, error = 10000 * 0.01 / 80 = 1.25 C
    double expected_mid_err = (10000.0 * 0.01) / (8.0 * 10.0);
    TEST_ASSERT(std::abs(sol.max_midpoint_error - expected_mid_err) < 1e-10, 
                "Midpoint error for N=10 must match q*h^2/(8*k) = 1.25 C");

    // Convergence study for N = 10, 20, 40, 80
    ConvergenceStudyResult study = ThermalFEMSolver1D::run_convergence_study(p);
    TEST_ASSERT(study.rows.size() == 4, "Study must contain 4 mesh levels");

    // Check O(h^2) rate: ratio between consecutive meshes must be approx 4.0
    for (size_t i = 1; i < study.rows.size(); ++i) {
        double ratio = study.rows[i].error_reduction_ratio;
        double rate = study.rows[i].observed_convergence_rate;
        TEST_ASSERT(std::abs(ratio - 4.0) < 1e-4, "Error reduction ratio must be 4.0 when doubling mesh count");
        TEST_ASSERT(std::abs(rate - 2.0) < 1e-4, "Observed convergence rate must be 2.0 (second-order)");
    }
}

// -------------------------------------------------------------
// 4. Multicore Batch Execution & Agreement Tests
// -------------------------------------------------------------
void test_multicore_bench_agreement() {
    BenchmarkConfig cfg;
    cfg.seed = 42;
    cfg.case_count = 50; // fast unit test
    cfg.mesh_size_N = 20;
    cfg.repetitions = 2;
    cfg.worker_counts = {1, 2};

    BenchmarkSummary summary = MulticoreBenchmarker::run_benchmark(cfg);
    TEST_ASSERT(summary.numerical_agreement_passed, 
                "Serial and parallel solutions must agree within 1e-8 C");
    TEST_ASSERT(summary.max_absolute_temp_diff_C <= 1e-8, 
                "Max temp diff <= 1e-8 C");
    TEST_ASSERT(summary.worker_results.size() >= 1, "Must have worker timing results");
}

// -------------------------------------------------------------
// 5. Embedded Hardware Mock & Recovery Tests
// -------------------------------------------------------------
void test_embedded_hardware_lifecycle_and_fault_recovery() {
    EmbeddedHardwareManager mgr;
    auto devices = mgr.discover_devices();
    TEST_ASSERT(devices.size() == 3, "Must discover exactly 3 devices (MCU, FPGA, JTAG)");

    // Test MCU Connection
    bool conn_ok = mgr.connect_device("mcu_stm32h7");
    TEST_ASSERT(conn_ok, "MCU connection must succeed");

    TelemetryPacket pkt = mgr.read_device_telemetry("mcu_stm32h7");
    TEST_ASSERT(pkt.state == DeviceConnectionState::STREAMING_TELEMETRY, "MCU state must be STREAMING_TELEMETRY");
    TEST_ASSERT(pkt.mcu_vcc_volts > 3.2 && pkt.mcu_vcc_volts < 3.4, "MCU VCC nominal around 3.3V");

    // Inject Fault: Communication Timeout
    mgr.inject_device_fault("mcu_stm32h7", SimulatedFaultType::COMMUNICATION_TIMEOUT);
    TelemetryPacket faulted_pkt = mgr.read_device_telemetry("mcu_stm32h7");
    TEST_ASSERT(faulted_pkt.state == DeviceConnectionState::ERROR_FAULTED, "MCU state must be ERROR_FAULTED");
    TEST_ASSERT(faulted_pkt.status_message.find("timeout") != std::string::npos, "Fault message indicates timeout");

    // Recover Fault
    bool rec_ok = mgr.recover_device_fault("mcu_stm32h7");
    TEST_ASSERT(rec_ok, "Recovery must succeed");
    TelemetryPacket recovered_pkt = mgr.read_device_telemetry("mcu_stm32h7");
    TEST_ASSERT(recovered_pkt.state == DeviceConnectionState::STREAMING_TELEMETRY, "MCU state restored to nominal");
}

// -------------------------------------------------------------
// 6. Simple Handwritten Backpropagation & Gradient Check Tests
// -------------------------------------------------------------
void test_backpropagation_gradient_check() {
    BackpropSummary bp = BackpropEngine::run_full_study();

    // Verify initial forward pass at x=1, target y=2, w=0.5, b=0, v=1, c=0
    // z = 0.5, h = tanh(0.5) = 0.462117157, y_hat = 0.462117157, diff = -1.53788284, loss = 1.1825418
    TEST_ASSERT(std::abs(bp.step_0.forward.z - 0.5) < 1e-12, "z == 0.5");
    TEST_ASSERT(std::abs(bp.step_0.forward.h - std::tanh(0.5)) < 1e-12, "h == tanh(0.5)");
    TEST_ASSERT(std::abs(bp.step_0.forward.y_hat - std::tanh(0.5)) < 1e-12, "y_hat == tanh(0.5)");
    TEST_ASSERT(std::abs(bp.step_0.forward.loss_J - 1.182541818) < 1e-6, "loss_J == 1.182542");

    // Verify analytical gradients
    // dJ/dc = -1.5378828
    // dJ/dv = -1.5378828 * 0.462117 = -0.710682
    // dJ/dw = -1.5378828 * 1 * (1 - 0.462117^2) * 1 = -1.2094645
    // dJ/db = -1.2094645
    TEST_ASSERT(std::abs(bp.step_0.gradients.dJ_dc - (-1.53788284)) < 1e-5, "dJ_dc correct");
    TEST_ASSERT(std::abs(bp.step_0.gradients.dJ_dv - (-0.71068205)) < 1e-5, "dJ_dv correct");
    TEST_ASSERT(std::abs(bp.step_0.gradients.dJ_dw - (-1.20946452)) < 1e-5, "dJ_dw correct");
    TEST_ASSERT(std::abs(bp.step_0.gradients.dJ_db - (-1.20946452)) < 1e-5, "dJ_db correct");

    // Verify finite difference gradient check passed (tolerance <= 1e-5 with epsilon = 1e-6)
    TEST_ASSERT(bp.all_gradient_checks_passed, "All finite difference gradient checks must pass <= 1e-5");

    // Verify 100 training steps: loss must strictly decrease
    TEST_ASSERT(bp.history_100_steps.size() == 101, "History has 101 records (step 0 to 100)");
    double initial_loss = bp.history_100_steps.front().forward.loss_J;
    double final_loss = bp.history_100_steps.back().forward.loss_J;
    TEST_ASSERT(final_loss < initial_loss, "Training must reduce loss");
    TEST_ASSERT(final_loss < 0.01, "Loss after 100 steps must converge close to 0 (< 0.01)");
}

// -------------------------------------------------------------
// 7. Disk Samples & Provenance Verification Tests
// -------------------------------------------------------------
void test_disk_files_sample_bracket_and_ansys() {
    // Parse the actual sample_bracket.stl from disk
    StlParseResult stl_res = StlParser::parse_file("data/sample_bracket.stl");
    TEST_ASSERT(stl_res.success, "sample_bracket.stl must parse successfully: " + stl_res.error_message);
    TEST_ASSERT(stl_res.facet_count == 32, "sample_bracket.stl has exactly 32 facets");
    TEST_ASSERT(std::abs(stl_res.surface_area - 0.046297) < 1e-4, "Surface area must match 0.0463 m^2");
    TEST_ASSERT(std::abs(stl_res.max_bound.x - 0.10) < 1e-3, "Max X bound is 0.10 m (100 mm)");
    TEST_ASSERT(std::abs(stl_res.max_bound.y - 0.08) < 1e-3, "Max Y bound is 0.08 m (80 mm)");
    TEST_ASSERT(std::abs(stl_res.max_bound.z - 0.10) < 1e-3, "Max Z bound is 0.10 m (100 mm)");

    // Parse the actual ansys_thermal_synthetic.csv from disk
    AnsysParseResult ansys_res = AnsysParser::parse_file("data/ansys_thermal_synthetic.csv");
    TEST_ASSERT(ansys_res.success, "ansys_thermal_synthetic.csv must parse successfully: " + ansys_res.error_message);
    TEST_ASSERT(ansys_res.node_count == 3485, "Must contain exactly 3,485 nodes");
    TEST_ASSERT(std::abs(ansys_res.min_temp_C - 36.87) < 0.1, "Min temp must be approx 36.87 C");
    TEST_ASSERT(std::abs(ansys_res.max_temp_C - 104.0) < 0.1, "Max temp must be approx 104.0 C");
    TEST_ASSERT(ansys_res.is_synthetic, "Synthetic flag must be true");
}

void test_disk_test_samples() {
    // Test negative STL specimens on disk
    TEST_ASSERT(!StlParser::parse_file("data/test_samples/empty.stl").success, "empty.stl must be rejected");
    TEST_ASSERT(!StlParser::parse_file("data/test_samples/corrupted_syntax.stl").success, "corrupted_syntax.stl must be rejected");
    TEST_ASSERT(!StlParser::parse_file("data/test_samples/nan_coords.stl").success, "nan_coords.stl must be rejected");

    // Test negative CSV specimens on disk
    TEST_ASSERT(!AnsysParser::parse_file("data/test_samples/empty.csv").success, "empty.csv must be rejected");
    TEST_ASSERT(!AnsysParser::parse_file("data/test_samples/missing_columns.csv").success, "missing_columns.csv must be rejected");
    TEST_ASSERT(!AnsysParser::parse_file("data/test_samples/sub_absolute_zero.csv").success, "sub_absolute_zero.csv must be rejected");
}

// -------------------------------------------------------------
// 8. FEA Edge Cases: N=1 and N=2 Stability
// -------------------------------------------------------------
void test_thermal_fea_edge_cases_N1_N2() {
    ThermalParams1D p;
    p.length_L = 1.0;
    p.area_A = 1e-4;
    p.conductivity_k = 10.0;
    p.heat_gen_q = 10000.0;
    p.temp_T0 = 100.0;
    p.temp_TL = 20.0;

    // N = 1 (Single element, no interior nodes)
    p.num_elements_N = 1;
    ThermalSolveResult1D sol_n1 = ThermalFEMSolver1D::solve(p);
    TEST_ASSERT(sol_n1.node_temp_fea.size() == 2, "N=1 has 2 nodes");
    TEST_ASSERT(std::abs(sol_n1.node_temp_fea[0] - 100.0) < 1e-12, "N=1 T0 == 100 C");
    TEST_ASSERT(std::abs(sol_n1.node_temp_fea[1] - 20.0) < 1e-12, "N=1 TL == 20 C");

    // N = 2 (Two elements, exactly 1 interior node at x = 0.5)
    p.num_elements_N = 2;
    ThermalSolveResult1D sol_n2 = ThermalFEMSolver1D::solve(p);
    TEST_ASSERT(sol_n2.node_temp_fea.size() == 3, "N=2 has 3 nodes");
    // Analytical solution at x = 0.5: T(0.5) = 100 - 40 + [10000/(20)] * 0.25 = 60 + 125 = 185.0 C
    TEST_ASSERT(std::abs(sol_n2.node_temp_fea[1] - 185.0) < 1e-10, "N=2 interior node must be exactly 185.0 C");
    // Midpoint error for N = 2, h = 0.5: q*h^2/(8*k) = 10000 * 0.25 / 80 = 31.25 C
    TEST_ASSERT(std::abs(sol_n2.max_midpoint_error - 31.25) < 1e-10, "N=2 midpoint error must be exactly 31.25 C");
}

// -------------------------------------------------------------
// 9. Multicore Benchmark Cooperative Cancellation
// -------------------------------------------------------------
void test_multicore_bench_cancellation() {
    BenchmarkConfig cfg;
    cfg.seed = 42;
    cfg.case_count = 200;
    cfg.mesh_size_N = 50;
    cfg.repetitions = 5;
    cfg.worker_counts = {1, 2, 4};

    std::atomic<bool> cancel_flag{true}; // Cancel immediately
    BenchmarkSummary summary = MulticoreBenchmarker::run_benchmark(cfg, &cancel_flag);
    TEST_ASSERT(summary.was_cancelled, "Benchmark must acknowledge cancellation flag");
}

// -------------------------------------------------------------
// 10. Thermal FEA Input Validation & Safety
// -------------------------------------------------------------
void test_thermal_fea_input_validation() {
    ThermalParams1D p;
    p.num_elements_N = 0; // Invalid N=0
    bool caught_n0 = false;
    try {
        ThermalFEMSolver1D::solve(p);
    } catch (const std::invalid_argument&) {
        caught_n0 = true;
    }
    TEST_ASSERT(caught_n0, "N=0 must throw std::invalid_argument");

    p.num_elements_N = 10;
    p.conductivity_k = -5.0; // Negative k
    bool caught_neg_k = false;
    try {
        ThermalFEMSolver1D::solve(p);
    } catch (const std::invalid_argument&) {
        caught_neg_k = true;
    }
    TEST_ASSERT(caught_neg_k, "k <= 0 must throw std::invalid_argument");

    p.conductivity_k = 10.0;
    p.length_L = -1.0; // Negative L
    bool caught_neg_L = false;
    try {
        ThermalFEMSolver1D::solve(p);
    } catch (const std::invalid_argument&) {
        caught_neg_L = true;
    }
    TEST_ASSERT(caught_neg_L, "L <= 0 must throw std::invalid_argument");
}

void test_parser_edge_cases() {
    // 1. Duplicate node ID check in ANSYS CSV
    std::string dup_csv = "node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,50\n1,0.01,0,0,60\n";
    auto r1 = AnsysParser::parse_string(dup_csv);
    TEST_ASSERT(!r1.success, "Duplicate node_id must be rejected");
    TEST_ASSERT(r1.error_message.find("Duplicate node_id") != std::string::npos, "Error must mention duplicate node_id");

    // 2. Fractional node ID check in ANSYS CSV
    std::string frac_csv = "node_id,x_m,y_m,z_m,temperature_C\n1.5,0,0,0,50\n";
    auto r2 = AnsysParser::parse_string(frac_csv);
    TEST_ASSERT(!r2.success, "Fractional node_id 1.5 must be rejected");
    TEST_ASSERT(r2.error_message.find("must be an integer") != std::string::npos, "Error must indicate non-integer node_id");

    // 3. Default non-synthetic check
    std::string plain_csv = "node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,50\n2,0.01,0,0,60\n";
    auto r3 = AnsysParser::parse_string(plain_csv);
    TEST_ASSERT(r3.success, "Plain CSV must parse successfully");
    TEST_ASSERT(!r3.is_synthetic, "is_synthetic must default to false");

    // 4. Extreme STL coordinate check (> 1e12 m)
    std::string extreme_stl = 
        "solid extreme_test\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1e308 0 0\n"
        "      vertex 0 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid extreme_test\n";
    auto r4 = StlParser::parse_string(extreme_stl);
    TEST_ASSERT(!r4.success, "Extreme coordinate 1e308 must be rejected by parser bounds check");
    TEST_ASSERT(r4.error_message.find("exceeds physical range") != std::string::npos, "Error message must report physical range rejection");
}

// -------------------------------------------------------------
// Test Runner Main
// -------------------------------------------------------------
int main() {
    std::cout << "=================================================================\n";
    std::cout << " UNIFIED ENGINEERING WORKBENCH - C++ VERIFICATION TEST SUITE     \n";
    std::cout << " Positive, Negative, Disk File, and Numerical Robustness Tests   \n";
    std::cout << "=================================================================\n";

    // Task B Tests
    RUN_TEST(test_stl_parser_positive);
    RUN_TEST(test_stl_parser_empty_file);
    RUN_TEST(test_stl_parser_nan_coords);
    RUN_TEST(test_stl_parser_malformed_syntax);
    RUN_TEST(test_ansys_parser_positive);
    RUN_TEST(test_ansys_parser_empty_file);
    RUN_TEST(test_ansys_parser_missing_header);
    RUN_TEST(test_ansys_parser_below_absolute_zero);
    RUN_TEST(test_disk_files_sample_bracket_and_ansys);
    RUN_TEST(test_disk_test_samples);
    RUN_TEST(test_parser_edge_cases);

    // Task C Tests
    RUN_TEST(test_thermal_fea_analytical_and_convergence);
    RUN_TEST(test_thermal_fea_edge_cases_N1_N2);
    RUN_TEST(test_thermal_fea_input_validation);

    // Task D Tests
    RUN_TEST(test_multicore_bench_agreement);
    RUN_TEST(test_multicore_bench_cancellation);

    // Task E Tests
    RUN_TEST(test_embedded_hardware_lifecycle_and_fault_recovery);

    // Task F Tests
    RUN_TEST(test_backpropagation_gradient_check);

    std::cout << "=================================================================\n";
    std::cout << " Test Summary: " << g_tests_passed << " Passed, " << g_tests_failed << " Failed\n";
    std::cout << "=================================================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
