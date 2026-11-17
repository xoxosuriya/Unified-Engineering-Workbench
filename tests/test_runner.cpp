#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using test_socket_t = SOCKET;
#define TEST_INVALID_SOCKET INVALID_SOCKET
#define test_closesocket(s) closesocket(s)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
using test_socket_t = int;
#define TEST_INVALID_SOCKET (-1)
#define test_closesocket(s) ::close(s)
#endif
#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <future>
#include "stl_parser.hpp"
#include "ansys_parser.hpp"
#include "thermal_fea_1d.hpp"
#include "multicore_bench.hpp"
#include "embedded_hardware.hpp"
#include "backprop_engine.hpp"
#include "http_server.hpp"
#include "sparse_matrix.hpp"
#include "thermal_solvers_3d.hpp"
#include "thermal_geometry_3d.hpp"
#include "comparative_benchmark_3d.hpp"

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
static std::string resolve_test_data_path(const std::string& rel_path) {
    if (std::filesystem::exists(rel_path)) return rel_path;
    if (std::filesystem::exists("../" + rel_path)) return "../" + rel_path;
    if (std::filesystem::exists("../../" + rel_path)) return "../../" + rel_path;
    return rel_path;
}

void test_disk_files_sample_bracket_and_ansys() {
    // Parse the actual sample_bracket.stl from disk
    StlParseResult stl_res = StlParser::parse_file(resolve_test_data_path("data/sample_bracket.stl"));
    TEST_ASSERT(stl_res.success, "sample_bracket.stl must parse successfully: " + stl_res.error_message);
    TEST_ASSERT(stl_res.facet_count == 32, "sample_bracket.stl has exactly 32 facets");
    TEST_ASSERT(std::abs(stl_res.surface_area - 0.046297) < 1e-4, "Surface area must match 0.0463 m^2");
    TEST_ASSERT(std::abs(stl_res.max_bound.x - 0.10) < 1e-3, "Max X bound is 0.10 m (100 mm)");
    TEST_ASSERT(std::abs(stl_res.max_bound.y - 0.08) < 1e-3, "Max Y bound is 0.08 m (80 mm)");
    TEST_ASSERT(std::abs(stl_res.max_bound.z - 0.10) < 1e-3, "Max Z bound is 0.10 m (100 mm)");

    // Parse the actual ansys_thermal_synthetic.csv from disk
    AnsysParseResult ansys_res = AnsysParser::parse_file(resolve_test_data_path("data/ansys_thermal_synthetic.csv"));
    TEST_ASSERT(ansys_res.success, "ansys_thermal_synthetic.csv must parse successfully: " + ansys_res.error_message);
    TEST_ASSERT(ansys_res.node_count == 3485, "Must contain exactly 3,485 nodes");
    TEST_ASSERT(std::abs(ansys_res.min_temp_C - 36.87) < 0.1, "Min temp must be approx 36.87 C");
    TEST_ASSERT(std::abs(ansys_res.max_temp_C - 104.0) < 0.1, "Max temp must be approx 104.0 C");
    TEST_ASSERT(ansys_res.is_synthetic, "Synthetic flag must be true");
}

void test_disk_test_samples() {
    // Test negative STL specimens on disk
    TEST_ASSERT(!StlParser::parse_file(resolve_test_data_path("data/test_samples/empty.stl")).success, "empty.stl must be rejected");
    TEST_ASSERT(!StlParser::parse_file(resolve_test_data_path("data/test_samples/corrupted_syntax.stl")).success, "corrupted_syntax.stl must be rejected");
    TEST_ASSERT(!StlParser::parse_file(resolve_test_data_path("data/test_samples/nan_coords.stl")).success, "nan_coords.stl must be rejected");

    // Test negative CSV specimens on disk
    TEST_ASSERT(!AnsysParser::parse_file(resolve_test_data_path("data/test_samples/empty.csv")).success, "empty.csv must be rejected");
    TEST_ASSERT(!AnsysParser::parse_file(resolve_test_data_path("data/test_samples/missing_columns.csv")).success, "missing_columns.csv must be rejected");
    TEST_ASSERT(!AnsysParser::parse_file(resolve_test_data_path("data/test_samples/sub_absolute_zero.csv")).success, "sub_absolute_zero.csv must be rejected");
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
    test_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == TEST_INVALID_SOCKET) return "";
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        test_closesocket(s);
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
    test_closesocket(s);
    return resp;
}

static std::string http_post_sync(const std::string& host, int port, const std::string& path, const std::string& body, const std::string& extra_headers = "") {
    test_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == TEST_INVALID_SOCKET) return "";
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        test_closesocket(s);
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
    test_closesocket(s);
    return resp;
}

static std::string http_custom_req_sync(const std::string& method, const std::string& host, int port, const std::string& path, const std::string& body, const std::string& extra_headers = "") {
    test_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == TEST_INVALID_SOCKET) return "";
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        test_closesocket(s);
        return "";
    }
    std::string req = method + " " + path + " HTTP/1.1\r\n" +
                      "Host: " + host + ":" + std::to_string(port) + "\r\n" +
                      extra_headers +
                      "Content-Length: " + std::to_string(body.size()) + "\r\n" +
                      "Connection: close\r\n\r\n" + body;
    send(s, req.data(), static_cast<int>(req.size()), 0);
    std::string resp;
    char buf[4096];
    int r = 0;
    while ((r = recv(s, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, r);
    }
    test_closesocket(s);
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
// Round 2 — Task G Tests (Sparse Matrix & Solvers)
// -------------------------------------------------------------

void test_sparse_matrix_spd_properties() {
    // 1. Construct 3x3 symmetric diagonally dominant test matrix
    // [  4.0  -1.0   0.0 ]
    // [ -1.0   4.0  -1.0 ]
    // [  0.0  -1.0   4.0 ]
    SparseMatrixBuilder b(3, 3);
    b.add_entry(0, 0, 4.0);
    b.add_entry(0, 1, -1.0);
    b.add_entry(1, 0, -1.0);
    b.add_entry(1, 1, 4.0);
    b.add_entry(1, 2, -1.0);
    b.add_entry(2, 1, -1.0);
    b.add_entry(2, 2, 4.0);

    CSRMatrix mat = b.build(true);
    std::string err;
    TEST_ASSERT(mat.is_valid(&err), "CSR matrix must pass structural validation: " + err);
    TEST_ASSERT(mat.nnz() == 7, "CSR NNZ must be 7");
    TEST_ASSERT(mat.num_rows == 3 && mat.num_cols == 3, "Dimensions must be 3x3");

    // 2. Matrix-vector multiplication against known reference
    // A * [1.0, 2.0, 3.0]^T = [4(1) - 2, -1 + 8 - 3, -2 + 12]^T = [2.0, 4.0, 10.0]^T
    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<double> y;
    mat.matvec(x, y);
    TEST_ASSERT(y.size() == 3, "Matvec output size must be 3");
    TEST_ASSERT(std::abs(y[0] - 2.0) < 1e-14, "Matvec y[0] must be 2.0");
    TEST_ASSERT(std::abs(y[1] - 4.0) < 1e-14, "Matvec y[1] must be 4.0");
    TEST_ASSERT(std::abs(y[2] - 10.0) < 1e-14, "Matvec y[2] must be 10.0");

    // 3. Diagonal extraction
    std::vector<double> diag = mat.get_diagonal();
    TEST_ASSERT(diag.size() == 3, "Diagonal vector size must be 3");
    TEST_ASSERT(diag[0] == 4.0 && diag[1] == 4.0 && diag[2] == 4.0, "Diagonal entries must be 4.0");

    // 4. Symmetry and strictly positive diagonal
    TEST_ASSERT(mat.is_symmetric(10.0 * EPS_MACH, &err), "Matrix must be symmetric: " + err);
    TEST_ASSERT(mat.has_strictly_positive_diagonal(&err), "Matrix must have strictly positive diagonal: " + err);

    // 5. Weak diagonal dominance using absolute values of off-diagonals
    TEST_ASSERT(mat.is_weakly_diagonally_dominant(10.0 * EPS_MACH, &err), 
                "Matrix must satisfy weak diagonal dominance: " + err);

    // 6. Invalid matrix rejection tests
    // Non-symmetric matrix
    SparseMatrixBuilder b_nonsym(2, 2);
    b_nonsym.add_entry(0, 0, 4.0);
    b_nonsym.add_entry(0, 1, -1.0);
    b_nonsym.add_entry(1, 0, -2.0); // Asymmetry: -2 != -1
    b_nonsym.add_entry(1, 1, 4.0);
    CSRMatrix mat_nonsym = b_nonsym.build();
    TEST_ASSERT(!mat_nonsym.is_symmetric(10.0 * EPS_MACH), "Asymmetric matrix must be rejected by is_symmetric");

    // Non-positive diagonal
    SparseMatrixBuilder b_negdiag(2, 2);
    b_negdiag.add_entry(0, 0, 4.0);
    b_negdiag.add_entry(1, 1, -2.0); // Negative diagonal
    CSRMatrix mat_negdiag = b_negdiag.build();
    TEST_ASSERT(!mat_negdiag.has_strictly_positive_diagonal(), "Negative diagonal must be rejected");

    // Diagonal dominance violation
    SparseMatrixBuilder b_notdom(2, 2);
    b_notdom.add_entry(0, 0, 1.0);
    b_notdom.add_entry(0, 1, -2.0); // | -2 | > 1
    b_notdom.add_entry(1, 0, -2.0);
    b_notdom.add_entry(1, 1, 1.0);
    CSRMatrix mat_notdom = b_notdom.build();
    TEST_ASSERT(!mat_notdom.is_weakly_diagonally_dominant(10.0 * EPS_MACH), 
                "Matrix violating diagonal dominance must be rejected");

    // Structural validation rejection: out of bounds column
    CSRMatrix corrupt = mat;
    corrupt.col_idx[1] = 99; // Out of bounds column
    TEST_ASSERT(!corrupt.is_valid(), "CSR with out-of-bounds column index must be rejected by is_valid");

    // 7. Exact NNZ accounting on known structured 3D grid
    // 2x2x2 cube grid: M = 8 cells, E = 3 * 2^2 * (2 - 1) = 12 internal faces
    // Expected NNZ = M + 2E = 8 + 24 = 32
    SparseMatrixBuilder b_cube2(8, 8);
    auto cell_id = [](int i, int j, int k) { return i * 4 + j * 2 + k; };
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                int u = cell_id(i, j, k);
                b_cube2.add_entry(u, u, 6.0); // Diagonal
                if (i + 1 < 2) {
                    int v = cell_id(i + 1, j, k);
                    b_cube2.add_entry(u, v, -1.0);
                    b_cube2.add_entry(v, u, -1.0);
                }
                if (j + 1 < 2) {
                    int v = cell_id(i, j + 1, k);
                    b_cube2.add_entry(u, v, -1.0);
                    b_cube2.add_entry(v, u, -1.0);
                }
                if (k + 1 < 2) {
                    int v = cell_id(i, j, k + 1);
                    b_cube2.add_entry(u, v, -1.0);
                    b_cube2.add_entry(v, u, -1.0);
                }
            }
        }
    }
    CSRMatrix mat_cube2 = b_cube2.build(true);
    TEST_ASSERT(mat_cube2.num_rows == 8, "Cube 2x2x2 active cells must be 8");
    TEST_ASSERT(mat_cube2.nnz() == 32, "Cube 2x2x2 NNZ must equal M + 2E = 8 + 2(12) = 32");
}

void test_direct_cholesky_factorization_exactness() {
    // 1. Build a 4x4 tridiagonal SPD matrix:
    // [  2  -1   0   0 ]
    // [ -1   2  -1   0 ]
    // [  0  -1   2  -1 ]
    // [  0   0  -1   2 ]
    SparseMatrixBuilder b(4, 4);
    for (int i = 0; i < 4; ++i) {
        b.add_entry(i, i, 2.0);
        if (i + 1 < 4) {
            b.add_entry(i, i + 1, -1.0);
            b.add_entry(i + 1, i, -1.0);
        }
    }
    CSRMatrix A = b.build();
    TEST_ASSERT(A.is_valid(), "Matrix A must be valid");

    // 2. Profile Cholesky factorization without RCM first
    ProfileCholesky chol_natural;
    ProfileCholesky::Status s1 = chol_natural.factor(A, false);
    TEST_ASSERT(s1 == ProfileCholesky::Status::SUCCESS, "Natural profile Cholesky must succeed");

    // 3. Verify scale-relative factor reconstruction error ||A - L L^T||_inf / ||A||_inf
    double recon_err = chol_natural.compute_reconstruction_error(A);
    TEST_ASSERT(recon_err < 1e-14, "Reconstruction error must be < 1e-14 (got " + std::to_string(recon_err) + ")");

    // 4. Solve known linear system A * x = b where x_true = [1, 2, 3, 4]^T
    // A * [1, 2, 3, 4]^T = [2(1)-2, -1+4-3, -2+6-4, -3+8]^T = [0, 0, 0, 5]^T
    std::vector<double> x_true = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> b_vec = {0.0, 0.0, 0.0, 5.0};
    std::vector<double> x_sol;
    bool solved = chol_natural.solve(b_vec, x_sol);
    TEST_ASSERT(solved, "Natural solve must succeed");
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT(std::abs(x_sol[i] - x_true[i]) < 1e-13, 
                    "Solution component " + std::to_string(i) + " must match x_true");
    }

    // 5. Verify scale-relative backward error
    double back_err = chol_natural.compute_backward_error(A, b_vec, x_sol);
    TEST_ASSERT(back_err < 100.0 * EPS_MACH, 
                "Backward error must be < 100 * eps_mach (got " + std::to_string(back_err) + ")");

    // 6. Profile Cholesky with Reverse Cuthill-McKee (RCM)
    ProfileCholesky chol_rcm;
    ProfileCholesky::Status s2 = chol_rcm.factor(A, true);
    TEST_ASSERT(s2 == ProfileCholesky::Status::SUCCESS, "RCM profile Cholesky must succeed");

    // Verify RCM permutation validity
    const auto& rcm = chol_rcm.get_rcm_result();
    TEST_ASSERT(rcm.is_valid, "RCM permutation must be valid");
    TEST_ASSERT(RCMOrdering::validate_permutation(rcm.perm), "Permutation vector must be a valid bijection");

    // Solve using RCM Cholesky
    std::vector<double> x_rcm;
    bool solved_rcm = chol_rcm.solve(b_vec, x_rcm);
    TEST_ASSERT(solved_rcm, "RCM solve must succeed");

    // Verify equivalence of solutions between RCM and natural ordering
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT(std::abs(x_rcm[i] - x_true[i]) < 1e-13, 
                    "RCM solution component " + std::to_string(i) + " must match x_true");
        TEST_ASSERT(std::abs(x_rcm[i] - x_sol[i]) < 1e-14, 
                    "RCM and natural solutions must be identical within 1e-14");
    }

    // 7. Verify factor metrics
    const auto& m = chol_rcm.get_metrics();
    TEST_ASSERT(m.matrix_dim == 4, "Matrix dimension must be 4");
    TEST_ASSERT(m.matrix_nnz == 10, "Matrix NNZ must be 10");
    TEST_ASSERT(m.envelope_capacity > 0, "Envelope capacity must be > 0");
    TEST_ASSERT(m.numerical_factor_nnz > 0, "Numerical factor NNZ must be > 0");
    TEST_ASSERT(m.fill_in_ratio > 0.0, "Fill-in ratio must be positive");
    TEST_ASSERT(m.allocated_factor_bytes > 0, "Allocated factor memory must be > 0");
    TEST_ASSERT(m.factorization_flops > 0, "Factorization FLOP count must be > 0");
    TEST_ASSERT(m.min_pivot > 0.0, "Minimum pivot must be positive");

    // 8. Reject non-SPD indefinite matrix
    SparseMatrixBuilder b_indef(2, 2);
    b_indef.add_entry(0, 0, 1.0);
    b_indef.add_entry(0, 1, 2.0);
    b_indef.add_entry(1, 0, 2.0);
    b_indef.add_entry(1, 1, 1.0); // Eigenvalues: 3.0 and -1.0 (Indefinite)
    CSRMatrix A_indef = b_indef.build();

    ProfileCholesky chol_fail;
    ProfileCholesky::Status s_indef = chol_fail.factor(A_indef, false);
    TEST_ASSERT(s_indef != ProfileCholesky::Status::SUCCESS, 
                "Indefinite matrix must be rejected by profile Cholesky factorization");

    // 9. Reject singular zero-pivot matrix
    SparseMatrixBuilder b_sing(2, 2);
    b_sing.add_entry(0, 0, 1.0);
    b_sing.add_entry(0, 1, 1.0);
    b_sing.add_entry(1, 0, 1.0);
    b_sing.add_entry(1, 1, 1.0); // Determinant = 0
    CSRMatrix A_sing = b_sing.build();

    ProfileCholesky chol_sing;
    ProfileCholesky::Status s_sing = chol_sing.factor(A_sing, false);
    TEST_ASSERT(s_sing != ProfileCholesky::Status::SUCCESS, 
                "Singular matrix with zero pivot must be rejected by profile Cholesky");
}

