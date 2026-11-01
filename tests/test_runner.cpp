#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include "stl_parser.hpp"
#include "ansys_parser.hpp"
#include "thermal_fea_1d.hpp"
#include "multicore_bench.hpp"
#include "embedded_hardware.hpp"
#include "backprop_engine.hpp"
#include "http_server.hpp"

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
// Helper HTTP clients for test suite
// -------------------------------------------------------------
static std::string http_get_sync(const std::string& host, int port, const std::string& path) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return "";
    }
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\nConnection: close\r\n\r\n";
    send(s, req.data(), static_cast<int>(req.size()), 0);
    std::string resp;
    char buf[4096];
    int r = 0;
    while ((r = recv(s, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, r);
    }
    closesocket(s);
    return resp;
}

static std::string http_post_sync(const std::string& host, int port, const std::string& path, const std::string& body, const std::string& extra_headers = "") {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return "";
    }
    std::string req = "POST " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\n" +
                      "Origin: http://127.0.0.1:" + std::to_string(port) + "\r\n" +
                      "Content-Length: " + std::to_string(body.size()) + "\r\n" +
                      extra_headers +
                      "Connection: close\r\n\r\n" + body;
    send(s, req.data(), static_cast<int>(req.size()), 0);
    std::string resp;
    char buf[4096];
    int r = 0;
    while ((r = recv(s, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, r);
    }
    closesocket(s);
    return resp;
}

static bool contains_inf_or_nan_token(const std::string& str) {
    if (str.find(": nan") != std::string::npos || str.find(":nan") != std::string::npos ||
        str.find(": inf") != std::string::npos || str.find(":inf") != std::string::npos ||
        str.find(":-inf") != std::string::npos || str.find(": -inf") != std::string::npos ||
        str.find("[nan") != std::string::npos || str.find(",nan") != std::string::npos ||
        str.find("[inf") != std::string::npos || str.find(",inf") != std::string::npos ||
        str.find("Infinity") != std::string::npos || str.find("NaN") != std::string::npos) {
        return true;
    }
    return false;
}

// -------------------------------------------------------------
// New Item 5 Tests: Parameter range rejection, 409 guard, cancel, JSON sanitizer, dup/1e300
// -------------------------------------------------------------
void test_parameter_range_rejection() {
    int test_port = 9881;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for parameter rejection tests");
    int port = server.get_actual_port();

    // 1. FEA numeric range rejection
    // L range: (0, 1000] -> L <= 0 or L > 1000 rejected
    std::string r1 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=-5&A=0.01&k=50&q=1000&T0=20&TL=100&N=10");
    TEST_ASSERT(r1.find("400 Bad Request") != std::string::npos, "L=-5 must return 400");
    std::string r2 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1500&A=0.01&k=50&q=1000&T0=20&TL=100&N=10");
    TEST_ASSERT(r2.find("400 Bad Request") != std::string::npos, "L=1500 must return 400");

    // A range: (0, 100] -> A <= 0 or A > 100 rejected
    std::string r3 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=0&k=50&q=1000&T0=20&TL=100&N=10");
    TEST_ASSERT(r3.find("400 Bad Request") != std::string::npos, "A=0 must return 400");
    std::string r4 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=150&k=50&q=1000&T0=20&TL=100&N=10");
    TEST_ASSERT(r4.find("400 Bad Request") != std::string::npos, "A=150 must return 400");

    // k range: (0, 100000] -> k <= 0 or k > 100000 rejected
    std::string r5 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=0.01&k=0&q=1000&T0=20&TL=100&N=10");
    TEST_ASSERT(r5.find("400 Bad Request") != std::string::npos, "k=0 must return 400");
    std::string r6 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=0.01&k=150000&q=1000&T0=20&TL=100&N=10");
    TEST_ASSERT(r6.find("400 Bad Request") != std::string::npos, "k=150000 must return 400");

    // T0, TL range: [-273.15, 1e6] -> T < -273.15 or T > 1e6 rejected
    std::string r7 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=0.01&k=50&q=1000&T0=-300&TL=100&N=10");
    TEST_ASSERT(r7.find("400 Bad Request") != std::string::npos, "T0=-300 must return 400");
    std::string r8 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=0.01&k=50&q=1000&T0=20&TL=2000000&N=10");
    TEST_ASSERT(r8.find("400 Bad Request") != std::string::npos, "TL=2000000 must return 400");

    // N range: [1, 100000] -> N=0 or N > 100000 rejected
    std::string r9 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=0.01&k=50&q=1000&T0=20&TL=100&N=0");
    TEST_ASSERT(r9.find("400 Bad Request") != std::string::npos, "N=0 must return 400");
    std::string r10 = http_get_sync("127.0.0.1", port, "/api/fea/solve?L=1&A=0.01&k=50&q=1000&T0=20&TL=100&N=100001");
    TEST_ASSERT(r10.find("400 Bad Request") != std::string::npos, "N=100001 must return 400");

    // Convergence study parameter rejection
    std::string r_conv = http_get_sync("127.0.0.1", port, "/api/fea/convergence?L=-1&A=0.01&k=50&q=1000&T0=20&TL=100");
    TEST_ASSERT(r_conv.find("400 Bad Request") != std::string::npos, "Convergence with L=-1 must return 400");

    // 2. Performance benchmark numeric range rejection
    // cases: 1..200000
    std::string r_c0 = http_get_sync("127.0.0.1", port, "/api/performance/run?cases=0&mesh=10&seed=1");
    TEST_ASSERT(r_c0.find("400 Bad Request") != std::string::npos, "cases=0 must return 400");
    std::string r_c_high = http_get_sync("127.0.0.1", port, "/api/performance/run?cases=200001&mesh=10&seed=1");
    TEST_ASSERT(r_c_high.find("400 Bad Request") != std::string::npos, "cases=200001 must return 400");

    // mesh: 1..5000
    std::string r_m0 = http_get_sync("127.0.0.1", port, "/api/performance/run?cases=10&mesh=0&seed=1");
    TEST_ASSERT(r_m0.find("400 Bad Request") != std::string::npos, "mesh=0 must return 400");
    std::string r_m_high = http_get_sync("127.0.0.1", port, "/api/performance/run?cases=10&mesh=5001&seed=1");
    TEST_ASSERT(r_m_high.find("400 Bad Request") != std::string::npos, "mesh=5001 must return 400");

    // non-numeric parameters
    std::string r_non_num = http_get_sync("127.0.0.1", port, "/api/performance/run?cases=abc&mesh=10&seed=1");
    TEST_ASSERT(r_non_num.find("400 Bad Request") != std::string::npos, "cases=abc must return 400");

    server.stop();
}

