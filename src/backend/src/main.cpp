#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <csignal>
#include "http_server.hpp"
#include "thermal_fea_1d.hpp"
#include "multicore_bench.hpp"
#include "backprop_engine.hpp"
#include "stl_parser.hpp"
#include "ansys_parser.hpp"
#include "comparative_benchmark_3d.hpp"

using namespace workbench;

static HttpServer* g_active_server = nullptr;

static void handle_server_signal(int) {
    if (g_active_server) {
        std::cout << "\n[INFO] Received termination signal, shutting down server gracefully...\n";
        g_active_server->stop();
    }
}

void print_banner() {
    std::cout << "=================================================================\n";
    std::cout << " PASS SCIENCE WORKS  |  R&D INTERNSHIP 2026-2027                 \n";
    std::cout << " UNIFIED ENGINEERING WORKBENCH                                   \n";
    std::cout << " C++17 Core Engine & High-Performance Embedded Backend           \n";
    std::cout << "=================================================================\n";
}

int main(int argc, char* argv[]) {
    HttpServer::set_process_start_time(std::chrono::steady_clock::now());
    std::string mode = "server";
    int port = 8080;
    std::string host = "127.0.0.1";

    // Read environment variables for cloud runtime
    const char* env_port = std::getenv("PORT");
    if (env_port && *env_port) {
        try {
            port = std::stoi(env_port);
            host = "0.0.0.0"; // Default to binding all interfaces when PORT is provided by cloud runtime
        } catch (...) {}
    }
    const char* env_host = std::getenv("HOST");
    if (env_host && *env_host) {
        host = env_host;
    }

    std::string web_dir = "src/frontend";
    std::string out_file;
    std::string out_dir;
    std::string env_out_file;

    BenchmarkConfig bench_cfg;
    bench_cfg.case_count = 20000;
    bench_cfg.mesh_size_N = 200;
    bench_cfg.seed = 42;
    bench_cfg.repetitions = 5;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test") {
            mode = "test";
        } else if (arg == "--bench") {
            mode = "bench";
        } else if (arg == "--fea") {
            mode = "fea";
        } else if (arg == "--backprop") {
            mode = "backprop";
        } else if (arg == "--bench3d") {
            mode = "bench3d";
        } else if (arg == "--cases" && i + 1 < argc) {
            bench_cfg.case_count = std::stoull(argv[++i]);
        } else if (arg == "--mesh" && i + 1 < argc) {
            bench_cfg.mesh_size_N = std::stoull(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            bench_cfg.seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--repetitions" && i + 1 < argc) {
            bench_cfg.repetitions = std::stoul(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            out_file = argv[++i];
        } else if (arg == "--out-dir" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == "--env-out" && i + 1 < argc) {
            env_out_file = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--web-dir" && i + 1 < argc) {
            web_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: workbench_backend.exe [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --server                         Start web server (default)\n";
            std::cout << "  --port <port>                    Set server port (default 8080)\n";
            std::cout << "  --web-dir <dir>                  Path to frontend web assets\n";
            std::cout << "  --fea                            Run 1D Thermal FEA convergence study\n";
            std::cout << "  --out-dir <dir>                  Directory to save FEA CSV files\n";
            std::cout << "  --bench                          Run multicore benchmark\n";
            std::cout << "  --bench3d                        Run 3D thermal solver comparative benchmark (Cube & Finned Sink)\n";
            std::cout << "  --cases <N>                      Number of benchmark cases (default 20000)\n";
            std::cout << "  --mesh <M>                       Mesh size N for benchmark (default 200)\n";
            std::cout << "  --seed <S>                       Random seed for benchmark (default 42)\n";
            std::cout << "  --repetitions <R>                Benchmark repetitions (default 5)\n";
            std::cout << "  --out <file>                     Output CSV file path\n";
            std::cout << "  --env-out <file>                 Output environment.json path\n";
            std::cout << "  --backprop                       Run backprop study & finite difference check\n";
            std::cout << "  --help                           Display this help text\n";
            return 0;
        }
    }

    if (mode == "fea") {
        ConvergenceStudyResult study = ThermalFEMSolver1D::run_convergence_study();
        if (!out_dir.empty()) {
            std::ofstream f_conv(out_dir + "/fea_convergence.csv");
            if (f_conv.is_open()) {
                f_conv << study.to_csv();
            }
            std::ofstream f_prof(out_dir + "/fea_profile.csv");
            if (f_prof.is_open()) {
                f_prof << study.to_profile_csv();
            }
        } else if (!out_file.empty()) {
            std::ofstream f(out_file);
            if (f.is_open()) {
                f << study.to_csv();
            }
        } else {
            std::cout << study.to_csv();
        }
        return 0;
    }

    if (mode == "bench") {
        BenchmarkSummary bench = MulticoreBenchmarker::run_benchmark(bench_cfg);
        if (!out_file.empty()) {
            std::ofstream f(out_file);
            if (f.is_open()) {
                f << bench.to_csv();
            }
        } else {
            std::cout << bench.to_csv();
        }

        std::string env_path = env_out_file;
        if (env_path.empty() && !out_file.empty()) {
            size_t slash = out_file.find_last_of("\\/");
            std::string dir = (slash != std::string::npos) ? out_file.substr(0, slash) : ".";
            env_path = dir + "/environment.json";
        }
        if (!env_path.empty()) {
            std::ofstream f_env(env_path);
            if (f_env.is_open()) {
                f_env << "{\n"
                      << "  \"cpu_model\": \"" << bench.cpu_model << "\",\n"
                      << "  \"physical_cores\": " << bench.physical_cores << ",\n"
                      << "  \"logical_cores\": " << bench.hardware_concurrency << ",\n"
                      << "  \"total_ram_gb\": " << bench.total_ram_gb << ",\n"
                      << "  \"compiler_info\": \"" << bench.compiler_info << "\",\n"
                      << "  \"optimization_flags\": \"" << bench.optimization_flags << "\",\n"
                      << "  \"workload\": {\n"
                      << "    \"case_count\": " << bench.config.case_count << ",\n"
                      << "    \"mesh_size_N\": " << bench.config.mesh_size_N << ",\n"
                      << "    \"seed\": " << bench.config.seed << ",\n"
                      << "    \"repetitions\": " << bench.config.repetitions << "\n"
                      << "  },\n"
                      << "  \"batch_generation_ms\": " << bench.batch_generation_ms << ",\n"
                      << "  \"warmup_ms\": " << bench.warmup_duration_ms << ",\n"
                      << "  \"launch_to_ready_ms\": " << bench.launch_to_ready_ms << ",\n"
                      << "  \"max_serial_parallel_diff\": " << (std::isfinite(bench.max_absolute_temp_diff_C) ? bench.max_absolute_temp_diff_C : 0.0) << "\n"
                      << "}\n";
            }
        }
        return 0;
    }

    if (mode == "backprop") {
        BackpropSummary bp = BackpropEngine::run_full_study();
        if (!out_file.empty()) {
            std::ofstream f(out_file);
            if (f.is_open()) {
                f << bp.to_csv();
            }
        } else {
            std::cout << bp.to_csv();
        }
        return 0;
    }

    if (mode == "bench3d") {
        print_banner();
        std::cout << "[INFO] Starting 3D Thermal Solver Comparative Benchmark Pipeline...\n";
        std::cout << "[INFO] Resolutions: N in {10, 20, 30} | Repeats: 5 measured runs + 1 warmup per case\n";
        std::string target_dir = out_dir.empty() ? "results" : out_dir;

        ComparativeBenchmarkSuite suite = run_comparative_benchmark_suite({10, 20, 30}, 5, true);

        std::cout << "\n[INFO] Exporting benchmark artifacts to directory: " << target_dir << "\n";
        bool export_ok = suite.export_all(target_dir);
        if (export_ok) {
            std::cout << "[SUCCESS] Comparative benchmark completed and exported successfully:\n"
                      << "  - " << target_dir << "/benchmark_solvers_3d.csv\n"
                      << "  - " << target_dir << "/benchmark_summary_3d.csv\n"
                      << "  - " << target_dir << "/benchmark_sor_sweep_3d.csv\n"
                      << "  - " << target_dir << "/benchmark_metadata_3d.json\n";
        } else {
            std::cerr << "[ERROR] Failed to export one or more benchmark files to " << target_dir << "\n";
            return 1;
        }
        return 0;
    }

    // Default: Start Server
    print_banner();
    HttpServer server(host, port, web_dir);
    g_active_server = &server;
    std::signal(SIGINT, handle_server_signal);
    std::signal(SIGTERM, handle_server_signal);

    if (!server.start()) {
        std::cerr << "[Error] Failed to start HTTP server on " << host << ":" << port << std::endl;
        g_active_server = nullptr;
        return 1;
    }

    std::cout << "\nWorkbench Core Backend running at: " << server.get_base_url() << "\n";
    std::cout << "Listening on host: " << host << ", port: " << server.get_actual_port() << "\n";
    std::cout << "Press Enter or Ctrl+C to terminate server...\n";

    // Keep running
    server.wait();
    g_active_server = nullptr;
    return 0;
}