void test_cholesky_pivot_scale_invariance() {
    // 1. Base 4x4 tridiagonal SPD matrix:
    // [  2.0  -1.0   0.0   0.0 ]
    // [ -1.0   2.0  -1.0   0.0 ]
    // [  0.0  -1.0   2.0  -1.0 ]
    // [  0.0   0.0  -1.0   2.0 ]
    const std::vector<double> scales = {1.0, 1e-12, 1e12};
    const std::vector<double> x_true = {1.0, 2.0, 3.0, 4.0};

    std::vector<double> base_factor_L0;

    for (double s : scales) {
        SparseMatrixBuilder b(4, 4);
        for (int i = 0; i < 4; ++i) {
            b.add_entry(i, i, 2.0 * s);
            if (i + 1 < 4) {
                b.add_entry(i, i + 1, -1.0 * s);
                b.add_entry(i + 1, i, -1.0 * s);
            }
        }
        CSRMatrix A_scaled = b.build();
        TEST_ASSERT(A_scaled.is_valid(), "Scaled matrix must be structurally valid");

        // Factor scaled matrix
        ProfileCholesky chol;
        ProfileCholesky::Status status = chol.factor(A_scaled, false);
        TEST_ASSERT(status == ProfileCholesky::Status::SUCCESS, 
                    "Scale-invariant factorization must succeed for scale " + std::to_string(s));

        // Factor reconstruction: ||A_s - L_s L_s^T||_inf / ||A_s||_inf
        double recon_err = chol.compute_reconstruction_error(A_scaled);
        TEST_ASSERT(recon_err < 1e-14, 
                    "Reconstruction error at scale " + std::to_string(s) + " must be < 1e-14 (got " + std::to_string(recon_err) + ")");

        // Compute RHS: b_s = A_s * x_true
        std::vector<double> b_s;
        A_scaled.matvec(x_true, b_s);

        // Solve A_s * x = b_s
        std::vector<double> x_sol;
        bool solved = chol.solve(b_s, x_sol);
        TEST_ASSERT(solved, "Solve must succeed at scale " + std::to_string(s));

        // Solution must match x_true independently of matrix scale s
        for (int i = 0; i < 4; ++i) {
            TEST_ASSERT(std::abs(x_sol[i] - x_true[i]) < 1e-11, 
                        "Solution component " + std::to_string(i) + " at scale " + std::to_string(s) + 
                        " must match x_true (got " + std::to_string(x_sol[i]) + ")");
        }

        // Backward error must be scale-relative: ||b_s - A_s x|| / (||A_s|| ||x|| + ||b_s||)
        double back_err = chol.compute_backward_error(A_scaled, b_s, x_sol);
        TEST_ASSERT(back_err < 100.0 * EPS_MACH, 
                    "Backward error at scale " + std::to_string(s) + " must be < 100 * eps_mach (got " + std::to_string(back_err) + ")");

        // Verify that factor entries L_s scale strictly as sqrt(s)
        double L_00 = chol.get_profile_values()[0]; // L(0, 0)
        if (s == 1.0) {
            base_factor_L0.push_back(L_00);
        } else {
            double expected_ratio = std::sqrt(s);
            double actual_ratio = L_00 / base_factor_L0[0];
            TEST_ASSERT(std::abs(actual_ratio - expected_ratio) / expected_ratio < 1e-12, 
                        "Factor L(0,0) must scale as sqrt(s): actual=" + std::to_string(actual_ratio) + 
                        " vs expected=" + std::to_string(expected_ratio));
        }
    }
}

void test_csr_structural_robustness() {
    // 1. Valid empty matrix (0x0)
    CSRMatrix empty_mat;
    empty_mat.num_rows = 0;
    empty_mat.num_cols = 0;
    empty_mat.row_ptr = {0};
    TEST_ASSERT(empty_mat.is_valid(), "0x0 CSR matrix must be valid");
    TEST_ASSERT(empty_mat.nnz() == 0, "Empty matrix NNZ must be 0");

    // 2. Matrix with empty row (row 1 is empty in 3x3 matrix)
    CSRMatrix mat_empty_row;
    mat_empty_row.num_rows = 3;
    mat_empty_row.num_cols = 3;
    mat_empty_row.row_ptr = {0, 1, 1, 2}; // Row 0: 1 elem, Row 1: 0 elem, Row 2: 1 elem
    mat_empty_row.col_idx = {0, 2};
    mat_empty_row.values = {5.0, 7.0};
    TEST_ASSERT(mat_empty_row.is_valid(), "CSR with empty rows must be structurally valid");
    std::vector<double> x_in = {1.0, 2.0, 3.0};
    std::vector<double> y_out;
    mat_empty_row.matvec(x_in, y_out);
    TEST_ASSERT(y_out[1] == 0.0, "Matvec on empty row must yield 0.0");
    TEST_ASSERT(y_out[0] == 5.0 && y_out[2] == 21.0, "Matvec on non-empty rows must be correct");

    // 3. Invalid row_ptr length: row_ptr.size() != num_rows + 1
    CSRMatrix bad_rowptr_len = mat_empty_row;
    bad_rowptr_len.row_ptr = {0, 1, 2}; // Size 3 instead of 4
    TEST_ASSERT(!bad_rowptr_len.is_valid(), "row_ptr.size() != num_rows + 1 must be rejected");

    // 4. Invalid row_ptr[0] != 0
    CSRMatrix bad_rowptr_start = mat_empty_row;
    bad_rowptr_start.row_ptr = {1, 2, 2, 3};
    TEST_ASSERT(!bad_rowptr_start.is_valid(), "row_ptr[0] != 0 must be rejected");

    // 5. Non-monotonic row_ptr: row_ptr[i] > row_ptr[i+1]
    CSRMatrix bad_rowptr_mono = mat_empty_row;
    bad_rowptr_mono.row_ptr = {0, 2, 1, 2}; // 2 > 1
    TEST_ASSERT(!bad_rowptr_mono.is_valid(), "Non-monotonic row_ptr must be rejected");

    // 6. row_ptr[num_rows] != values.size()
    CSRMatrix bad_rowptr_end = mat_empty_row;
    bad_rowptr_end.row_ptr = {0, 1, 1, 3}; // 3 != 2
    TEST_ASSERT(!bad_rowptr_end.is_valid(), "row_ptr[num_rows] != values.size() must be rejected");

    // 7. Out-of-range column indices (negative and >= num_cols)
    CSRMatrix bad_col_neg = mat_empty_row;
    bad_col_neg.col_idx = {-1, 2};
    TEST_ASSERT(!bad_col_neg.is_valid(), "Negative column index must be rejected");

    CSRMatrix bad_col_large = mat_empty_row;
    bad_col_large.col_idx = {0, 3}; // Column 3 >= num_cols (3)
    TEST_ASSERT(!bad_col_large.is_valid(), "Column index >= num_cols must be rejected");

    // 8. Unsorted or duplicate column indices in same row
    CSRMatrix bad_col_dup;
    bad_col_dup.num_rows = 1;
    bad_col_dup.num_cols = 3;
    bad_col_dup.row_ptr = {0, 2};
    bad_col_dup.col_idx = {1, 1}; // Duplicate col 1
    bad_col_dup.values = {1.0, 2.0};
    TEST_ASSERT(!bad_col_dup.is_valid(), "Duplicate column indices in same row must be rejected");

    CSRMatrix bad_col_unsorted = bad_col_dup;
    bad_col_unsorted.col_idx = {2, 0}; // 2 > 0 (unsorted)
    TEST_ASSERT(!bad_col_unsorted.is_valid(), "Unsorted column indices in same row must be rejected");

    // 9. Coordinate triplet duplicate summation in builder
    SparseMatrixBuilder b_dup(2, 2);
    b_dup.add_entry(0, 1, 3.5);
    b_dup.add_entry(0, 1, 1.5); // Duplicate (0, 1): sum should be 5.0
    b_dup.add_entry(1, 1, 4.0);
    CSRMatrix mat_summed = b_dup.build(true);
    TEST_ASSERT(mat_summed.is_valid(), "Summed matrix must be valid");
    TEST_ASSERT(mat_summed.nnz() == 2, "Duplicate entries must be merged into 1 entry");
    TEST_ASSERT(mat_summed.get_entry(0, 1) == 5.0, "Merged value must be 3.5 + 1.5 = 5.0");

    // 10. Matrix-vector dimension mismatch throws std::invalid_argument
    bool caught_mismatch = false;
    try {
        std::vector<double> x_wrong_size = {1.0, 2.0}; // Size 2 vs num_cols 3
        std::vector<double> y_dummy;
        mat_empty_row.matvec(x_wrong_size, y_dummy);
    } catch (const std::invalid_argument&) {
        caught_mismatch = true;
    }
    TEST_ASSERT(caught_mismatch, "matvec must throw std::invalid_argument on dimension mismatch");

    // 11. NaN and infinity handling in values
    CSRMatrix mat_nan = mat_empty_row;
    mat_nan.values[0] = std::numeric_limits<double>::quiet_NaN();
    TEST_ASSERT(!mat_nan.is_valid(), "CSR with NaN value must be rejected");

    CSRMatrix mat_inf = mat_empty_row;
    mat_inf.values[0] = std::numeric_limits<double>::infinity();
    TEST_ASSERT(!mat_inf.is_valid(), "CSR with +Inf value must be rejected");

    CSRMatrix mat_neginf = mat_empty_row;
    mat_neginf.values[0] = -std::numeric_limits<double>::infinity();
    TEST_ASSERT(!mat_neginf.is_valid(), "CSR with -Inf value must be rejected");

    // 12. Safe dimension and allocation overflow checks in SparseMatrixBuilder
    bool caught_oob = false;
    SparseMatrixBuilder b_bounds(2, 2);
    try {
        b_bounds.add_entry(2, 0, 1.0); // Row 2 >= 2
    } catch (const std::out_of_range&) {
        caught_oob = true;
    }
    TEST_ASSERT(caught_oob, "SparseMatrixBuilder::add_entry must throw std::out_of_range for row >= num_rows");

    caught_oob = false;
    try {
        b_bounds.add_entry(0, 2, 1.0); // Col 2 >= 2
    } catch (const std::out_of_range&) {
        caught_oob = true;
    }
    TEST_ASSERT(caught_oob, "SparseMatrixBuilder::add_entry must throw std::out_of_range for col >= num_cols");
}

void test_profile_cholesky_skyline_fillin_and_rcm() {
    // Construct nontrivial 6x6 ring SPD matrix:
    // Strong diagonal (4.0) with -1.0 off-diagonals on ring edges:
    // (0,1), (1,2), (2,3), (3,4), (4,5), and (0,5).
    // In natural order, row 5 connects to column 0 and 4.
    // Therefore col_start[5] = 0, creating a profile row of length 6:
    // [ (5,0), (5,1), (5,2), (5,3), (5,4), (5,5) ]
    // Although A(5,1) = A(5,2) = A(5,3) = 0 in the sparse stencil,
    // they reside inside the profile envelope and fill in during Cholesky factorization.
    const std::size_t n = 6;
    SparseMatrixBuilder b(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        b.add_entry(i, i, 4.0);
        std::size_t next = (i + 1) % n;
        b.add_entry(i, next, -1.0);
        b.add_entry(next, i, -1.0);
    }
    CSRMatrix A = b.build(true);
    TEST_ASSERT(A.is_valid(), "Ring matrix must be valid");
    TEST_ASSERT(A.nnz() == 18, "Ring matrix 6x6 must have 6 diag + 12 off-diag = 18 nonzeros");

    // 1. Factor in natural ordering (without RCM)
    ProfileCholesky chol_nat;
    ProfileCholesky::Status s_nat = chol_nat.factor(A, false);
    TEST_ASSERT(s_nat == ProfileCholesky::Status::SUCCESS, "Natural profile Cholesky must succeed");

    const auto& m_nat = chol_nat.get_metrics();
    // Natural envelope capacity: row 0: 1, row 1: 2, row 2: 2, row 3: 2, row 4: 2, row 5: 6 => total 15
    TEST_ASSERT(m_nat.envelope_capacity == 15, 
                "Natural envelope capacity must be exactly 15 (got " + std::to_string(m_nat.envelope_capacity) + ")");

    // Verify fill-in: lower triangle of A has 12 entries (6 diag + 6 lower off-diag).
    // Envelope has 15 positions. The 3 envelope zeros fill in during factorization.
    // Numerical factor nonzeros NNZ_L must equal 15.
    TEST_ASSERT(m_nat.numerical_factor_nnz == 15, 
                "Numerical factor nonzeros must equal 15 due to fill-in (got " + std::to_string(m_nat.numerical_factor_nnz) + ")");
    TEST_ASSERT(m_nat.fill_in_ratio > 1.0, "Fill-in ratio must exceed 1.0 (got " + std::to_string(m_nat.fill_in_ratio) + ")");

    // Factor reconstruction error in natural order
    double recon_nat = chol_nat.compute_reconstruction_error(A);
    TEST_ASSERT(recon_nat < 1e-14, "Natural reconstruction error must be < 1e-14");

    // Solve against known vector x_true = [1, 2, 3, 4, 5, 6]^T
    std::vector<double> x_true = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> b_vec;
    A.matvec(x_true, b_vec);

    std::vector<double> x_nat;
    bool solved_nat = chol_nat.solve(b_vec, x_nat);
    TEST_ASSERT(solved_nat, "Natural solve must succeed");
    for (std::size_t i = 0; i < n; ++i) {
        TEST_ASSERT(std::abs(x_nat[i] - x_true[i]) < 1e-13, 
                    "Natural solve component " + std::to_string(i) + " must match x_true");
    }
    double back_nat = chol_nat.compute_backward_error(A, b_vec, x_nat);
    TEST_ASSERT(back_nat < 100.0 * EPS_MACH, "Natural backward error must be < 100 * eps_mach");

    // 2. Factor with RCM ordering
    ProfileCholesky chol_rcm;
    ProfileCholesky::Status s_rcm = chol_rcm.factor(A, true);
    TEST_ASSERT(s_rcm == ProfileCholesky::Status::SUCCESS, "RCM profile Cholesky must succeed");

    const auto& m_rcm = chol_rcm.get_metrics();
    // Verify profile envelope before and after RCM
    // Report both capacities: natural envelope = 15, RCM envelope = 14
    std::cout << "      [Skyline Profile Report] Natural Envelope: " << m_nat.envelope_capacity 
              << ", RCM Envelope: " << m_rcm.envelope_capacity << "\n";
    TEST_ASSERT(m_rcm.envelope_capacity <= m_nat.envelope_capacity, 
                "RCM envelope capacity must be <= natural envelope capacity for ring graph");

    // Factor reconstruction error in RCM order
    const auto& rcm_res = chol_rcm.get_rcm_result();
    CSRMatrix A_perm = RCMOrdering::permute_matrix(A, rcm_res.perm, rcm_res.inv_perm);
    double recon_rcm = chol_rcm.compute_reconstruction_error(A_perm);
    TEST_ASSERT(recon_rcm < 1e-14, "RCM reconstruction error must be < 1e-14");

    // Solve using RCM
    std::vector<double> x_rcm;
    bool solved_rcm = chol_rcm.solve(b_vec, x_rcm);
    TEST_ASSERT(solved_rcm, "RCM solve must succeed");

    // Verify equivalence of solutions between natural and RCM orderings
    for (std::size_t i = 0; i < n; ++i) {
        TEST_ASSERT(std::abs(x_rcm[i] - x_true[i]) < 1e-13, 
                    "RCM solve component " + std::to_string(i) + " must match x_true");
        TEST_ASSERT(std::abs(x_rcm[i] - x_nat[i]) < 1e-14, 
                    "RCM and Natural solutions must be identical within 1e-14");
    }

    // 3. Clean rejection of non-SPD matrix with negative pivot
    SparseMatrixBuilder b_nonspd(n, n);
    for (std::size_t i = 0; i < n; ++i) {
        b_nonspd.add_entry(i, i, (i == 3) ? -4.0 : 4.0); // Negative diagonal at row 3
        std::size_t next = (i + 1) % n;
        b_nonspd.add_entry(i, next, -1.0);
        b_nonspd.add_entry(next, i, -1.0);
    }
    CSRMatrix A_nonspd = b_nonspd.build(true);
    ProfileCholesky chol_bad;
    ProfileCholesky::Status s_bad = chol_bad.factor(A_nonspd, false);
    TEST_ASSERT(s_bad == ProfileCholesky::Status::FAILED_NON_SPD, 
                "Matrix with negative diagonal must be cleanly rejected with FAILED_NON_SPD");
}