void test_benchmark_concurrency_guard_409() {
    int test_port = 9882;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for concurrency test");
    int port = server.get_actual_port();

    std::atomic<bool> t1_done{false};
    std::thread t1([&]() {
        http_get_sync("127.0.0.1", port, "/api/performance/run?cases=100000&mesh=400&seed=42");
        t1_done = true;
    });

    // Wait until benchmark enters running state
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::string prog = http_get_sync("127.0.0.1", port, "/api/performance/progress");
        if (prog.find("\"running\":true") != std::string::npos) break;
    }

    // Attempt second concurrent request
    std::string resp2 = http_get_sync("127.0.0.1", port, "/api/performance/run?cases=10&mesh=10&seed=1");
    TEST_ASSERT(resp2.find("409 Conflict") != std::string::npos, "Concurrent benchmark must return 409 Conflict");
    TEST_ASSERT(resp2.find("A benchmark is already running") != std::string::npos, "409 body must state already running");

    // Cancel running benchmark
    std::string cancel_resp = http_post_sync("127.0.0.1", port, "/api/performance/cancel", "");
    TEST_ASSERT(cancel_resp.find("\"success\":true") != std::string::npos, "Cancel must return success: true");

    t1.join();
    server.stop();
}

void test_benchmark_cancel_mid_run() {
    BenchmarkConfig cfg;
    cfg.case_count = 50000;
    cfg.mesh_size_N = 200;
    cfg.repetitions = 5;
    std::atomic<bool> cancel_flag{false};

    std::thread cancel_thread([&]() {
        for (int i = 0; i < 500; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            auto prog = MulticoreBenchmarker::get_progress();
            if (prog.running && prog.step > 0) {
                break;
            }
        }
        cancel_flag = true;
    });

    BenchmarkSummary res = MulticoreBenchmarker::run_benchmark(cfg, &cancel_flag);
    cancel_thread.join();

    TEST_ASSERT(res.was_cancelled, "Benchmark must indicate was_cancelled == true");
    auto prog = MulticoreBenchmarker::get_progress();
    TEST_ASSERT(!prog.running, "Progress running flag must be false after completion");
}

