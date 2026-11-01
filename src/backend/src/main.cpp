#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include "http_server.hpp"
#include "thermal_fea_1d.hpp"
#include "multicore_bench.hpp"
#include "backprop_engine.hpp"
#include "stl_parser.hpp"
#include "ansys_parser.hpp"

using namespace workbench;

void print_banner() {
    std::cout << "=================================================================\n";
    std::cout << " PASS SCIENCE WORKS  |  R&D INTERNSHIP 2026-2027                 \n";
    std::cout << " UNIFIED ENGINEERING WORKBENCH                                   \n";
    std::cout << " C++17 Core Engine & High-Performance Embedded Backend           \n";
    std::cout << "=================================================================\n";
}

int main(int argc, char* argv[]) {
    print_banner();

    std::string mode = "server";
    int port = 8080;
    std::string web_dir = "src/frontend";

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
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--web-dir" && i + 1 < argc) {
            web_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: workbench_backend.exe [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --server             Start web server (default)\n";
            std::cout << "  --port <port>        Set server port (default 8080)\n";
            std::cout << "  --web-dir <dir>      Path to frontend web assets\n";
            std::cout << "  --fea                Run 1D Thermal FEA convergence study\n";
            std::cout << "  --bench              Run multicore benchmark\n";
            std::cout << "  --backprop           Run backprop study & finite difference check\n";
            std::cout << "  --help               Display this help text\n";
            return 0;
        }
    }

    if (mode == "fea") {
        std::cout << "\n[FEA] Running 1D Thermal FEA Steady-State Rod Solver...\n";
        ConvergenceStudyResult study = ThermalFEMSolver1D::run_convergence_study();
        std::cout << study.to_csv() << std::endl;
        return 0;
    }

    if (mode == "bench") {
        std::cout << "\n[BENCH] Running Multicore Parallel Batch Solver Benchmark...\n";
        BenchmarkConfig cfg;
        BenchmarkSummary bench = MulticoreBenchmarker::run_benchmark(cfg);
        std::cout << bench.to_csv() << std::endl;
        return 0;
    }

    if (mode == "backprop") {
        std::cout << "\n[BACKPROP] Running Simple Handwritten Backpropagation Verification...\n";
        BackpropSummary bp = BackpropEngine::run_full_study();
        std::cout << bp.to_csv() << std::endl;
        return 0;
    }

    // Default: Start Server
    HttpServer server("127.0.0.1", port, web_dir);
    if (!server.start()) {
        std::cerr << "[Error] Failed to start HTTP server." << std::endl;
        return 1;
    }

    std::cout << "\nWorkbench Core Backend running at: " << server.get_base_url() << "\n";
    std::cout << "Press Enter or Ctrl+C to terminate server...\n";

    // Keep running
    server.wait();
    return 0;
}