void test_phase1_hardening_and_edge_cases() {
    // 1. Unilateral off-diagonal symmetry check: upper-only vs lower-only
    SparseMatrixBuilder b_upper(2, 2);
    b_upper.add_entry(0, 0, 2.0);
    b_upper.add_entry(0, 1, 1.0); // Only upper off-diagonal
    b_upper.add_entry(1, 1, 2.0);
    CSRMatrix A_upper = b_upper.build();
    TEST_ASSERT(A_upper.is_valid(), "A_upper must be structurally valid");
    TEST_ASSERT(!A_upper.is_symmetric(10.0 * EPS_MACH), "A_upper with only upper off-diagonal must fail is_symmetric");

    SparseMatrixBuilder b_lower(2, 2);
    b_lower.add_entry(0, 0, 2.0);
    b_lower.add_entry(1, 0, 1.0); // Only lower off-diagonal
    b_lower.add_entry(1, 1, 2.0);
    CSRMatrix A_lower = b_lower.build();
    TEST_ASSERT(A_lower.is_valid(), "A_lower must be structurally valid");
    TEST_ASSERT(!A_lower.is_symmetric(10.0 * EPS_MACH), "A_lower with only lower off-diagonal must fail is_symmetric");

    // Non-finite off-diagonal symmetry rejection
    CSRMatrix A_nan_sym = A_upper;
    A_nan_sym.values[1] = std::numeric_limits<double>::quiet_NaN();
    TEST_ASSERT(!A_nan_sym.is_symmetric(10.0 * EPS_MACH), "is_symmetric must reject matrix containing NaN");
    CSRMatrix A_inf_sym = A_upper;
    A_inf_sym.values[1] = std::numeric_limits<double>::infinity();
    TEST_ASSERT(!A_inf_sym.is_symmetric(10.0 * EPS_MACH), "is_symmetric must reject matrix containing +Inf");

    // 2. Cholesky pre-validation rejection of nonsymmetric matrices
    ProfileCholesky chol_u;
    ProfileCholesky::Status su = chol_u.factor(A_upper, false);
    TEST_ASSERT(su == ProfileCholesky::Status::FAILED_NON_SPD, 
                "ProfileCholesky must reject upper-only nonsymmetric matrix");

    ProfileCholesky chol_l;
    ProfileCholesky::Status sl = chol_l.factor(A_lower, false);
    TEST_ASSERT(sl == ProfileCholesky::Status::FAILED_NON_SPD, 
                "ProfileCholesky must reject lower-only nonsymmetric matrix");

    // 3. Cholesky pre-validation rejection of malformed CSR and missing diagonal
    CSRMatrix A_malformed = A_upper;
    A_malformed.row_ptr[0] = 1; // Corrupted row pointer
    ProfileCholesky chol_m;
    ProfileCholesky::Status sm = chol_m.factor(A_malformed, false);
    TEST_ASSERT(sm == ProfileCholesky::Status::FAILED_DIMENSION_MISMATCH, 
                "ProfileCholesky must reject structurally invalid CSR matrix");

    SparseMatrixBuilder b_nodiag(2, 2);
    b_nodiag.add_entry(0, 0, 2.0);
    b_nodiag.add_entry(0, 1, 1.0);
    b_nodiag.add_entry(1, 0, 1.0);
    // Row 1 is missing diagonal entry (1, 1)
    CSRMatrix A_nodiag = b_nodiag.build();
    ProfileCholesky chol_nd;
    ProfileCholesky::Status snd = chol_nd.factor(A_nodiag, false);
    TEST_ASSERT(snd == ProfileCholesky::Status::FAILED_NON_SPD, 
                "ProfileCholesky must reject matrix with missing diagonal entry");

    // 4. Safe unfactored and failed solve behavior
    ProfileCholesky chol_unfactored;
    std::vector<double> b_vec = {1.0, 2.0};
    std::vector<double> x_out = {99.0, 99.0};
    bool solve_ok = chol_unfactored.solve(b_vec, x_out);
    TEST_ASSERT(!solve_ok, "Calling solve() on unfactored instance must return false");
    TEST_ASSERT(x_out.empty() || (x_out[0] == 0.0 && x_out[1] == 0.0), 
                "Output vector must be cleared or set to safe zero state on unfactored solve");

    x_out = {99.0, 99.0};
    bool solve_failed_ok = chol_nd.solve(b_vec, x_out);
    TEST_ASSERT(!solve_failed_ok, "Calling solve() on failed factorization instance must return false");

    // 5. RCM on disconnected graph and isolated vertices
    // 4x4 matrix: 2 disconnected 2-node components (0-1 connected, 2-3 connected)
    SparseMatrixBuilder b_disc(4, 4);
    b_disc.add_entry(0, 0, 4.0); b_disc.add_entry(0, 1, -1.0);
    b_disc.add_entry(1, 0, -1.0); b_disc.add_entry(1, 1, 4.0);
    b_disc.add_entry(2, 2, 4.0); b_disc.add_entry(2, 3, -1.0);
    b_disc.add_entry(3, 2, -1.0); b_disc.add_entry(3, 3, 4.0);
    CSRMatrix A_disc = b_disc.build();
    auto rcm_disc = RCMOrdering::compute(A_disc);
    TEST_ASSERT(rcm_disc.is_valid, "RCM must successfully handle disconnected graph components");
    TEST_ASSERT(RCMOrdering::validate_permutation(rcm_disc.perm), 
                "RCM permutation on disconnected graph must be a valid bijection");

    // 3x3 matrix: node 2 is an isolated vertex (degree 0)
    SparseMatrixBuilder b_iso(3, 3);
    b_iso.add_entry(0, 0, 3.0); b_iso.add_entry(0, 1, -1.0);
    b_iso.add_entry(1, 0, -1.0); b_iso.add_entry(1, 1, 3.0);
    b_iso.add_entry(2, 2, 5.0); // Isolated node with A(2,2) = 5.0
    CSRMatrix A_iso = b_iso.build();
    auto rcm_iso = RCMOrdering::compute(A_iso);
    TEST_ASSERT(rcm_iso.is_valid, "RCM must successfully handle isolated degree-0 vertices");
    TEST_ASSERT(RCMOrdering::validate_permutation(rcm_iso.perm), 
                "RCM permutation with isolated vertex must be a valid bijection");

    // Factor and solve system with isolated vertex
    ProfileCholesky chol_iso;
    ProfileCholesky::Status s_iso = chol_iso.factor(A_iso, true);
    TEST_ASSERT(s_iso == ProfileCholesky::Status::SUCCESS, "Cholesky must succeed on SPD system with isolated vertex");
    std::vector<double> b_iso_vec = {2.0, 2.0, 15.0};
    std::vector<double> x_iso_sol;
    bool iso_solved = chol_iso.solve(b_iso_vec, x_iso_sol);
    TEST_ASSERT(iso_solved, "Solve must succeed on system with isolated vertex");
    TEST_ASSERT(std::abs(x_iso_sol[2] - 3.0) < 1e-12, "x[2] for isolated node (5 * x = 15) must equal 3.0");

    // Test solve rejects non-finite RHS (NaN and Inf) and resets output to 0.0
    std::vector<double> b_nan = {1.0, std::numeric_limits<double>::quiet_NaN(), 15.0};
    std::vector<double> x_nan = {1.0, 2.0, 3.0};
    bool ok_nan = chol_iso.solve(b_nan, x_nan);
    TEST_ASSERT(!ok_nan, "ProfileCholesky::solve must reject RHS containing NaN");
    TEST_ASSERT(x_nan[0] == 0.0 && x_nan[1] == 0.0 && x_nan[2] == 0.0, 
                "Output vector must be reset to zero when solve fails due to NaN RHS");

    std::vector<double> b_inf = {1.0, 2.0, std::numeric_limits<double>::infinity()};
    std::vector<double> x_inf = {1.0, 2.0, 3.0};
    bool ok_inf = chol_iso.solve(b_inf, x_inf);
    TEST_ASSERT(!ok_inf, "ProfileCholesky::solve must reject RHS containing +Inf");
    TEST_ASSERT(x_inf[0] == 0.0 && x_inf[1] == 0.0 && x_inf[2] == 0.0, 
                "Output vector must be reset to zero when solve fails due to +Inf RHS");

    // RCM permutation dimension and bijection validation
    bool caught_rcm_perm = false;
    try {
        std::vector<std::size_t> bad_perm = {0, 1}; // size 2 vs matrix size 4
        (void)RCMOrdering::permute_matrix(A_disc, bad_perm, bad_perm);
    } catch (const std::invalid_argument&) {
        caught_rcm_perm = true;
    }
    TEST_ASSERT(caught_rcm_perm, "permute_matrix must throw on permutation size mismatch");

    caught_rcm_perm = false;
    try {
        std::vector<std::size_t> non_bijection = {0, 0, 2, 3}; // duplicate 0
        (void)RCMOrdering::permute_matrix(A_disc, non_bijection, non_bijection);
    } catch (const std::invalid_argument&) {
        caught_rcm_perm = true;
    }
    TEST_ASSERT(caught_rcm_perm, "permute_matrix must throw on non-bijection permutation");

    // Test mutually inconsistent bijections (inv_perm[perm[i]] != i)
    caught_rcm_perm = false;
    try {
        // perm = {1, 2, 0} is a bijection; inv_perm = {0, 1, 2} is a bijection.
        // But inv_perm[perm[0]] = inv_perm[1] = 1 != 0 (mutually inconsistent).
        std::vector<std::size_t> p1 = {1, 2, 0};
        std::vector<std::size_t> p2 = {0, 1, 2};
        (void)RCMOrdering::permute_matrix(A_iso, p1, p2);
    } catch (const std::invalid_argument&) {
        caught_rcm_perm = true;
    }
    TEST_ASSERT(caught_rcm_perm, "permute_matrix must throw on mutually inconsistent permutations (inv_perm[perm[i]] != i)");

    caught_rcm_perm = false;
    try {
        std::vector<double> b_wrong_size = {1.0, 2.0};
        std::vector<std::size_t> perm_4 = {0, 1, 2, 3};
        (void)RCMOrdering::permute_vector(b_wrong_size, perm_4);
    } catch (const std::invalid_argument&) {
        caught_rcm_perm = true;
    }
    TEST_ASSERT(caught_rcm_perm, "permute_vector must throw on size mismatch");

    caught_rcm_perm = false;
    try {
        std::vector<double> x_wrong_size = {1.0, 2.0};
        std::vector<std::size_t> perm_4 = {0, 1, 2, 3};
        (void)RCMOrdering::unpermute_vector(x_wrong_size, perm_4);
    } catch (const std::invalid_argument&) {
        caught_rcm_perm = true;
    }
    TEST_ASSERT(caught_rcm_perm, "unpermute_vector must throw on size mismatch");

    // 6. SparseMatrixBuilder safety checks
    SparseMatrixBuilder b_valid(2, 2);
    bool caught_nan = false;
    try {
        b_valid.add_entry(0, 0, std::numeric_limits<double>::quiet_NaN());
    } catch (const std::invalid_argument&) {
        caught_nan = true;
    }
    TEST_ASSERT(caught_nan, "add_entry must throw std::invalid_argument on NaN");

    bool caught_inf = false;
    try {
        b_valid.add_entry(0, 0, std::numeric_limits<double>::infinity());
    } catch (const std::invalid_argument&) {
        caught_inf = true;
    }
    TEST_ASSERT(caught_inf, "add_entry must throw std::invalid_argument on +Inf");

    bool caught_builder_dim = false;
    try {
        SparseMatrixBuilder b_overflow(static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1, 10);
    } catch (const std::invalid_argument&) {
        caught_builder_dim = true;
    }
    TEST_ASSERT(caught_builder_dim, "Builder constructor must reject rows > INT_MAX");

    // Non-finite duplicate accumulation check (+Inf and -Inf)
    SparseMatrixBuilder b_dup_inf(2, 2);
    b_dup_inf.add_entry(0, 0, 1e308);
    b_dup_inf.add_entry(0, 0, 1e308); // 1e308 + 1e308 overflows to +Inf
    b_dup_inf.add_entry(1, 1, 2.0);
    bool caught_acc_inf = false;
    try {
        (void)b_dup_inf.build(true);
    } catch (const std::runtime_error&) {
        caught_acc_inf = true;
    }
    TEST_ASSERT(caught_acc_inf, "build must throw std::runtime_error when duplicate accumulation overflows to +Inf");

    SparseMatrixBuilder b_dup_neg_inf(2, 2);
    b_dup_neg_inf.add_entry(0, 0, -1e308);
    b_dup_neg_inf.add_entry(0, 0, -1e308); // -1e308 + -1e308 overflows to -Inf
    b_dup_neg_inf.add_entry(1, 1, 2.0);
    bool caught_acc_neg_inf = false;
    try {
        (void)b_dup_neg_inf.build(true);
    } catch (const std::runtime_error&) {
        caught_acc_neg_inf = true;
    }
    TEST_ASSERT(caught_acc_neg_inf, "build must throw std::runtime_error when duplicate accumulation overflows to -Inf");

    // 7. compute_reconstruction_error input validation tests (chol_iso is factored 3x3)
    CSRMatrix A_recon_malformed = A_iso;
    A_recon_malformed.row_ptr[0] = 999; // Corrupted row pointer
    TEST_ASSERT(chol_iso.compute_reconstruction_error(A_recon_malformed) == -1.0,
                "compute_reconstruction_error must return -1.0 for structurally invalid CSR");

    SparseMatrixBuilder b_nonsquare(2, 3);
    b_nonsquare.add_entry(0, 0, 1.0);
    b_nonsquare.add_entry(1, 1, 1.0);
    CSRMatrix A_nonsquare = b_nonsquare.build();
    TEST_ASSERT(chol_iso.compute_reconstruction_error(A_nonsquare) == -1.0,
                "compute_reconstruction_error must return -1.0 for non-square matrix");

    SparseMatrixBuilder b_dim_mismatch(2, 2);
    b_dim_mismatch.add_entry(0, 0, 1.0);
    b_dim_mismatch.add_entry(1, 1, 1.0);
    CSRMatrix A_dim_mismatch = b_dim_mismatch.build();
    TEST_ASSERT(chol_iso.compute_reconstruction_error(A_dim_mismatch) == -1.0,
                "compute_reconstruction_error must return -1.0 for dimension mismatch");

    SparseMatrixBuilder b_nonsym3(3, 3);
    b_nonsym3.add_entry(0, 0, 2.0);
    b_nonsym3.add_entry(0, 1, 1.0); // No (1, 0) entry
    b_nonsym3.add_entry(1, 1, 2.0);
    b_nonsym3.add_entry(2, 2, 2.0);
    CSRMatrix A_nonsym3 = b_nonsym3.build();
    TEST_ASSERT(chol_iso.compute_reconstruction_error(A_nonsym3) == -1.0,
                "compute_reconstruction_error must return -1.0 for nonsymmetric matrix");

    CSRMatrix A_recon_nan = A_iso;
    A_recon_nan.values[0] = std::numeric_limits<double>::quiet_NaN();
    TEST_ASSERT(chol_iso.compute_reconstruction_error(A_recon_nan) == -1.0,
                "compute_reconstruction_error must return -1.0 for matrix with NaN entry");

    CSRMatrix A_recon_inf = A_iso;
    A_recon_inf.values[0] = std::numeric_limits<double>::infinity();
    TEST_ASSERT(chol_iso.compute_reconstruction_error(A_recon_inf) == -1.0,
                "compute_reconstruction_error must return -1.0 for matrix with Inf entry");

    SparseMatrixBuilder b_zero(3, 3);
    CSRMatrix A_zero = b_zero.build();
    TEST_ASSERT(chol_iso.compute_reconstruction_error(A_zero) == -1.0,
                "compute_reconstruction_error must return -1.0 for zero-norm matrix");

    // 8. API safety: norm_inf and vector helpers
    CSRMatrix empty_mat;
    TEST_ASSERT(empty_mat.norm_inf() == 0.0, "norm_inf() on empty matrix must return 0.0");

    bool caught_null = false;
    try {
        (void)vector_dot(nullptr, b_vec.data(), 2);
    } catch (const std::invalid_argument&) {
        caught_null = true;
    }
    TEST_ASSERT(caught_null, "vector_dot must throw std::invalid_argument on null pointer");

    caught_null = false;
    try {
        (void)vector_norm2(nullptr, 2);
    } catch (const std::invalid_argument&) {
        caught_null = true;
    }
    TEST_ASSERT(caught_null, "vector_norm2 must throw std::invalid_argument on null pointer");

    caught_null = false;
    try {
        (void)vector_norm_inf(nullptr, 2);
    } catch (const std::invalid_argument&) {
        caught_null = true;
    }
    TEST_ASSERT(caught_null, "vector_norm_inf must throw std::invalid_argument on null pointer");

    bool caught_dim = false;
    try {
        std::vector<double> v1 = {1.0, 2.0};
        std::vector<double> v2 = {1.0, 2.0, 3.0};
        (void)vector_dot(v1, v2);
    } catch (const std::invalid_argument&) {
        caught_dim = true;
    }
    TEST_ASSERT(caught_dim, "vector_dot must throw std::invalid_argument on dimension mismatch");

    // 9. Wider-profile 16x16 2D 5-point Laplacian (4x4 grid)
    const std::size_t grid_n = 4;
    const std::size_t lap_dim = grid_n * grid_n; // 16
    SparseMatrixBuilder b_lap(lap_dim, lap_dim);
    auto lap_id = [grid_n](std::size_t r, std::size_t c) { return r * grid_n + c; };

    for (std::size_t r = 0; r < grid_n; ++r) {
        for (std::size_t c = 0; c < grid_n; ++c) {
            std::size_t u = lap_id(r, c);
            b_lap.add_entry(u, u, 4.0); // 5-point Laplacian diagonal
            if (r + 1 < grid_n) {
                std::size_t v = lap_id(r + 1, c);
                b_lap.add_entry(u, v, -1.0);
                b_lap.add_entry(v, u, -1.0);
            }
            if (c + 1 < grid_n) {
                std::size_t v = lap_id(r, c + 1);
                b_lap.add_entry(u, v, -1.0);
                b_lap.add_entry(v, u, -1.0);
            }
        }
    }
    CSRMatrix A_lap = b_lap.build(true);
    TEST_ASSERT(A_lap.is_valid(), "16x16 Laplacian matrix must be valid");
    TEST_ASSERT(A_lap.is_symmetric(10.0 * EPS_MACH), "16x16 Laplacian must be symmetric");
    TEST_ASSERT(A_lap.has_strictly_positive_diagonal(), "16x16 Laplacian must have positive diagonal");

    // Factor with RCM and solve
    ProfileCholesky chol_lap;
    ProfileCholesky::Status s_lap = chol_lap.factor(A_lap, true);
    TEST_ASSERT(s_lap == ProfileCholesky::Status::SUCCESS, "Profile Cholesky must succeed on 16x16 Laplacian");

    const auto& lap_rcm = chol_lap.get_rcm_result();
    CSRMatrix A_lap_perm = RCMOrdering::permute_matrix(A_lap, lap_rcm.perm, lap_rcm.inv_perm);
    double recon_lap = chol_lap.compute_reconstruction_error(A_lap_perm);
    TEST_ASSERT(recon_lap < 1e-13, "Laplacian reconstruction error must be < 1e-13 (got " + std::to_string(recon_lap) + ")");

    std::vector<double> x_lap_true(lap_dim);
    for (std::size_t i = 0; i < lap_dim; ++i) x_lap_true[i] = static_cast<double>(i + 1);
    std::vector<double> b_lap_rhs;
    A_lap.matvec(x_lap_true, b_lap_rhs);

    std::vector<double> x_lap_sol;
    bool lap_solved = chol_lap.solve(b_lap_rhs, x_lap_sol);
    TEST_ASSERT(lap_solved, "Solve on 16x16 Laplacian must succeed");

    for (std::size_t i = 0; i < lap_dim; ++i) {
        TEST_ASSERT(std::abs(x_lap_sol[i] - x_lap_true[i]) < 1e-12, 
                    "Laplacian solution component " + std::to_string(i) + " must match exact solution");
    }

    double back_lap = chol_lap.compute_backward_error(A_lap, b_lap_rhs, x_lap_sol);
    TEST_ASSERT(back_lap < 100.0 * EPS_MACH, 
                "Laplacian backward error must be < 100 * eps_mach (got " + std::to_string(back_lap) + ")");
}