void test_json_sanitizer() {
    // 1. STL parser on failed parse
    StlParseResult stl_fail = StlParser::parse_string("corrupted non-stl content");
    std::string stl_j = stl_fail.to_json();
    TEST_ASSERT(stl_j.find("\"surface_area\":null") != std::string::npos, "Failed STL must emit surface_area: null");
    TEST_ASSERT(stl_j.find("\"bounds\":null") != std::string::npos, "Failed STL must emit bounds: null");
    TEST_ASSERT(!contains_inf_or_nan_token(stl_j), "STL JSON must never contain nan or inf");
    TEST_ASSERT(stl_j.find("1e30") == std::string::npos && stl_j.find("1e+30") == std::string::npos, "STL JSON must never contain 1e30 sentinels");

    // 2. ANSYS parser on failed parse
    AnsysParseResult ans_fail = AnsysParser::parse_string("invalid non-csv content");
    std::string ans_j = ans_fail.to_json();
    TEST_ASSERT(ans_j.find("\"min_temp_C\":null") != std::string::npos, "Failed ANSYS must emit min_temp_C: null");
    TEST_ASSERT(ans_j.find("\"max_temp_C\":null") != std::string::npos, "Failed ANSYS must emit max_temp_C: null");
    TEST_ASSERT(ans_j.find("\"avg_temp_C\":null") != std::string::npos, "Failed ANSYS must emit avg_temp_C: null");
    TEST_ASSERT(ans_j.find("\"bounds\":null") != std::string::npos, "Failed ANSYS must emit bounds: null");
    TEST_ASSERT(!contains_inf_or_nan_token(ans_j), "ANSYS JSON must never contain nan or inf");
    TEST_ASSERT(ans_j.find("1e30") == std::string::npos && ans_j.find("1e+30") == std::string::npos, "ANSYS JSON must never contain 1e30 sentinels");

    // 3. BenchmarkSummary with uncalculated/sentinel max_absolute_temp_diff_C
    BenchmarkSummary b_sum;
    b_sum.max_absolute_temp_diff_C = 1e30;
    std::string b_j = b_sum.to_json();
    TEST_ASSERT(b_j.find("\"max_absolute_temp_diff_C\":null") != std::string::npos, "Sentinel diff must emit null");
    TEST_ASSERT(!contains_inf_or_nan_token(b_j), "Benchmark JSON must never contain nan or inf");
}

void test_duplicate_id_and_1e300_rejection() {
    // 1. Duplicate node ID in ANSYS CSV
    std::string dup_csv =
        "node_id,x_m,y_m,z_m,temperature_C\n"
        "201,0.0,0.0,0.0,50.0\n"
        "202,0.1,0.0,0.0,60.0\n"
        "201,0.2,0.0,0.0,70.0\n";
    AnsysParseResult r1 = AnsysParser::parse_string(dup_csv);
    TEST_ASSERT(!r1.success, "Duplicate node ID must be rejected");
    TEST_ASSERT(r1.error_message.find("Duplicate node_id 201") != std::string::npos, "Error message must report duplicate node_id 201");

    // 2. Temperature of 1e300 C
    std::string t1e300_csv =
        "node_id,x_m,y_m,z_m,temperature_C\n"
        "1,0.0,0.0,0.0,1e300\n";
    AnsysParseResult r2 = AnsysParser::parse_string(t1e300_csv);
    TEST_ASSERT(!r2.success, "1e300 C temperature must be rejected");
    TEST_ASSERT(r2.error_message.find("5000 C") != std::string::npos || r2.error_message.find("exceeds") != std::string::npos,
                "Error message must report temperature range violation");

    // 3. Temperature exceeding 5000 C bound (|T| > 5000)
    std::string t_over =
        "node_id,x_m,y_m,z_m,temperature_C\n"
        "1,0.0,0.0,0.0,5000.5\n";
    AnsysParseResult r3 = AnsysParser::parse_string(t_over);
    TEST_ASSERT(!r3.success, "5000.5 C must be rejected (|T| > 5000 C)");
    TEST_ASSERT(r3.error_message.find("5000 C") != std::string::npos, "Error message must report 5000 C violation");

    // 4. Negative temperature below absolute zero
    std::string t_abs =
        "node_id,x_m,y_m,z_m,temperature_C\n"
        "1,0.0,0.0,0.0,-280.0\n";
    AnsysParseResult r4 = AnsysParser::parse_string(t_abs);
    TEST_ASSERT(!r4.success, "Temperature below absolute zero (-280 C) must be rejected");
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

    // Item 5 Extended Tests
    RUN_TEST(test_parameter_range_rejection);
    RUN_TEST(test_benchmark_concurrency_guard_409);
    RUN_TEST(test_benchmark_cancel_mid_run);
    RUN_TEST(test_json_sanitizer);
    RUN_TEST(test_duplicate_id_and_1e300_rejection);

    std::cout << "=================================================================\n";
    std::cout << " Test Summary: " << g_tests_passed << " Passed, " << g_tests_failed << " Failed\n";
    std::cout << "=================================================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
