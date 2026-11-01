#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include "thermal_fea_1d.hpp"

namespace workbench {

struct BenchmarkConfig {
    uint32_t seed = 42;
    size_t case_count = 1000;
    size_t mesh_size_N = 100;
    size_t repetitions = 5;
    std::vector<size_t> worker_counts = {1, 2, 4}; // Will be filtered by available logical cores
};

struct RunTiming {
    size_t run_index = 0;
    double duration_ms = 0.0;
};

struct WorkerBenchmarkResult {
    size_t worker_count = 1;
    std::vector<RunTiming> repetition_timings_ms;
    double median_duration_ms = 0.0;
    double mean_duration_ms = 0.0;
    double min_duration_ms = 0.0;
    double max_duration_ms = 0.0;
    double speedup = 1.0;            // T_1 / T_p
    double efficiency = 1.0;         // (T_1 / T_p) / p
};

struct BenchmarkSummary {
    BenchmarkConfig config;
    std::string cpu_model;
    unsigned int hardware_concurrency = 0;
    unsigned int physical_cores = 0;
    double total_ram_gb = 0.0;
    std::string compiler_info;
    std::string optimization_flags;
    
    double launch_to_ready_ms = 0.0;
    double warmup_duration_ms = 0.0;
    
    std::vector<WorkerBenchmarkResult> worker_results;
    
    // Serial vs Parallel numerical agreement check
    double max_absolute_temp_diff_C = 0.0;
    bool numerical_agreement_passed = false; // diff <= 1e-8 C
    bool was_cancelled = false;

    std::string to_json() const;
    std::string to_csv() const;
};

class MulticoreBenchmarker {
public:
    static std::vector<ThermalParams1D> generate_batch(const BenchmarkConfig& cfg);

    // Run full benchmark suite (with optional cancellation flag)
    static BenchmarkSummary run_benchmark(const BenchmarkConfig& cfg, std::atomic<bool>* cancel_flag = nullptr);

    // Query system hardware info
    static std::string get_cpu_brand_string();
    static unsigned int get_hardware_concurrency();
    static unsigned int get_physical_cores();
    static double get_total_ram_gb();
};

} // namespace workbench