// -------------------------------------------------------------
// Round 2 — Phase 2A Tests (Eigen Integration & PCG Foundation)
// -------------------------------------------------------------

void test_eigen_conversion_and_pcg_foundation() {
    // -------------------------------------------------------------
    // 1. Matrix-Vector Consistency: CSR vs Eigen RowMajor Sparse
    // -------------------------------------------------------------
    const std::size_t n1 = 4;
    SparseMatrixBuilder b1(n1, n1);
    b1.add_entry(0, 0, 4.0); b1.add_entry(0, 1, -1.0);
    b1.add_entry(1, 0, -1.0); b1.add_entry(1, 1, 4.0); b1.add_entry(1, 2, -1.0);
    b1.add_entry(2, 1, -1.0); b1.add_entry(2, 2, 4.0); b1.add_entry(2, 3, -1.0);
    b1.add_entry(3, 2, -1.0); b1.add_entry(3, 3, 4.0);
    CSRMatrix A1 = b1.build(true);
    TEST_ASSERT(A1.is_valid(), "A1 must be structurally valid");

    Eigen::SparseMatrix<double, Eigen::RowMajor, int> A1_eigen;
    double t_conv1 = 0.0;
    std::string err1;
    bool ok_conv1 = convert_csr_to_eigen(A1, A1_eigen, &t_conv1, &err1);
    TEST_ASSERT(ok_conv1, "convert_csr_to_eigen must succeed on valid CSR: " + err1);
    TEST_ASSERT(t_conv1 >= 0.0, "Conversion time must be non-negative");
    TEST_ASSERT(A1_eigen.rows() == 4 && A1_eigen.cols() == 4, "Eigen dimension must match CSR");
    TEST_ASSERT(A1_eigen.nonZeros() == static_cast<int>(A1.nnz()), "Eigen nonZeros must match CSR NNZ");

    std::vector<double> v1 = {1.5, -2.0, 3.5, 0.5};
    std::vector<double> y_csr;
    A1.matvec(v1, y_csr);

    Eigen::Map<const Eigen::VectorXd> v1_eigen(v1.data(), static_cast<Eigen::Index>(v1.size()));
    Eigen::VectorXd y_eigen = A1_eigen * v1_eigen;

    for (std::size_t i = 0; i < n1; ++i) {
        TEST_ASSERT(std::abs(y_csr[i] - y_eigen[static_cast<Eigen::Index>(i)]) < 1e-14,
                    "CSR matvec and Eigen matvec must produce identical results at component " + std::to_string(i));
    }

    // -------------------------------------------------------------
    // 2. PCG Convergence and Analytical Agreement on 1D Laplacian
    // -------------------------------------------------------------
    const std::size_t n_lap = 12;
    SparseMatrixBuilder b_lap(n_lap, n_lap);
    for (std::size_t i = 0; i < n_lap; ++i) {
        b_lap.add_entry(i, i, 4.0);
        if (i > 0) b_lap.add_entry(i, i - 1, -1.0);
        if (i + 1 < n_lap) b_lap.add_entry(i, i + 1, -1.0);
    }
    CSRMatrix A_lap = b_lap.build(true);
    TEST_ASSERT(A_lap.is_valid(), "A_lap must be valid");

    // Exact reference solution x_exact (multi-mode polynomial, requiring multiple PCG iterations)
    std::vector<double> x_exact(n_lap);
    for (std::size_t i = 0; i < n_lap; ++i) {
        x_exact[i] = static_cast<double>(i + 1) * 0.5;
    }
    std::vector<double> b_lap_rhs;
    A_lap.matvec(x_exact, b_lap_rhs);

    PCGConfig pcg_cfg;
    pcg_cfg.tolerance = 1e-6;
    pcg_cfg.max_iterations = 2000;
    pcg_cfg.check_spd = true;
    EigenPCGSolver pcg(pcg_cfg);

    std::vector<double> x_pcg;
    SolverResult res_pcg = pcg.solve(A_lap, b_lap_rhs, x_pcg);

    TEST_ASSERT(res_pcg.status == SolverStatus::SUCCESS, "PCG solve must succeed");
    TEST_ASSERT(res_pcg.relative_residual <= 1e-6, 
                "PCG relative residual must be <= 1e-6 (got " + std::to_string(res_pcg.relative_residual) + ")");
    TEST_ASSERT(res_pcg.iterations > 0 && res_pcg.iterations <= static_cast<int>(n_lap),
                "PCG iteration count must be in [1, n_lap] (got " + std::to_string(res_pcg.iterations) + ")");
    TEST_ASSERT(res_pcg.solve_time_ms >= 0.0, "Solve time must be non-negative");
    TEST_ASSERT(res_pcg.setup_time_ms >= 0.0, "Setup time must be non-negative");
    TEST_ASSERT(res_pcg.conversion_time_ms >= 0.0, "Conversion time must be non-negative");

    // Agreement with analytical solution
    for (std::size_t i = 0; i < n_lap; ++i) {
        TEST_ASSERT(std::abs(x_pcg[i] - x_exact[i]) < 1e-5,
                    "PCG solution must agree with exact analytical reference at index " + std::to_string(i));
    }

    // Direct Profile Cholesky comparison through unified IThermalSolver3D interface
    ProfileCholeskySolver chol_solver(true);
    std::vector<double> x_chol;
    SolverResult res_chol = chol_solver.solve(A_lap, b_lap_rhs, x_chol);
    TEST_ASSERT(res_chol.status == SolverStatus::SUCCESS, "Cholesky solve must succeed through IThermalSolver3D");
    TEST_ASSERT(res_chol.iterations == 0, "Cholesky direct solver must report 0 iterations");
    TEST_ASSERT(res_chol.relative_residual < 1e-12, "Cholesky relative residual must be near machine precision");

    for (std::size_t i = 0; i < n_lap; ++i) {
        TEST_ASSERT(std::abs(x_pcg[i] - x_chol[i]) < 1e-5,
                    "PCG and Cholesky solutions must agree within 1e-5 at index " + std::to_string(i));
    }

    // -------------------------------------------------------------
    // 3. Multi-Scale Invariance: 1e-12, 1.0, 1e12
    // -------------------------------------------------------------
    const double scales[] = {1e-12, 1.0, 1e12};
    for (double s : scales) {
        SparseMatrixBuilder b_scale(n_lap, n_lap);
        for (std::size_t i = 0; i < n_lap; ++i) {
            b_scale.add_entry(i, i, 4.0 * s);
            if (i > 0) b_scale.add_entry(i, i - 1, -1.0 * s);
            if (i + 1 < n_lap) b_scale.add_entry(i, i + 1, -1.0 * s);
        }
        CSRMatrix A_scale = b_scale.build(true);
        std::vector<double> b_scale_rhs(n_lap);
        for (std::size_t i = 0; i < n_lap; ++i) {
            b_scale_rhs[i] = b_lap_rhs[i] * s;
        }

        std::vector<double> x_scale;
        SolverResult res_scale = pcg.solve(A_scale, b_scale_rhs, x_scale);
        TEST_ASSERT(res_scale.status == SolverStatus::SUCCESS, 
                    "PCG must converge at scale " + std::to_string(s));
        TEST_ASSERT(res_scale.relative_residual <= 1e-6, 
                    "PCG relative residual at scale " + std::to_string(s) + " must be <= 1e-6");

        // Solution vector x is scale-invariant because (s A) x = (s b) <=> A x = b
        for (std::size_t i = 0; i < n_lap; ++i) {
            TEST_ASSERT(std::abs(x_scale[i] - x_exact[i]) < 1e-5,
                        "Solution at scale " + std::to_string(s) + " must match exact analytical reference");
        }
    }

    // -------------------------------------------------------------
    // 4. Invalid and Non-Finite Input Rejection Tests
    // -------------------------------------------------------------
    // Non-square matrix
    SparseMatrixBuilder b_rect(2, 3);
    b_rect.add_entry(0, 0, 1.0);
    b_rect.add_entry(1, 1, 1.0);
    CSRMatrix A_rect = b_rect.build();
    std::vector<double> b_rect_rhs = {1.0, 1.0};
    std::vector<double> x_fail;
    SolverResult r_rect = pcg.solve(A_rect, b_rect_rhs, x_fail);
    TEST_ASSERT(r_rect.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "PCG must reject non-square matrix");

    // Dimension mismatch
    std::vector<double> b_bad_dim = {1.0, 2.0, 3.0}; // size 3 vs matrix size 4
    SolverResult r_dim = pcg.solve(A1, b_bad_dim, x_fail);
    TEST_ASSERT(r_dim.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "PCG must reject RHS dimension mismatch");

    // Non-finite RHS (NaN, +Inf, -Inf)
    std::vector<double> b_nan = {1.0, std::numeric_limits<double>::quiet_NaN(), 3.0, 4.0};
    SolverResult r_nan = pcg.solve(A1, b_nan, x_fail);
    TEST_ASSERT(r_nan.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "PCG must reject RHS containing NaN");

    std::vector<double> b_inf = {1.0, 2.0, std::numeric_limits<double>::infinity(), 4.0};
    SolverResult r_inf = pcg.solve(A1, b_inf, x_fail);
    TEST_ASSERT(r_inf.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "PCG must reject RHS containing +Inf");

    std::vector<double> b_neg_inf = {1.0, 2.0, -std::numeric_limits<double>::infinity(), 4.0};
    SolverResult r_neg_inf = pcg.solve(A1, b_neg_inf, x_fail);
    TEST_ASSERT(r_neg_inf.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "PCG must reject RHS containing -Inf");

    // Non-finite initial guess x0
    std::vector<double> x0_nan = {0.0, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0};
    std::vector<double> b_valid = {1.0, 2.0, 3.0, 4.0};
    SolverResult r_x0_nan = pcg.solve(A1, b_valid, x_fail, &x0_nan);
    TEST_ASSERT(r_x0_nan.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "PCG must reject initial guess containing NaN");

    // Preconditioner failure on non-positive diagonal
    SparseMatrixBuilder b_neg_diag(2, 2);
    b_neg_diag.add_entry(0, 0, 4.0);
    b_neg_diag.add_entry(1, 1, -2.0); // Negative diagonal
    CSRMatrix A_neg_diag = b_neg_diag.build();
    std::vector<double> b_2 = {1.0, 1.0};
    SolverResult r_neg_diag = pcg.solve(A_neg_diag, b_2, x_fail);
    TEST_ASSERT(r_neg_diag.status == SolverStatus::FAILED_PRECONDITIONER, 
                "PCG must report FAILED_PRECONDITIONER on non-positive diagonal");

    // CSR conversion rejection on malformed matrix
    CSRMatrix A_corrupt = A1;
    A_corrupt.row_ptr[0] = 999;
    Eigen::SparseMatrix<double, Eigen::RowMajor, int> eigen_corrupt;
    bool ok_corrupt = convert_csr_to_eigen(A_corrupt, eigen_corrupt);
    TEST_ASSERT(!ok_corrupt, "convert_csr_to_eigen must reject structurally invalid CSR");

    // CSR conversion rejection on non-finite entry
    CSRMatrix A_nan_entry = A1;
    A_nan_entry.values[0] = std::numeric_limits<double>::quiet_NaN();
    bool ok_nan_entry = convert_csr_to_eigen(A_nan_entry, eigen_corrupt);
    TEST_ASSERT(!ok_nan_entry, "convert_csr_to_eigen must reject matrix with NaN value");
}

// -------------------------------------------------------------
// Round 2 — Phase 2B Tests (3D Geometry & Cell-Centred FVM)
// -------------------------------------------------------------

void test_thermal_geometry_3d_cube_and_finned_mesh_hierarchy() {
    // -------------------------------------------------------------
    // 1. Model B — Cube Hierarchy (N = 10, 20, 30)
    // -------------------------------------------------------------
    const int cube_N[] = {10, 20, 30};
    const std::size_t cube_expected_M[] = {1000, 8000, 27000};
    const std::size_t cube_expected_E[] = {2700, 22800, 78300};
    const std::size_t cube_expected_NNZ[] = {6400, 53600, 183600};
    const std::size_t cube_expected_dir[] = {200, 800, 1800};     // 2 * N^2
    const std::size_t cube_expected_neu[] = {400, 1600, 3600};     // 4 * N^2

    for (int idx = 0; idx < 3; ++idx) {
        int N = cube_N[idx];
        ThermalGeometry3D geom(ModelType::CUBE, N);
        const auto& stats = geom.get_stats();

        TEST_ASSERT(stats.active_cells == cube_expected_M[idx],
                    "Cube N=" + std::to_string(N) + " active cells must be " + 
                    std::to_string(cube_expected_M[idx]) + " (got " + std::to_string(stats.active_cells) + ")");
        TEST_ASSERT(stats.internal_faces == cube_expected_E[idx],
                    "Cube N=" + std::to_string(N) + " internal faces must be " + 
                    std::to_string(cube_expected_E[idx]) + " (got " + std::to_string(stats.internal_faces) + ")");
        TEST_ASSERT(stats.expected_nnz == cube_expected_NNZ[idx],
                    "Cube N=" + std::to_string(N) + " expected NNZ must be " + 
                    std::to_string(cube_expected_NNZ[idx]) + " (got " + std::to_string(stats.expected_nnz) + ")");
        TEST_ASSERT(stats.dirichlet_faces == cube_expected_dir[idx],
                    "Cube N=" + std::to_string(N) + " Dirichlet faces must be " + std::to_string(cube_expected_dir[idx]));
        TEST_ASSERT(stats.neumann_faces == cube_expected_neu[idx],
                    "Cube N=" + std::to_string(N) + " Neumann faces must be " + std::to_string(cube_expected_neu[idx]));
        TEST_ASSERT(stats.robin_faces == 0, "Cube must have zero Robin faces");
        TEST_ASSERT(stats.total_exposed_faces == stats.dirichlet_faces + stats.neumann_faces,
                    "Total exposed faces must equal Dirichlet + Neumann");
        TEST_ASSERT(stats.total_exposed_faces == 6 * stats.active_cells - 2 * stats.internal_faces,
                    "Boundary face conservation 6M - 2E must hold exactly");

        // Assemble FVM discrete linear system (A, b)
        CSRMatrix A;
        std::vector<double> b;
        double t_asm = 0.0;
        geom.assemble_system(A, b, &t_asm);

        std::string val_err;
        TEST_ASSERT(A.is_valid(&val_err), "Cube N=" + std::to_string(N) + " CSR must be valid: " + val_err);
        TEST_ASSERT(A.num_rows == stats.active_cells && A.num_cols == stats.active_cells,
                    "Cube matrix dimensions must match active cells");
        TEST_ASSERT(A.nnz() == stats.expected_nnz,
                    "Cube assembled NNZ must exactly equal M + 2E");
        TEST_ASSERT(A.is_symmetric(10.0 * EPS_MACH), "Cube assembled matrix must be symmetric");
        TEST_ASSERT(A.has_strictly_positive_diagonal(), "Cube matrix must have strictly positive diagonal");
        TEST_ASSERT(A.is_weakly_diagonally_dominant(10.0 * EPS_MACH), "Cube matrix must be weakly diagonally dominant");

        // Verify non-positive off-diagonals (M-matrix property) and finite RHS
        for (std::size_t u = 0; u < stats.active_cells; ++u) {
            TEST_ASSERT(std::isfinite(b[u]), "RHS entry at " + std::to_string(u) + " must be finite");
            for (std::size_t p = A.row_ptr[u]; p < A.row_ptr[u + 1]; ++p) {
                std::size_t c = static_cast<std::size_t>(A.col_idx[p]);
                TEST_ASSERT(std::isfinite(A.values[p]), "Matrix entry must be finite");
                if (c != u) {
                    TEST_ASSERT(A.values[p] <= 0.0, "Off-diagonal entry must be non-positive");
                }
            }
        }
    }

    // -------------------------------------------------------------
    // 2. Model C — Finned Heat Sink Hierarchy (N = 10, 20, 30)
    // -------------------------------------------------------------
    const int finned_N[] = {10, 20, 30};
    const std::size_t finned_expected_M[] = {520, 4160, 14040};
    const std::size_t finned_expected_E[] = {1068, 10512, 37692};
    const std::size_t finned_expected_NNZ[] = {2656, 25184, 89424};
    const std::size_t finned_expected_dir[] = {100, 400, 900};       // N^2
    const std::size_t finned_expected_neu[] = {104, 416, 936};       // 2 * 0.52 * N^2 = 1.04 N^2
    const std::size_t finned_expected_rob[] = {780, 3120, 7020};     // 6M - 2E - N_dir - N_neu

    for (int idx = 0; idx < 3; ++idx) {
        int N = finned_N[idx];
        ThermalGeometry3D geom(ModelType::FINNED_HEAT_SINK, N);
        const auto& stats = geom.get_stats();

        TEST_ASSERT(stats.active_cells == finned_expected_M[idx],
                    "Finned N=" + std::to_string(N) + " active cells must be " + 
                    std::to_string(finned_expected_M[idx]) + " (got " + std::to_string(stats.active_cells) + ")");
        TEST_ASSERT(stats.internal_faces == finned_expected_E[idx],
                    "Finned N=" + std::to_string(N) + " internal faces must be " + 
                    std::to_string(finned_expected_E[idx]) + " (got " + std::to_string(stats.internal_faces) + ")");
        TEST_ASSERT(stats.expected_nnz == finned_expected_NNZ[idx],
                    "Finned N=" + std::to_string(N) + " expected NNZ must be " + 
                    std::to_string(finned_expected_NNZ[idx]) + " (got " + std::to_string(stats.expected_nnz) + ")");
        TEST_ASSERT(stats.dirichlet_faces == finned_expected_dir[idx],
                    "Finned N=" + std::to_string(N) + " Dirichlet faces must be " + std::to_string(finned_expected_dir[idx]));
        TEST_ASSERT(stats.neumann_faces == finned_expected_neu[idx],
                    "Finned N=" + std::to_string(N) + " Neumann faces must be " + std::to_string(finned_expected_neu[idx]));
        TEST_ASSERT(stats.robin_faces == finned_expected_rob[idx],
                    "Finned N=" + std::to_string(N) + " Robin faces must be " + std::to_string(finned_expected_rob[idx]));
        TEST_ASSERT(stats.total_exposed_faces == stats.dirichlet_faces + stats.robin_faces + stats.neumann_faces,
                    "Total exposed faces must equal Dirichlet + Robin + Neumann");
        TEST_ASSERT(stats.total_exposed_faces == 6 * stats.active_cells - 2 * stats.internal_faces,
                    "Finned boundary face conservation 6M - 2E must hold exactly");

        // Verify solid volume fraction: V_solid == 0.52 * 0.1^3 = 0.00052 m^3
        TEST_ASSERT(std::abs(stats.solid_volume - 0.00052) < 1e-12,
                    "Finned solid volume must equal 0.00052 m^3");

        // Assemble FVM discrete linear system (A, b)
        CSRMatrix A;
        std::vector<double> b;
        double t_asm = 0.0;
        geom.assemble_system(A, b, &t_asm);

        std::string val_err;
        TEST_ASSERT(A.is_valid(&val_err), "Finned N=" + std::to_string(N) + " CSR must be valid: " + val_err);
        TEST_ASSERT(A.num_rows == stats.active_cells && A.num_cols == stats.active_cells,
                    "Finned matrix dimensions must match active cells");
        TEST_ASSERT(A.nnz() == stats.expected_nnz,
                    "Finned assembled NNZ must exactly equal M + 2E");
        TEST_ASSERT(A.is_symmetric(10.0 * EPS_MACH), "Finned assembled matrix must be symmetric");
        TEST_ASSERT(A.has_strictly_positive_diagonal(), "Finned matrix must have strictly positive diagonal");
        TEST_ASSERT(A.is_weakly_diagonally_dominant(10.0 * EPS_MACH), "Finned matrix must be weakly diagonally dominant");

        // Verify non-positive off-diagonals and finite RHS
        for (std::size_t u = 0; u < stats.active_cells; ++u) {
            TEST_ASSERT(std::isfinite(b[u]), "Finned RHS entry at " + std::to_string(u) + " must be finite");
            for (std::size_t p = A.row_ptr[u]; p < A.row_ptr[u + 1]; ++p) {
                std::size_t c = static_cast<std::size_t>(A.col_idx[p]);
                TEST_ASSERT(std::isfinite(A.values[p]), "Matrix entry must be finite");
                if (c != u) {
                    TEST_ASSERT(A.values[p] <= 0.0, "Finned off-diagonal entry must be non-positive");
                }
            }
        }
    }
}

void test_cube_analytical_exactness_fvm() {
    // -------------------------------------------------------------
    // Cube Model B: Solution vs Continuous Analytical Reference
    // T_exact(x) = 100.0 - 800.0*x + 500.0*x*(0.1 - x)
    // -------------------------------------------------------------
    // Solve at N = 10
    ThermalGeometry3D geom10(ModelType::CUBE, 10);
    CSRMatrix A10;
    std::vector<double> b10;
    geom10.assemble_system(A10, b10);

    PCGConfig pcg_cfg;
    pcg_cfg.tolerance = 1e-7;
    pcg_cfg.max_iterations = 2000;
    EigenPCGSolver pcg(pcg_cfg);

    std::vector<double> T10;
    SolverResult r10 = pcg.solve(A10, b10, T10);
    TEST_ASSERT(r10.status == SolverStatus::SUCCESS, "PCG solve on Cube N=10 must succeed");
    double err10 = geom10.compute_max_error_cube(T10);

    // Solve at N = 20
    ThermalGeometry3D geom20(ModelType::CUBE, 20);
    CSRMatrix A20;
    std::vector<double> b20;
    geom20.assemble_system(A20, b20);

    std::vector<double> T20;
    SolverResult r20 = pcg.solve(A20, b20, T20);
    TEST_ASSERT(r20.status == SolverStatus::SUCCESS, "PCG solve on Cube N=20 must succeed");
    double err20 = geom20.compute_max_error_cube(T20);

    // Acceptance criterion from blueprint: maximum error on N = 20 grid must be < 0.05 deg C
    TEST_ASSERT(err20 < 0.05, 
                "Cube N=20 maximum analytical error must be < 0.05 C (got " + std::to_string(err20) + " C)");

    // Discretization error convergence: error at N=20 should be significantly lower than N=10 (O(h^2))
    TEST_ASSERT(err20 < 0.35 * err10, 
                "FVM discretization error must converge as O(h^2): err20 (" + std::to_string(err20) + 
                ") vs err10 (" + std::to_string(err10) + ")");

    // Direct Profile Cholesky validation and pairwise agreement on N = 10
    ProfileCholeskySolver chol_solver(true);
    std::vector<double> T10_chol;
    SolverResult r10_chol = chol_solver.solve(A10, b10, T10_chol);
    TEST_ASSERT(r10_chol.status == SolverStatus::SUCCESS, "Cholesky solve on Cube N=10 must succeed");

    double max_diff = 0.0;
    for (std::size_t u = 0; u < geom10.get_stats().active_cells; ++u) {
        double diff = std::abs(T10[u] - T10_chol[u]);
        if (diff > max_diff) max_diff = diff;
    }
    TEST_ASSERT(max_diff < 1e-5, 
                "PCG and Cholesky solutions on Cube N=10 must agree within 1e-5 C (got " + std::to_string(max_diff) + " C)");
}

void test_finned_geometry_energy_conservation_fvm() {
    // -------------------------------------------------------------
    // Finned Heat Sink Model C: Global Thermal Energy Balance
    // Q_gen = q * V_solid == Q_base + Q_conv within 0.1% tolerance
    // -------------------------------------------------------------
    const int test_N[] = {10, 20};

    PCGConfig pcg_cfg;
    pcg_cfg.tolerance = 1e-7;
    pcg_cfg.max_iterations = 3000;
    EigenPCGSolver pcg(pcg_cfg);

    for (int N : test_N) {
        ThermalGeometry3D geom(ModelType::FINNED_HEAT_SINK, N);
        CSRMatrix A;
        std::vector<double> b;
        geom.assemble_system(A, b);

        std::vector<double> T_sol;
        SolverResult r_sol = pcg.solve(A, b, T_sol);
        TEST_ASSERT(r_sol.status == SolverStatus::SUCCESS, 
                    "PCG solve on Finned Sink N=" + std::to_string(N) + " must succeed");

        double Q_gen = 0.0, Q_base = 0.0, Q_conv = 0.0, rel_err = 0.0;
        bool balanced = geom.verify_energy_balance_finned(T_sol, Q_gen, Q_base, Q_conv, rel_err);

        TEST_ASSERT(balanced, 
                    "Finned N=" + std::to_string(N) + " thermal energy balance must be within 0.1% tolerance (got " + 
                    std::to_string(rel_err * 100.0) + "%, Q_gen=" + std::to_string(Q_gen) + " W, Q_out=" + 
                    std::to_string(Q_base + Q_conv) + " W)");
        TEST_ASSERT(std::abs(Q_gen - 5.2) < 1e-10, 
                    "Q_gen must equal 5.2 W (q=10000 * V=0.00052)");
        TEST_ASSERT(Q_conv > 0.0, "Convective heat loss Q_conv must be strictly positive");
    }
}

// -------------------------------------------------------------
// Round 2 — Phase 2C Tests (Custom SOR & Spectral Radius)
// -------------------------------------------------------------

void test_sor_convergence_and_solver_agreement() {
    // -------------------------------------------------------------
    // 1. SOR Convergence on Known 1D Laplacian (n = 10)
    // -------------------------------------------------------------
    const std::size_t n1 = 10;
    SparseMatrixBuilder b1(n1, n1);
    for (std::size_t i = 0; i < n1; ++i) {
        b1.add_entry(i, i, 2.0);
        if (i > 0) b1.add_entry(i, i - 1, -1.0);
        if (i + 1 < n1) b1.add_entry(i, i + 1, -1.0);
    }
    CSRMatrix A1 = b1.build(true);
    TEST_ASSERT(A1.is_valid(), "A1 must be structurally valid");

    std::vector<double> x_true(n1);
    for (std::size_t i = 0; i < n1; ++i) {
        x_true[i] = 1.0 + 0.5 * static_cast<double>(i + 1);
    }
    std::vector<double> b1_rhs;
    A1.matvec(x_true, b1_rhs);

    SORSolver sor1(1.25, 1e-7, 2000);
    std::vector<double> x_sol;
    SolverResult r1 = sor1.solve(A1, b1_rhs, x_sol);

    TEST_ASSERT(r1.status == SolverStatus::CONVERGED, "SOR must converge on 1D Laplacian");
    TEST_ASSERT(r1.relative_residual <= 1e-7, "SOR relative residual must be <= 1e-7 (got " + 
                std::to_string(r1.relative_residual) + ")");
    TEST_ASSERT(r1.iterations > 0, "SOR iterations must be > 0");

    for (std::size_t i = 0; i < n1; ++i) {
        TEST_ASSERT(std::abs(x_sol[i] - x_true[i]) < 1e-5, 
                    "SOR solution at component " + std::to_string(i) + " must match exact solution");
    }

    // -------------------------------------------------------------
    // 2. Multiple Matrix Scales (s = 100.0 and s = 0.01)
    // -------------------------------------------------------------
    const double scales[] = {100.0, 0.01};
    for (double s : scales) {
        SparseMatrixBuilder b_scale(n1, n1);
        for (std::size_t i = 0; i < n1; ++i) {
            b_scale.add_entry(i, i, 2.0 * s);
            if (i > 0) b_scale.add_entry(i, i - 1, -1.0 * s);
            if (i + 1 < n1) b_scale.add_entry(i, i + 1, -1.0 * s);
        }
        CSRMatrix A_scaled = b_scale.build(true);
        std::vector<double> b_scaled;
        A_scaled.matvec(x_true, b_scaled);

        SORSolver sor_s(1.25, 1e-6, 2000);
        std::vector<double> x_scaled;
        SolverResult r_s = sor_s.solve(A_scaled, b_scaled, x_scaled);

        TEST_ASSERT(r_s.status == SolverStatus::CONVERGED, 
                    "SOR must converge for scaled matrix (s=" + std::to_string(s) + ")");
        TEST_ASSERT(r_s.relative_residual <= 1e-6, 
                    "Relative residual must be <= 1e-6 at scale " + std::to_string(s));
        for (std::size_t i = 0; i < n1; ++i) {
            TEST_ASSERT(std::abs(x_scaled[i] - x_true[i]) < 1e-4, 
                        "Scaled solution component " + std::to_string(i) + " must match exact solution");
        }
    }

    // -------------------------------------------------------------
    // 3. Pairwise Agreement with Profile Cholesky and Eigen PCG on 3D Cube (N = 10, M = 1000)
    // -------------------------------------------------------------
    ThermalGeometry3D cube(ModelType::CUBE, 10);
    CSRMatrix A_cube;
    std::vector<double> b_cube;
    cube.assemble_system(A_cube, b_cube);

    ProfileCholeskySolver chol_solver(true);
    EigenPCGSolver pcg_solver;
    PCGConfig pcg_cfg;
    pcg_cfg.tolerance = 1e-8;
    pcg_cfg.max_iterations = 2000;
    pcg_solver.set_config(pcg_cfg);

    SORSolver sor_cube(1.70, 1e-8, 5000);

    // Verify polymorphic interface
    IThermalSolver3D* s_interface = &sor_cube;
    TEST_ASSERT(s_interface->get_name() == "Custom_SOR", "SORSolver must implement IThermalSolver3D::get_name");

    std::vector<double> T_chol, T_pcg, T_sor;
    SolverResult res_chol = chol_solver.solve(A_cube, b_cube, T_chol);
    SolverResult res_pcg  = pcg_solver.solve(A_cube, b_cube, T_pcg);
    SolverResult res_sor  = sor_cube.solve(A_cube, b_cube, T_sor);

    TEST_ASSERT(res_chol.status == SolverStatus::SUCCESS, "Cholesky solve must succeed on Cube N=10");
    TEST_ASSERT(res_pcg.status == SolverStatus::SUCCESS, "PCG solve must succeed on Cube N=10");
    TEST_ASSERT(res_sor.status == SolverStatus::CONVERGED, "SOR solve must converge on Cube N=10");

    double max_diff_chol_pcg = 0.0;
    double max_diff_pcg_sor  = 0.0;
    double max_diff_chol_sor = 0.0;

    for (std::size_t i = 0; i < A_cube.num_rows; ++i) {
        max_diff_chol_pcg = std::max(max_diff_chol_pcg, std::abs(T_chol[i] - T_pcg[i]));
        max_diff_pcg_sor  = std::max(max_diff_pcg_sor,  std::abs(T_pcg[i] - T_sor[i]));
        max_diff_chol_sor = std::max(max_diff_chol_sor, std::abs(T_chol[i] - T_sor[i]));
    }

    TEST_ASSERT(max_diff_chol_pcg < 1e-4, 
                "Cholesky vs PCG pairwise agreement must be < 1e-4 deg C (got " + std::to_string(max_diff_chol_pcg) + ")");
    TEST_ASSERT(max_diff_pcg_sor < 1e-4, 
                "PCG vs SOR pairwise agreement must be < 1e-4 deg C (got " + std::to_string(max_diff_pcg_sor) + ")");
    TEST_ASSERT(max_diff_chol_sor < 1e-4, 
                "Cholesky vs SOR pairwise agreement must be < 1e-4 deg C (got " + std::to_string(max_diff_chol_sor) + ")");
}

void test_sor_parameters_initial_guess_and_four_termination_categories() {
    ThermalGeometry3D cube(ModelType::CUBE, 10);
    CSRMatrix A;
    std::vector<double> b;
    cube.assemble_system(A, b);
    const std::size_t M = A.num_rows;

    std::vector<double> x_out;

    // -------------------------------------------------------------
    // 1. Valid Omega Values (omega = 1.0, 1.25, 1.5, 1.7)
    // -------------------------------------------------------------
    const double valid_omegas[] = {1.0, 1.25, 1.5, 1.7};
    for (double w : valid_omegas) {
        SORSolver sor(w, 1e-5, 3000);
        SolverResult r = sor.solve(A, b, x_out);
        TEST_ASSERT(r.status == SolverStatus::CONVERGED, 
                    "SOR must converge for valid omega=" + std::to_string(w));
        TEST_ASSERT(r.relative_residual <= 1e-5, "Relative residual must be <= 1e-5");
    }

    // -------------------------------------------------------------
    // 2. Invalid Omega Values Rejected with FAILED_INPUT_VALIDATION
    // -------------------------------------------------------------
    const double invalid_omegas[] = {
        0.0, -0.5, -2.0, 
        std::numeric_limits<double>::quiet_NaN(), 
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (double bad_w : invalid_omegas) {
        SORSolver sor(bad_w, 1e-6, 1000);
        SolverResult r = sor.solve(A, b, x_out);
        TEST_ASSERT(r.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                    "SOR must return FAILED_INPUT_VALIDATION on invalid omega " + std::to_string(bad_w));
        TEST_ASSERT(!r.error_message.empty(), "Error message must be non-empty for invalid omega");
    }

    // Invalid tolerance & max_iterations
    SORSolver sor_bad_tol(1.2, -1e-6, 1000);
    SolverResult r_bad_tol = sor_bad_tol.solve(A, b, x_out);
    TEST_ASSERT(r_bad_tol.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "Negative tolerance must be rejected with FAILED_INPUT_VALIDATION");

    SORSolver sor_bad_maxit(1.2, 1e-6, 0);
    SolverResult r_bad_maxit = sor_bad_maxit.solve(A, b, x_out);
    TEST_ASSERT(r_bad_maxit.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "max_iterations <= 0 must be rejected with FAILED_INPUT_VALIDATION");

    // -------------------------------------------------------------
    // 3. Initial Guess Validation
    // -------------------------------------------------------------
    // Size mismatch
    std::vector<double> x0_bad_size = {1.0, 2.0};
    SORSolver sor_x0(1.5, 1e-6, 1000);
    SolverResult r_x0_size = sor_x0.solve(A, b, x_out, &x0_bad_size);
    TEST_ASSERT(r_x0_size.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "Initial guess size mismatch must be rejected with FAILED_INPUT_VALIDATION");

    // Non-finite value in initial guess
    std::vector<double> x0_nan(M, 0.0);
    x0_nan[5] = std::numeric_limits<double>::quiet_NaN();
    SolverResult r_x0_nan = sor_x0.solve(A, b, x_out, &x0_nan);
    TEST_ASSERT(r_x0_nan.status == SolverStatus::FAILED_INPUT_VALIDATION, 
                "Initial guess containing NaN must be rejected with FAILED_INPUT_VALIDATION");

    // Warm start efficacy: initial guess close to converged solution
    std::vector<double> T_exact;
    SolverResult r_base = sor_x0.solve(A, b, T_exact, nullptr); // Zero initial guess
    TEST_ASSERT(r_base.status == SolverStatus::CONVERGED, "Base solve must converge");

    std::vector<double> x0_warm = T_exact;
    for (std::size_t i = 0; i < M; ++i) {
        x0_warm[i] += 0.001; // Small perturbation
    }
    std::vector<double> T_warm;
    SolverResult r_warm = sor_x0.solve(A, b, T_warm, &x0_warm);
    TEST_ASSERT(r_warm.status == SolverStatus::CONVERGED, "Warm start must converge");
    TEST_ASSERT(r_warm.iterations < r_base.iterations, 
                "Warm start must converge in strictly fewer iterations than zero initial guess: warm=" + 
                std::to_string(r_warm.iterations) + " vs zero=" + std::to_string(r_base.iterations));

    // -------------------------------------------------------------
    // 4. Four Termination Categories Verification
    // -------------------------------------------------------------
    // Category 1: CONVERGED (relative residual <= 1e-6)
    SORSolver sor_cat1(1.5, 1e-6, 5000);
    SolverResult r_cat1 = sor_cat1.solve(A, b, x_out);
    TEST_ASSERT(r_cat1.status == SolverStatus::CONVERGED, "Category 1: status must be CONVERGED");
    TEST_ASSERT(r_cat1.relative_residual <= 1e-6, "Category 1: relative residual must be <= 1e-6");

    // Category 2: DIVERGED_RESIDUAL (relative residual > 1e4 with omega = 2.1)
    // Verified empirical crossing: omega = 2.1 crosses 1e4 at iteration 91 on Model B N=10 cube with zero initial guess
    SORSolver sor_cat2(2.1, 1e-6, 150);
    SolverResult r_cat2 = sor_cat2.solve(A, b, x_out);
    TEST_ASSERT(r_cat2.status == SolverStatus::DIVERGED_RESIDUAL, 
                "Category 2: status must be DIVERGED_RESIDUAL for omega=2.1");
    TEST_ASSERT(r_cat2.relative_residual > 1e4, 
                "Category 2: relative residual must exceed 1e4 (got " + std::to_string(r_cat2.relative_residual) + ")");
    TEST_ASSERT(r_cat2.iterations == 91, 
                "Category 2: verified empirical divergence threshold crossing must occur at iteration 91 (got " + 
                std::to_string(r_cat2.iterations) + ")");

    // Category 3: DIVERGED_NUMERICAL (non-finite state encountered during solve)
    // Construct system whose iteration rapidly overflows to Infinity
    SparseMatrixBuilder b_bad(2, 2);
    b_bad.add_entry(0, 0, 1.0);
    b_bad.add_entry(0, 1, -1e200);
    b_bad.add_entry(1, 0, -1e200);
    b_bad.add_entry(1, 1, 1.0);
    CSRMatrix A_num_bad = b_bad.build(true);
    std::vector<double> b_num_bad = {1.0, 1.0};
    SORSolver sor_cat3(1.5, 1e-6, 100);
    SolverResult r_cat3 = sor_cat3.solve(A_num_bad, b_num_bad, x_out);
    TEST_ASSERT(r_cat3.status == SolverStatus::DIVERGED_NUMERICAL,
                "Category 3: severe numerical breakdown must trigger DIVERGED_NUMERICAL");
    TEST_ASSERT(r_cat3.error_message.find("DIVERGED_NUMERICAL") != std::string::npos,
                "Category 3: error message must report DIVERGED_NUMERICAL");

    // Category 4: MAX_ITERATIONS_REACHED (iteration limit reached without convergence or divergence)
    SORSolver sor_cat4(1.1, 1e-6, 5); // 5 iterations is insufficient for 1e-6 from zero guess
    SolverResult r_cat4 = sor_cat4.solve(A, b, x_out);
    TEST_ASSERT(r_cat4.status == SolverStatus::MAX_ITERATIONS_REACHED, 
                "Category 4: status must be MAX_ITERATIONS_REACHED");
    TEST_ASSERT(r_cat4.iterations == 5, "Category 4: iteration count must match max_iterations (5)");
    TEST_ASSERT(r_cat4.relative_residual > 1e-6 && r_cat4.relative_residual <= 1e4, 
                "Category 4: relative residual must remain strictly bounded: 1e-6 < rel_res <= 1e4 (got " + 
                std::to_string(r_cat4.relative_residual) + ")");
}

void test_spectral_radius_estimation_and_omega_sweep() {
    // -------------------------------------------------------------
    // 1. Spectral-Radius Estimation on Known 4x4 Tridiagonal Matrix
    // -------------------------------------------------------------
    // Standard 1D tridiagonal Laplacian: diagonal = 2, off-diagonal = -1
    // Exact Jacobi iteration matrix eigenvalues: cos(j * pi / (n + 1))
    // For n = 4: rho(B) = cos(pi / 5) = (1 + sqrt(5)) / 4 = 0.80901699437
    const int n = 4;
    SparseMatrixBuilder b4(n, n);
    for (int i = 0; i < n; ++i) {
        b4.add_entry(i, i, 2.0);
        if (i > 0) b4.add_entry(i, i - 1, -1.0);
        if (i + 1 < n) b4.add_entry(i, i + 1, -1.0);
    }
    CSRMatrix A4 = b4.build(true);
    TEST_ASSERT(A4.is_valid(), "A4 must be structurally valid");

    constexpr double kPi = 3.141592653589793238462643383279502884;
    const double exact_rho = std::cos(kPi / 5.0);

    SpectralRadiusResult spec4 = estimate_spectral_radius_jacobi(A4, 1e-5, 1000);
    TEST_ASSERT(spec4.status == SpectralRadiusStatus::CONVERGED, 
                "Power iteration on 4x4 tridiagonal must converge");
    TEST_ASSERT(spec4.relative_residual <= 1e-5, 
                "Relative eigenpair residual must be <= 1e-5 (got " + std::to_string(spec4.relative_residual) + ")");
    TEST_ASSERT(std::abs(spec4.spectral_radius - exact_rho) < 1e-4, 
                "Estimated spectral radius (" + std::to_string(spec4.spectral_radius) + 
                ") must match exact analytical rho (" + std::to_string(exact_rho) + ") within 1e-4");
    TEST_ASSERT(std::abs(std::sqrt(spec4.dominant_eigenvalue_c2) - spec4.spectral_radius) < 1e-12, 
                "rho(B) must equal sqrt(lambda_max(C^2))");

    // -------------------------------------------------------------
    // 2. Spectral-Radius Estimation on 3D Cube (N = 10, M = 1000)
    // -------------------------------------------------------------
    ThermalGeometry3D cube(ModelType::CUBE, 10);
    CSRMatrix A_cube;
    std::vector<double> b_cube;
    cube.assemble_system(A_cube, b_cube);

    SpectralRadiusResult spec_cube = estimate_spectral_radius_jacobi(A_cube, 1e-5, 1000);
    TEST_ASSERT(spec_cube.status == SpectralRadiusStatus::CONVERGED, 
                "Power iteration on Cube N=10 must converge");
    TEST_ASSERT(spec_cube.relative_residual <= 1e-5, 
                "Cube relative residual must be <= 1e-5 (got " + std::to_string(spec_cube.relative_residual) + ")");
    TEST_ASSERT(spec_cube.spectral_radius > 0.90 && spec_cube.spectral_radius < 1.0, 
                "Cube discrete spectral radius must be in (0.90, 1.00) (got " + 
                std::to_string(spec_cube.spectral_radius) + ")");

    // Distinct distinction: estimated discrete spectral radius vs idealized analytical reference
    double rho_ideal = compute_idealized_cube_spectral_radius(10);
    TEST_ASSERT(std::abs(spec_cube.spectral_radius - rho_ideal) > 1e-4, 
                "Discrete estimated spectral radius must be distinct from idealized continuous formula: discrete=" + 
                std::to_string(spec_cube.spectral_radius) + " vs ideal=" + std::to_string(rho_ideal));

    // Iteration limit behavior: NOT_CONVERGED
    SpectralRadiusResult spec_truncated = estimate_spectral_radius_jacobi(A_cube, 1e-5, 2);
    TEST_ASSERT(spec_truncated.status == SpectralRadiusStatus::NOT_CONVERGED, 
                "Truncated power iteration must report NOT_CONVERGED");
    TEST_ASSERT(spec_truncated.iterations == 2, "Truncated run must stop at max_iterations (2)");
    TEST_ASSERT(spec_truncated.error_message.find("max iterations") != std::string::npos, 
                "Error message must report max iterations reached");

    // Deterministic reproducibility: two independent runs must produce bitwise identical results
    SpectralRadiusResult spec_cube_dup = estimate_spectral_radius_jacobi(A_cube, 1e-5, 1000);
    TEST_ASSERT(spec_cube.spectral_radius == spec_cube_dup.spectral_radius, 
                "Power iteration with deterministic initial vector must be bitwise reproducible");
    TEST_ASSERT(spec_cube.iterations == spec_cube_dup.iterations, 
                "Iteration count must be identical between runs");

    // -------------------------------------------------------------
    // 3. Theoretical Optimal Omega Formula
    // -------------------------------------------------------------
    double opt_w = compute_optimal_omega_from_spectral_radius(spec_cube.spectral_radius);
    TEST_ASSERT(opt_w > 1.0 && opt_w < 2.0, "Optimal omega must be in (1.0, 2.0)");

    bool caught_bad_rho = false;
    try {
        (void)compute_optimal_omega_from_spectral_radius(1.0); // rho = 1.0 invalid
    } catch (const std::invalid_argument&) {
        caught_bad_rho = true;
    }
    TEST_ASSERT(caught_bad_rho, "compute_optimal_omega_from_spectral_radius must throw for rho >= 1.0");

    // -------------------------------------------------------------
    // 4. Empirical SOR Omega Sweep and Median Verification
    // -------------------------------------------------------------
    OmegaSweepResult sweep = run_sor_omega_sweep(A_cube, b_cube, 1e-6, 10000, 5, 10);
    TEST_ASSERT(sweep.points.size() == 18, 
                "Omega sweep must evaluate exactly 18 points (omega = 1.10 .. 1.95 with step 0.05)");

    for (const auto& pt : sweep.points) {
        TEST_ASSERT(pt.runtimes_ms.size() == 5, "Each sweep point must have 5 timing samples");
        TEST_ASSERT(pt.median_runtime_ms > 0.0, "Median runtime must be strictly positive");
        // Verify median calculation: middle element of sorted 5 runtimes
        std::vector<double> sorted = pt.runtimes_ms;
        std::sort(sorted.begin(), sorted.end());
        TEST_ASSERT(pt.median_runtime_ms == sorted[2], "Median must equal middle sample of sorted array");
    }

    // Verify distinct values: omega_iter, omega_time, and omega_ideal
    TEST_ASSERT(sweep.omega_iter >= 1.10 && sweep.omega_iter <= 1.95, 
                "omega_iter must be in [1.10, 1.95] (got " + std::to_string(sweep.omega_iter) + ")");
    TEST_ASSERT(sweep.omega_time >= 1.10 && sweep.omega_time <= 1.95, 
                "omega_time must be in [1.10, 1.95] (got " + std::to_string(sweep.omega_time) + ")");
    TEST_ASSERT(sweep.omega_ideal >= 1.10 && sweep.omega_ideal <= 1.95, 
                "omega_ideal must be in [1.10, 1.95] (got " + std::to_string(sweep.omega_ideal) + ")");
    TEST_ASSERT(sweep.min_iterations > 0, "Minimum iterations must be > 0");
    TEST_ASSERT(sweep.min_median_runtime_ms > 0.0, "Minimum median runtime must be > 0.0");
}

// -------------------------------------------------------------
// Round 2 — Phase 2D Tests (Comparative Benchmarking Pipeline)
// -------------------------------------------------------------
void test_comparative_benchmark_3d_pipeline() {
    // 1. Run pipeline on N=10 for Cube and Finned Sink with 3 repeats, non-verbose
    ComparativeBenchmarkSuite suite = run_comparative_benchmark_suite({10}, 3, false, 1e-8);

    // 2 geometries * 1 resolution * 3 solvers = 6 case results
    TEST_ASSERT(suite.cases.size() == 6, "Must have exactly 6 case results for {10} with 3 solvers");
    TEST_ASSERT(suite.sor_sweeps.size() == 2, "Must have 2 SOR sweep records (Cube and Finned Sink)");

    for (const auto& c : suite.cases) {
        // 1 warmup + 3 measured = 4 runs
        TEST_ASSERT(c.runs.size() == 4, "Case must contain exactly 4 runs (1 warmup + 3 measured)");
        TEST_ASSERT(c.runs[0].run_type == "warmup", "Run 0 must be warmup");
        TEST_ASSERT(c.runs[0].run_index == 0, "Run 0 index must be 0");

        // Verify measured runs
        for (int rep = 1; rep <= 3; ++rep) {
            const auto& r = c.runs[rep];
            TEST_ASSERT(r.run_type == "measured", "Runs 1..3 must be measured");
            TEST_ASSERT(r.run_index == rep, "Run index must match repetition");
            TEST_ASSERT(r.timings.t_sol_ms >= 0.0 && std::isfinite(r.timings.t_sol_ms), 
                        "Solve time must be non-negative and finite");
            TEST_ASSERT(r.timings.t_asm_ms > 0.0, "Assembly time must be strictly positive");
            TEST_ASSERT(std::isfinite(r.relative_residual), "Relative residual must be finite");
            TEST_ASSERT(r.status == SolverStatus::SUCCESS || r.status == SolverStatus::CONVERGED, 
                        "Status must be success or converged");
        }

        // Verify median record
        TEST_ASSERT(c.median_record.run_type == "median", "Median record run_type must be 'median'");
        TEST_ASSERT(c.median_record.run_index == -1, "Median record run_index must be -1");
        TEST_ASSERT(c.median_record.timings.t_sol_ms >= 0.0, "Median solve time must be non-negative");

        // Verify memory metrics
        TEST_ASSERT(c.median_record.memory.active_dofs_M > 0, "Active DOFs must be > 0");
        TEST_ASSERT(c.median_record.memory.matrix_nnz > 0, "Matrix NNZ must be > 0");
        TEST_ASSERT(c.median_record.memory.csr_allocated_bytes > 0, "CSR allocated bytes must be > 0");
        TEST_ASSERT(c.median_record.memory.process_working_set_bytes > 0, "Process working set must be > 0");

        if (c.solver_name == "Profile_Cholesky_RCM") {
            TEST_ASSERT(c.median_record.iterations_str == "N/A", "Direct solver iterations must be 'N/A'");
            TEST_ASSERT(c.median_record.iterations == 0, "Direct solver iterations int must be 0");
            TEST_ASSERT(c.median_record.memory.factor_envelope_capacity > 0, "Factor envelope must be > 0");
            TEST_ASSERT(c.median_record.memory.factor_allocated_bytes > 0, "Factor allocated bytes must be > 0");
            TEST_ASSERT(c.median_record.memory.factor_fill_ratio > 0.0, "Factor fill ratio must be > 0");
            TEST_ASSERT(c.median_record.timings.t_rcm_ms >= 0.0, "t_rcm_ms must be non-negative");
            TEST_ASSERT(c.median_record.timings.t_env_ms >= 0.0, "t_env_ms must be non-negative");
            TEST_ASSERT(c.median_record.timings.t_fac_ms >= 0.0, "t_fac_ms must be non-negative");
        } else {
            TEST_ASSERT(c.median_record.iterations_str != "N/A", "Iterative solver iterations must not be 'N/A'");
            TEST_ASSERT(c.median_record.iterations > 0, "Iterative solver iterations must be > 0");
            TEST_ASSERT(c.median_record.memory.iterative_buffer_bytes > 0, "Iterative buffer bytes must be > 0");
        }

        if (c.solver_name == "Eigen_PCG_Jacobi") {
            TEST_ASSERT(c.median_record.timings.t_convert_ms >= 0.0, "t_convert_ms must be non-negative");
            TEST_ASSERT(c.median_record.timings.t_setup_ms >= 0.0, "t_setup_ms must be non-negative");
        }
    }

    // 2. Pairwise agreement threshold
    TEST_ASSERT(suite.max_pairwise_diff_C < 1e-4, 
                "Pairwise solver agreement must be < 1e-4 deg C (got " + std::to_string(suite.max_pairwise_diff_C) + ")");

    // 3. All measurements are finite
    TEST_ASSERT(suite.all_valid_finite, "All measurements must be finite");

    // 4. Test file export
    std::string test_dir = "test_results_bench3d";
    bool export_ok = suite.export_all(test_dir);
    TEST_ASSERT(export_ok, "export_all must succeed");

    TEST_ASSERT(std::filesystem::exists(test_dir + "/benchmark_solvers_3d.csv"), "Detailed CSV must exist");
    TEST_ASSERT(std::filesystem::exists(test_dir + "/benchmark_summary_3d.csv"), "Summary CSV must exist");
    TEST_ASSERT(std::filesystem::exists(test_dir + "/benchmark_sor_sweep_3d.csv"), "SOR sweep CSV must exist");
    TEST_ASSERT(std::filesystem::exists(test_dir + "/benchmark_metadata_3d.json"), "Metadata JSON must exist");

    // Check non-empty files
    TEST_ASSERT(std::filesystem::file_size(test_dir + "/benchmark_solvers_3d.csv") > 100, "Detailed CSV must be non-empty");
    TEST_ASSERT(std::filesystem::file_size(test_dir + "/benchmark_summary_3d.csv") > 100, "Summary CSV must be non-empty");
    TEST_ASSERT(std::filesystem::file_size(test_dir + "/benchmark_sor_sweep_3d.csv") > 100, "SOR sweep CSV must be non-empty");
    TEST_ASSERT(std::filesystem::file_size(test_dir + "/benchmark_metadata_3d.json") > 50, "Metadata JSON must be non-empty");

    // Cleanup test artifacts
    std::filesystem::remove_all(test_dir);
}

// -------------------------------------------------------------
// Round 2 — Phase 3B Tests (3D Thermal REST API & Concurrency)
// -------------------------------------------------------------

void test_thermal3d_api_models_endpoint() {
    int test_port = 9883;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for models endpoint test");
    int port = server.get_actual_port();

    std::string resp = http_get_sync("127.0.0.1", port, "/api/thermal3d/models");
    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "GET /api/thermal3d/models must return 200 OK");
    TEST_ASSERT(resp.find("\"id\":\"CUBE\"") != std::string::npos, "Response must include CUBE model");
    TEST_ASSERT(resp.find("\"id\":\"FINNED_SINK\"") != std::string::npos, "Response must include FINNED_SINK model");

    // Verify verified blueprint mesh statistics across all 6 configurations:
    // Cube N=10: M=1000, NNZ=6400, grid=1000
    TEST_ASSERT(resp.find("\"N\":10,\"grid_cells\":1000,\"active_cells_M\":1000,\"matrix_nnz\":6400") != std::string::npos,
                "Cube N=10 mesh stats must match verified blueprint: M=1000, NNZ=6400");
    // Finned Sink N=10: M=520, NNZ=2656, grid=1000
    TEST_ASSERT(resp.find("\"N\":10,\"grid_cells\":1000,\"active_cells_M\":520,\"matrix_nnz\":2656") != std::string::npos,
                "Finned Sink N=10 mesh stats must match verified blueprint: M=520, NNZ=2656");
    // Cube N=20: M=8000, NNZ=53600, grid=8000
    TEST_ASSERT(resp.find("\"N\":20,\"grid_cells\":8000,\"active_cells_M\":8000,\"matrix_nnz\":53600") != std::string::npos,
                "Cube N=20 mesh stats must match verified blueprint: M=8000, NNZ=53600");
    // Finned Sink N=20: M=4160, NNZ=25184, grid=8000
    TEST_ASSERT(resp.find("\"N\":20,\"grid_cells\":8000,\"active_cells_M\":4160,\"matrix_nnz\":25184") != std::string::npos,
                "Finned Sink N=20 mesh stats must match verified blueprint: M=4160, NNZ=25184");
    // Cube N=30: M=27000, NNZ=183600, grid=27000
    TEST_ASSERT(resp.find("\"N\":30,\"grid_cells\":27000,\"active_cells_M\":27000,\"matrix_nnz\":183600") != std::string::npos,
                "Cube N=30 mesh stats must match verified blueprint: M=27000, NNZ=183600");
    // Finned Sink N=30: M=14040, NNZ=89424, grid=27000
    TEST_ASSERT(resp.find("\"N\":30,\"grid_cells\":27000,\"active_cells_M\":14040,\"matrix_nnz\":89424") != std::string::npos,
                "Finned Sink N=30 mesh stats must match verified blueprint: M=14040, NNZ=89424");

    server.stop();
}

void test_thermal3d_api_config_endpoint() {
    int test_port = 9884;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for config endpoint test");
    int port = server.get_actual_port();

    std::string resp = http_get_sync("127.0.0.1", port, "/api/thermal3d/config");
    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "GET /api/thermal3d/config must return 200 OK");
    TEST_ASSERT(resp.find("\"physical_properties_fixed\":true") != std::string::npos,
                "Physical properties must be flagged as fixed in Release 1");
    TEST_ASSERT(resp.find("\"thermal_conductivity_W_mK\":10") != std::string::npos, "Conductivity must be 10");
    TEST_ASSERT(resp.find("\"volumetric_heat_gen_W_m3\":10000") != std::string::npos, "Heat gen must be 10000");
    TEST_ASSERT(resp.find("\"heat_transfer_coeff_W_m2K\":50") != std::string::npos, "Convection coeff must be 50");
    TEST_ASSERT(resp.find("\"ambient_temp_degC\":25") != std::string::npos, "Ambient temp must be 25");
    TEST_ASSERT(resp.find("\"hot_temp_degC\":100") != std::string::npos, "Hot temp must be 100");
    TEST_ASSERT(resp.find("\"cold_temp_degC\":20") != std::string::npos, "Cold temp must be 20");

    TEST_ASSERT(resp.find("\"id\":\"PROFILE_CHOLESKY\"") != std::string::npos, "Solver PROFILE_CHOLESKY must be listed");
    TEST_ASSERT(resp.find("\"id\":\"PCG\"") != std::string::npos, "Solver PCG must be listed");
    TEST_ASSERT(resp.find("\"id\":\"SOR\"") != std::string::npos, "Solver SOR must be listed");
    TEST_ASSERT(resp.find("\"default_tolerance\":1e-8") != std::string::npos, "PCG/SOR default tolerance must be 1e-8");

    server.stop();
}

void test_thermal3d_api_health_endpoint() {
    int test_port = 9885;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for health endpoint test");
    int port = server.get_actual_port();

    std::string resp = http_get_sync("127.0.0.1", port, "/api/thermal3d/health");
    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "GET /api/thermal3d/health must return 200 OK");
    TEST_ASSERT(resp.find("\"status\":\"ONLINE\"") != std::string::npos, "Status must be ONLINE");
    TEST_ASSERT(resp.find("\"eigen_version\":\"3.4.0\"") != std::string::npos, "Eigen version must be 3.4.0");
    TEST_ASSERT(resp.find("\"solve_in_progress\":false") != std::string::npos, "solve_in_progress must be false when idle");

    server.stop();
}

void test_thermal3d_api_solve_cube_cholesky_success() {
    int test_port = 9886;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for Cholesky solve test");
    int port = server.get_actual_port();

    std::string req = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"PROFILE_CHOLESKY\"}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "Solve must return 200 OK");
    TEST_ASSERT(resp.find("\"status\":\"SUCCESS\"") != std::string::npos, "Solver status must be SUCCESS");
    TEST_ASSERT(resp.find("\"usable_field\":true") != std::string::npos, "usable_field must be true on success");
    TEST_ASSERT(resp.find("\"iterations\":\"N/A\"") != std::string::npos, "Direct solver iterations must be 'N/A'");
    TEST_ASSERT(resp.find("\"initial_residual\":null") != std::string::npos, "Direct solver initial residual must be null");
    TEST_ASSERT(resp.find("\"active_cells_M\":1000") != std::string::npos, "Active cells M must be 1000");
    TEST_ASSERT(resp.find("\"matrix_nnz\":6400") != std::string::npos, "Matrix NNZ must be 6400");

    // Analytical error must be present for Cube and < 0.05 deg C
    TEST_ASSERT(resp.find("\"max_analytical_error_degC\":") != std::string::npos, "Cube solve must report analytical error");
    TEST_ASSERT(resp.find("\"energy_balance_rel_error\":null") != std::string::npos, "Cube solve must report null energy balance error");

    server.stop();
}

void test_thermal3d_api_solve_finned_pcg_success() {
    int test_port = 9887;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for Finned PCG test");
    int port = server.get_actual_port();

    // 1. Solve with omitted solver_options (verifies default 1e-8 tolerance is active)
    std::string req = "{\"model\":\"FINNED_SINK\",\"resolution\":10,\"solver\":\"PCG\"}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "PCG default solve must return 200 OK");
    TEST_ASSERT(resp.find("\"status\":\"CONVERGED\"") != std::string::npos, "PCG default solve must return CONVERGED");
    TEST_ASSERT(resp.find("\"usable_field\":true") != std::string::npos, "usable_field must be true on CONVERGED");
    TEST_ASSERT(resp.find("\"active_cells_M\":520") != std::string::npos, "Finned Sink active cells M must be 520");
    TEST_ASSERT(resp.find("\"matrix_nnz\":2656") != std::string::npos, "Finned Sink matrix NNZ must be 2656");
    TEST_ASSERT(resp.find("\"max_analytical_error_degC\":null") != std::string::npos, "Finned Sink must have null analytical error");
    TEST_ASSERT(resp.find("\"energy_balance_rel_error\":") != std::string::npos, "Finned Sink must report energy balance error");

    // 2. Solve with explicitly supplied tolerance (verifies custom tolerance is respected)
    std::string req_custom = "{\"model\":\"FINNED_SINK\",\"resolution\":10,\"solver\":\"PCG\",\"solver_options\":{\"tolerance\":1e-4,\"max_iterations\":5000}}";
    std::string resp_custom = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req_custom, "Content-Type: application/json\r\n");
    TEST_ASSERT(resp_custom.find("200 OK") != std::string::npos, "PCG custom solve must return 200 OK");
    TEST_ASSERT(resp_custom.find("\"status\":\"CONVERGED\"") != std::string::npos, "PCG custom solve must return CONVERGED");

    server.stop();
}

void test_thermal3d_api_solve_cube_sor_convergence() {
    int test_port = 9888;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for SOR solve test");
    int port = server.get_actual_port();

    // 1. Solve with omitted solver_options (verifies default 1e-8 tolerance is active)
    std::string req = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"SOR\"}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "SOR default solve must return 200 OK");
    TEST_ASSERT(resp.find("\"status\":\"CONVERGED\"") != std::string::npos, "SOR default solve must converge");
    TEST_ASSERT(resp.find("\"usable_field\":true") != std::string::npos, "usable_field must be true on CONVERGED");

    // 2. Solve with explicitly supplied tolerance (verifies custom tolerance is respected)
    std::string req_custom = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"SOR\",\"solver_options\":{\"omega\":1.0,\"tolerance\":1e-5,\"max_iterations\":10000}}";
    std::string resp_custom = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req_custom, "Content-Type: application/json\r\n");
    TEST_ASSERT(resp_custom.find("200 OK") != std::string::npos, "SOR custom solve must return 200 OK");
    TEST_ASSERT(resp_custom.find("\"status\":\"CONVERGED\"") != std::string::npos, "SOR custom solve must converge");

    server.stop();
}

void test_thermal3d_api_unsupported_model_rejection_400() {
    int test_port = 9889;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for unsupported model test");
    int port = server.get_actual_port();

    std::string req = "{\"model\":\"CYLINDER\",\"resolution\":10,\"solver\":\"PCG\"}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("400 Bad Request") != std::string::npos, "Invalid model must return 400 Bad Request");
    TEST_ASSERT(resp.find("Invalid model") != std::string::npos, "Response must describe invalid model");

    server.stop();
}

void test_thermal3d_api_unsupported_resolution_rejection_400() {
    int test_port = 9890;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for resolution rejection test");
    int port = server.get_actual_port();

    std::string req1 = "{\"model\":\"CUBE\",\"resolution\":15,\"solver\":\"PCG\"}";
    std::string r1 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req1, "Content-Type: application/json\r\n");
    TEST_ASSERT(r1.find("400 Bad Request") != std::string::npos, "N=15 must return 400 Bad Request");
    TEST_ASSERT(r1.find("Invalid resolution") != std::string::npos, "Error must state invalid resolution");

    std::string req2 = "{\"model\":\"CUBE\",\"resolution\":0,\"solver\":\"PCG\"}";
    std::string r2 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req2, "Content-Type: application/json\r\n");
    TEST_ASSERT(r2.find("400 Bad Request") != std::string::npos, "N=0 must return 400 Bad Request");

    std::string req3 = "{\"model\":\"CUBE\",\"resolution\":40,\"solver\":\"PCG\"}";
    std::string r3 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req3, "Content-Type: application/json\r\n");
    TEST_ASSERT(r3.find("400 Bad Request") != std::string::npos, "N=40 must return 400 Bad Request");

    server.stop();
}

void test_thermal3d_api_invalid_solver_rejection_400() {
    int test_port = 9891;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for invalid solver test");
    int port = server.get_actual_port();

    std::string req = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"MINRES\"}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("400 Bad Request") != std::string::npos, "Invalid solver must return 400 Bad Request");
    TEST_ASSERT(resp.find("Invalid solver") != std::string::npos, "Error must state invalid solver");

    server.stop();
}

void test_thermal3d_api_invalid_options_and_malformed_json_400() {
    int test_port = 9892;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for options rejection test");
    int port = server.get_actual_port();

    // 1. Malformed JSON syntax
    std::string r1 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", "{\"model\": \"CUBE\",", "Content-Type: application/json\r\n");
    TEST_ASSERT(r1.find("400 Bad Request") != std::string::npos, "Malformed JSON syntax must return 400");
    TEST_ASSERT(r1.find("Malformed JSON") != std::string::npos, "Error must indicate malformed JSON");

    // 2. Negative tolerance
    std::string r2 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", 
                                    "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"PCG\",\"solver_options\":{\"tolerance\":-1e-6}}", 
                                    "Content-Type: application/json\r\n");
    TEST_ASSERT(r2.find("400 Bad Request") != std::string::npos, "Negative tolerance must return 400");

    // 3. Omega out of range (omega <= 0.0 or >= 2.0)
    std::string r3 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", 
                                    "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"SOR\",\"solver_options\":{\"omega\":0.0}}", 
                                    "Content-Type: application/json\r\n");
    TEST_ASSERT(r3.find("400 Bad Request") != std::string::npos, "omega=0.0 must return 400");

    std::string r4 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", 
                                    "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"SOR\",\"solver_options\":{\"omega\":2.5}}", 
                                    "Content-Type: application/json\r\n");
    TEST_ASSERT(r4.find("400 Bad Request") != std::string::npos, "omega=2.5 must return 400");

    // 4. Unknown root field
    std::string r5 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", 
                                    "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"PCG\",\"unexpected_field\":123}", 
                                    "Content-Type: application/json\r\n");
    TEST_ASSERT(r5.find("400 Bad Request") != std::string::npos, "Unknown root field must return 400");

    // 5. Method not allowed (GET /api/thermal3d/solve)
    std::string r6 = http_get_sync("127.0.0.1", port, "/api/thermal3d/solve");
    TEST_ASSERT(r6.find("405 Method Not Allowed") != std::string::npos, "GET on solve endpoint must return 405 Method Not Allowed");

    server.stop();
}

void test_thermal3d_api_field_arrays_and_statistics() {
    int test_port = 9893;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for thermal field test");
    int port = server.get_actual_port();

    std::string req = "{\"model\":\"FINNED_SINK\",\"resolution\":10,\"solver\":\"PCG\"}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "Solve must return 200 OK");
    TEST_ASSERT(resp.find("\"count\":520") != std::string::npos, "thermal_field.count must equal active cells M (520)");

    // Find temperatures array and coordinates array in response
    size_t coord_pos = resp.find("\"coordinates\":[");
    TEST_ASSERT(coord_pos != std::string::npos, "Response must contain coordinates array");
    size_t temp_pos = resp.find("\"temperatures\":[");
    TEST_ASSERT(temp_pos != std::string::npos, "Response must contain temperatures array");

    // Validate that minimum, maximum, and average temperatures are physically consistent
    TEST_ASSERT(resp.find("\"min_temp_degC\":") != std::string::npos, "Response must report min_temp_degC");
    TEST_ASSERT(resp.find("\"max_temp_degC\":") != std::string::npos, "Response must report max_temp_degC");
    TEST_ASSERT(resp.find("\"avg_temp_degC\":") != std::string::npos, "Response must report avg_temp_degC");
    TEST_ASSERT(resp.find("\"total_time_ms\":") != std::string::npos, "Response must report total_time_ms");

    server.stop();
}

void test_thermal3d_api_solver_failure_usable_field_false() {
    int test_port = 9894;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for solver failure status test");
    int port = server.get_actual_port();

    // Trigger MAX_ITERATIONS_REACHED by restricting max_iterations to 1 on Cube
    std::string req = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"SOR\",\"solver_options\":{\"omega\":1.0,\"max_iterations\":1}}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "HTTP transaction must return 200 OK");
    TEST_ASSERT(resp.find("\"status\":\"MAX_ITERATIONS_REACHED\"") != std::string::npos,
                "Solver status must be MAX_ITERATIONS_REACHED");
    TEST_ASSERT(resp.find("\"usable_field\":false") != std::string::npos,
                "usable_field must be strictly false on un-converged solve");
    TEST_ASSERT(resp.find("\"coordinates\":[]") != std::string::npos,
                "Unusable field must serialize coordinates as empty array");
    TEST_ASSERT(resp.find("\"temperatures\":[]") != std::string::npos,
                "Unusable field must serialize temperatures as empty array");
    TEST_ASSERT(resp.find("\"count\":0") != std::string::npos,
                "Unusable field must report count: 0");
    TEST_ASSERT(resp.find("\"min_temp_degC\":null") != std::string::npos,
                "Unusable field must report min_temp_degC: null");
    TEST_ASSERT(resp.find("\"max_temp_degC\":null") != std::string::npos,
                "Unusable field must report max_temp_degC: null");
    TEST_ASSERT(resp.find("\"avg_temp_degC\":null") != std::string::npos,
                "Unusable field must report avg_temp_degC: null");

    server.stop();
}

void test_thermal3d_api_deterministic_concurrency_guard_409() {
    int test_port = 9895;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for concurrency test");
    int port = server.get_actual_port();

    std::promise<void> guard_acquired_promise;
    std::future<void> guard_acquired_future = guard_acquired_promise.get_future();

    std::promise<void> release_solve_promise;
    std::future<void> release_solve_future = release_solve_promise.get_future();

    std::atomic<bool> hook_entered{false};

    // Register test-only synchronization hook: invoked while holding the atomic guard
    HttpServer::set_thermal3d_test_hook([&]() {
        hook_entered.store(true);
        guard_acquired_promise.set_value();
        // Wait until released with a 5-second timeout fail-safe
        auto status = release_solve_future.wait_for(std::chrono::seconds(5));
        if (status == std::future_status::timeout) {
            std::cerr << "  [FAIL] Concurrency test hook timed out waiting for release!\n";
        }
    });

    std::string solve_req = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"PROFILE_CHOLESKY\"}";
    std::string resp1;

    // Launch Thread 1: Acquires guard and pauses inside the controlled hook
    std::thread t1([&]() {
        resp1 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", solve_req, "Content-Type: application/json\r\n");
    });

    // Wait until Thread 1 has acquired the guard deterministically
    auto acquired_status = guard_acquired_future.wait_for(std::chrono::seconds(5));
    TEST_ASSERT(acquired_status == std::future_status::ready, "Thread 1 must acquire guard within 5 seconds");
    TEST_ASSERT(hook_entered.load(), "Test hook must have been entered");

    // Verify health endpoint actively reports solve_in_progress: true
    std::string health_busy = http_get_sync("127.0.0.1", port, "/api/thermal3d/health");
    TEST_ASSERT(health_busy.find("\"solve_in_progress\":true") != std::string::npos,
                "Health endpoint must report solve_in_progress: true while solve is active");

    // Thread 2 attempts concurrent solve while Thread 1 holds the lock
    std::string resp2 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", solve_req, "Content-Type: application/json\r\n");
    TEST_ASSERT(resp2.find("409 Conflict") != std::string::npos, "Concurrent solve must deterministically return 409 Conflict");
    TEST_ASSERT(resp2.find("A 3D thermal solve is already in progress.") != std::string::npos,
                "409 body must state solve is already in progress");

    // Release Thread 1 from the hook
    release_solve_promise.set_value();
    t1.join();

    // Verify Thread 1 completed successfully with HTTP 200
    TEST_ASSERT(resp1.find("200 OK") != std::string::npos, "Thread 1 solve must complete with 200 OK after release");

    // Clear test hook
    HttpServer::set_thermal3d_test_hook(nullptr);

    // Verify health endpoint reports solve_in_progress: false
    std::string health_idle = http_get_sync("127.0.0.1", port, "/api/thermal3d/health");
    TEST_ASSERT(health_idle.find("\"solve_in_progress\":false") != std::string::npos,
                "Health endpoint must report solve_in_progress: false after completion");

    server.stop();
}

void test_thermal3d_api_duplicate_key_rejection_400() {
    int test_port = 9896;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for duplicate key test");
    int port = server.get_actual_port();

    // 1. Root duplicate key rejection
    std::string req1 = "{\"model\":\"CUBE\",\"model\":\"FINNED_SINK\",\"resolution\":10,\"solver\":\"PCG\"}";
    std::string resp1 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req1, "Content-Type: application/json\r\n");
    TEST_ASSERT(resp1.find("400 Bad Request") != std::string::npos, "Duplicate root key must return 400 Bad Request");
    TEST_ASSERT(resp1.find("Duplicate key in JSON object: 'model'") != std::string::npos, 
                "Error body must report duplicate key 'model'");

    // 2. Nested duplicate key in solver_options rejection
    std::string req2 = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"PCG\",\"solver_options\":{\"tolerance\":1e-6,\"tolerance\":1e-5}}";
    std::string resp2 = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req2, "Content-Type: application/json\r\n");
    TEST_ASSERT(resp2.find("400 Bad Request") != std::string::npos, "Duplicate nested key must return 400 Bad Request");
    TEST_ASSERT(resp2.find("Duplicate key in JSON object: 'tolerance'") != std::string::npos,
                "Error body must report duplicate nested key 'tolerance'");

    server.stop();
}

void test_thermal3d_api_origin_validation_and_rejection() {
    int test_port = 9897;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for Origin validation test");
    int port = server.get_actual_port();

    std::string valid_body = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"PROFILE_CHOLESKY\"}";

    // 1. Accept Origin: http://127.0.0.1:<port> on POST
    std::string h1 = "Origin: http://127.0.0.1:" + std::to_string(port) + "\r\nContent-Type: application/json\r\n";
    std::string r1 = http_custom_req_sync("POST", "127.0.0.1", port, "/api/thermal3d/solve", valid_body, h1);
    TEST_ASSERT(r1.find("200 OK") != std::string::npos, "Origin http://127.0.0.1:<port> must return 200 OK");

    // 2. Accept Origin: http://localhost:<port> on POST
    std::string h2 = "Origin: http://localhost:" + std::to_string(port) + "\r\nContent-Type: application/json\r\n";
    std::string r2 = http_custom_req_sync("POST", "127.0.0.1", port, "/api/thermal3d/solve", valid_body, h2);
    TEST_ASSERT(r2.find("200 OK") != std::string::npos, "Origin http://localhost:<port> must return 200 OK");

    // 3. Reject unexpected external Origin on POST
    std::string h3 = "Origin: https://evil.attacker.com\r\nContent-Type: application/json\r\n";
    std::string r3 = http_custom_req_sync("POST", "127.0.0.1", port, "/api/thermal3d/solve", valid_body, h3);
    TEST_ASSERT(r3.find("403 Forbidden") != std::string::npos, "External Origin on POST must return 403 Forbidden");
    TEST_ASSERT(r3.find("Origin rejected") != std::string::npos, "403 response must cite rejected Origin");

    // 4. Reject unexpected external Origin on GET /api/thermal3d/models
    std::string h4 = "Origin: https://evil.attacker.com\r\n";
    std::string r4 = http_custom_req_sync("GET", "127.0.0.1", port, "/api/thermal3d/models", "", h4);
    TEST_ASSERT(r4.find("403 Forbidden") != std::string::npos, "External Origin on GET /api/* must return 403 Forbidden");

    // 5. Accept GET /api/thermal3d/health with Origin: http://localhost:<port>
    std::string h5 = "Origin: http://localhost:" + std::to_string(port) + "\r\n";
    std::string r5 = http_custom_req_sync("GET", "127.0.0.1", port, "/api/thermal3d/health", "", h5);
    TEST_ASSERT(r5.find("200 OK") != std::string::npos, "GET with local Origin must return 200 OK");

    // 6. Accept local request without Origin header
    std::string r6 = http_custom_req_sync("GET", "127.0.0.1", port, "/api/thermal3d/config", "", "");
    TEST_ASSERT(r6.find("200 OK") != std::string::npos, "GET without Origin must return 200 OK");

    server.stop();
}

void test_thermal3d_api_serialization_safety_and_non_finite_guard() {
    int test_port = 9898;
    HttpServer server("127.0.0.1", test_port, "src/frontend");
    TEST_ASSERT(server.start(), "Server must start for serialization safety test");
    int port = server.get_actual_port();

    // 1. Force un-converged solver run (SOR max_iterations=1)
    std::string req = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"SOR\",\"solver_options\":{\"omega\":1.0,\"max_iterations\":1}}";
    std::string resp = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req, "Content-Type: application/json\r\n");

    TEST_ASSERT(resp.find("200 OK") != std::string::npos, "HTTP transaction must return 200 OK");
    TEST_ASSERT(resp.find("\"usable_field\":false") != std::string::npos, "usable_field must be false");

    // Verify unusable fields produce empty arrays and count: 0
    TEST_ASSERT(resp.find("\"coordinates\":[]") != std::string::npos, "coordinates must be empty array when unusable");
    TEST_ASSERT(resp.find("\"temperatures\":[]") != std::string::npos, "temperatures must be empty array when unusable");
    TEST_ASSERT(resp.find("\"count\":0") != std::string::npos, "thermal_field count must be 0 when unusable");
    TEST_ASSERT(resp.find("\"min_temp_degC\":null") != std::string::npos, "min_temp_degC must be null when unusable");
    TEST_ASSERT(resp.find("\"max_temp_degC\":null") != std::string::npos, "max_temp_degC must be null when unusable");
    TEST_ASSERT(resp.find("\"avg_temp_degC\":null") != std::string::npos, "avg_temp_degC must be null when unusable");

    // Verify that JSON payload does not contain non-finite IEEE 754 representations (nan, inf)
    TEST_ASSERT(!contains_inf_or_nan_token(resp), "Unconverged response must not contain unquoted nan or inf tokens");

    // 2. Converged solve must also have zero nan/inf tokens
    std::string req_ok = "{\"model\":\"CUBE\",\"resolution\":10,\"solver\":\"PROFILE_CHOLESKY\"}";
    std::string resp_ok = http_post_sync("127.0.0.1", port, "/api/thermal3d/solve", req_ok, "Content-Type: application/json\r\n");
    TEST_ASSERT(resp_ok.find("200 OK") != std::string::npos, "Cholesky solve must return 200 OK");
    TEST_ASSERT(!contains_inf_or_nan_token(resp_ok), "Cholesky response must not contain unquoted nan or inf tokens");

    server.stop();
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

    // Round 2 — Task G Tests (Phase 1)
    RUN_TEST(test_sparse_matrix_spd_properties);
    RUN_TEST(test_direct_cholesky_factorization_exactness);
    RUN_TEST(test_cholesky_pivot_scale_invariance);
    RUN_TEST(test_csr_structural_robustness);
    RUN_TEST(test_profile_cholesky_skyline_fillin_and_rcm);
    RUN_TEST(test_phase1_hardening_and_edge_cases);

    // Round 2 — Phase 2A Tests (Eigen Integration & PCG Foundation)
    RUN_TEST(test_eigen_conversion_and_pcg_foundation);

    // Round 2 — Phase 2B Tests (3D Geometry & Cell-Centred FVM)
    RUN_TEST(test_thermal_geometry_3d_cube_and_finned_mesh_hierarchy);
    RUN_TEST(test_cube_analytical_exactness_fvm);
    RUN_TEST(test_finned_geometry_energy_conservation_fvm);

    // Round 2 — Phase 2C Tests (Custom SOR & Spectral Radius Estimation)
    RUN_TEST(test_sor_convergence_and_solver_agreement);
    RUN_TEST(test_sor_parameters_initial_guess_and_four_termination_categories);
    RUN_TEST(test_spectral_radius_estimation_and_omega_sweep);

    // Round 2 — Phase 2D Tests (Comparative Benchmarking Pipeline)
    RUN_TEST(test_comparative_benchmark_3d_pipeline);

    // Round 2 — Phase 3B Tests (3D Thermal REST API)
    RUN_TEST(test_thermal3d_api_models_endpoint);
    RUN_TEST(test_thermal3d_api_config_endpoint);
    RUN_TEST(test_thermal3d_api_health_endpoint);
    RUN_TEST(test_thermal3d_api_solve_cube_cholesky_success);
    RUN_TEST(test_thermal3d_api_solve_finned_pcg_success);
    RUN_TEST(test_thermal3d_api_solve_cube_sor_convergence);
    RUN_TEST(test_thermal3d_api_unsupported_model_rejection_400);
    RUN_TEST(test_thermal3d_api_unsupported_resolution_rejection_400);
    RUN_TEST(test_thermal3d_api_invalid_solver_rejection_400);
    RUN_TEST(test_thermal3d_api_invalid_options_and_malformed_json_400);
    RUN_TEST(test_thermal3d_api_field_arrays_and_statistics);
    RUN_TEST(test_thermal3d_api_solver_failure_usable_field_false);
    RUN_TEST(test_thermal3d_api_deterministic_concurrency_guard_409);
    RUN_TEST(test_thermal3d_api_duplicate_key_rejection_400);
    RUN_TEST(test_thermal3d_api_origin_validation_and_rejection);
    RUN_TEST(test_thermal3d_api_serialization_safety_and_non_finite_guard);

    std::cout << "=================================================================\n";
    std::cout << " Test Summary: " << g_tests_passed << " Passed, " << g_tests_failed << " Failed\n";
    std::cout << "=================================================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
