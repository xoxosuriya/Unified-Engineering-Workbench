#include "multicore_bench.hpp"
#include "thread_pool.hpp"
#include <random>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
#endif

namespace workbench {

std::string MulticoreBenchmarker::get_cpu_brand_string() {
#if defined(_WIN32)
    int cpuInfo[4] = {-1};
    char CPUBrandString[0x40] = {0};
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = cpuInfo[0];
    if (nExIds >= 0x80000004) {
        __cpuid(reinterpret_cast<int*>(CPUBrandString), 0x80000002);
        __cpuid(reinterpret_cast<int*>(CPUBrandString + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(CPUBrandString + 32), 0x80000004);
        return std::string(CPUBrandString);
    }
#endif
    return "x86_64 Compatible Multi-Core Processor";
}

unsigned int MulticoreBenchmarker::get_hardware_concurrency() {
    unsigned int count = std::thread::hardware_concurrency();
    return count > 0 ? count : 1;
}

unsigned int MulticoreBenchmarker::get_physical_cores() {
#if defined(_WIN32)
    DWORD length = 0;
    GetLogicalProcessorInformation(nullptr, &length);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && length > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(buffer.data(), &length)) {
            unsigned int physical_cores = 0;
            for (const auto& info : buffer) {
                if (info.Relationship == RelationProcessorCore) {
                    physical_cores++;
                }
            }
            if (physical_cores > 0) return physical_cores;
        }
    }
#endif
    unsigned int logical = get_hardware_concurrency();
    return logical > 1 ? logical / 2 : 1;
}

double MulticoreBenchmarker::get_total_ram_gb() {
#if defined(_WIN32)
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        return static_cast<double>(mem_status.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    }
#endif
    return 16.0;
}

std::vector<ThermalParams1D> MulticoreBenchmarker::generate_batch(const BenchmarkConfig& cfg) {
    std::mt19937 rng(cfg.seed);
    
    // Controlled ranges
    std::uniform_real_distribution<double> dist_k(5.0, 50.0);      // conductivity [W/(m K)]
    std::uniform_real_distribution<double> dist_q(1000.0, 50000.0);// heat generation [W/m^3]
    std::uniform_real_distribution<double> dist_T0(50.0, 150.0);   // left BC [C]
    std::uniform_real_distribution<double> dist_TL(10.0, 40.0);    // right BC [C]
    std::uniform_real_distribution<double> dist_L(0.5, 2.0);       // length [m]

    std::vector<ThermalParams1D> batch;
    batch.reserve(cfg.case_count);

    for (size_t i = 0; i < cfg.case_count; ++i) {
        ThermalParams1D p;
        p.length_L = dist_L(rng);
        p.area_A = 1e-4; // fixed cross-section
        p.conductivity_k = dist_k(rng);
        p.heat_gen_q = dist_q(rng);
        p.temp_T0 = dist_T0(rng);
        p.temp_TL = dist_TL(rng);
        p.num_elements_N = cfg.mesh_size_N;
        batch.push_back(p);
    }

    return batch;
}

static void solve_batch_serial(const std::vector<ThermalParams1D>& batch, 
                               std::vector<ThermalSolveResult1D>& results,
                               std::atomic<bool>* cancel_flag = nullptr) {
    results.resize(batch.size());
    for (size_t i = 0; i < batch.size(); ++i) {
        if (cancel_flag && cancel_flag->load()) return;
        results[i] = ThermalFEMSolver1D::solve(batch[i]);
    }
}

static void solve_batch_parallel(const std::vector<ThermalParams1D>& batch,
                                 std::vector<ThermalSolveResult1D>& results,
                                 ThreadPool& pool,
                                 std::atomic<bool>* cancel_flag = nullptr) {
    results.resize(batch.size());
    const size_t num_cases = batch.size();
    const size_t workers = pool.worker_count();
    // Dynamic chunking for balanced queueing
    const size_t chunk_size = std::max(size_t(1), num_cases / (workers * 4));

    for (size_t start = 0; start < num_cases; start += chunk_size) {
        if (cancel_flag && cancel_flag->load()) return;
        size_t end = std::min(start + chunk_size, num_cases);
        pool.enqueue([&batch, &results, start, end, cancel_flag]() {
            for (size_t i = start; i < end; ++i) {
                if (cancel_flag && cancel_flag->load()) return;
                results[i] = ThermalFEMSolver1D::solve(batch[i]);
            }
        });
    }

    pool.wait_all();
}

BenchmarkSummary MulticoreBenchmarker::run_benchmark(const BenchmarkConfig& cfg, std::atomic<bool>* cancel_flag) {
    BenchmarkSummary summary;
    summary.config = cfg;
    summary.cpu_model = get_cpu_brand_string();
    summary.hardware_concurrency = get_hardware_concurrency();
    summary.physical_cores = get_physical_cores();
    summary.total_ram_gb = get_total_ram_gb();

#if defined(__clang__)
    summary.compiler_info = "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    summary.compiler_info = "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    summary.compiler_info = "MSVC " + std::to_string(_MSC_VER);
#else
    summary.compiler_info = "C++17 Compatible Compiler";
#endif

    summary.optimization_flags = "-O3";

    // Measure real launch/generator ready time
    auto t_start = std::chrono::high_resolution_clock::now();
    std::vector<ThermalParams1D> batch = generate_batch(cfg);
    auto t_ready = std::chrono::high_resolution_clock::now();
    summary.launch_to_ready_ms = std::chrono::duration<double, std::milli>(t_ready - t_start).count();

    if (cancel_flag && cancel_flag->load()) {
        summary.was_cancelled = true;
        return summary;
    }

    // Warm-up run (single pass to warm caches and branch predictors)
    auto t_wstart = std::chrono::high_resolution_clock::now();
    std::vector<ThermalSolveResult1D> warmup_res;
    solve_batch_serial(batch, warmup_res, cancel_flag);
    auto t_wend = std::chrono::high_resolution_clock::now();
    summary.warmup_duration_ms = std::chrono::duration<double, std::milli>(t_wend - t_wstart).count();

    if (cancel_flag && cancel_flag->load()) {
        summary.was_cancelled = true;
        return summary;
    }

    // Baseline serial solution for numerical verification
    std::vector<ThermalSolveResult1D> serial_gold_results;
    solve_batch_serial(batch, serial_gold_results, cancel_flag);

    // Filter worker counts by available hardware concurrency, always including 1
    std::vector<size_t> target_workers;
    for (size_t p : cfg.worker_counts) {
        if (p == 1 || p <= summary.hardware_concurrency) {
            if (std::find(target_workers.begin(), target_workers.end(), p) == target_workers.end()) {
                target_workers.push_back(p);
            }
        }
    }
    std::sort(target_workers.begin(), target_workers.end());

    double baseline_T1_median = 0.0;
    summary.max_absolute_temp_diff_C = 0.0;

    for (size_t p : target_workers) {
        if (cancel_flag && cancel_flag->load()) {
            summary.was_cancelled = true;
            break;
        }

        WorkerBenchmarkResult wr;
        wr.worker_count = p;
        std::vector<double> durations;

        // Construct pool ONCE outside repetition timing: isolates compute from thread spawn
        std::unique_ptr<ThreadPool> pool_ptr;
        if (p > 1) {
            pool_ptr = std::make_unique<ThreadPool>(p);
        }

        for (size_t rep = 0; rep < cfg.repetitions; ++rep) {
            if (cancel_flag && cancel_flag->load()) {
                summary.was_cancelled = true;
                break;
            }

            std::vector<ThermalSolveResult1D> trial_results;
            auto t0 = std::chrono::high_resolution_clock::now();

            if (p == 1) {
                solve_batch_serial(batch, trial_results, cancel_flag);
            } else {
                solve_batch_parallel(batch, trial_results, *pool_ptr, cancel_flag);
            }

            auto t1 = std::chrono::high_resolution_clock::now();
            double dur_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            
            RunTiming rt;
            rt.run_index = rep + 1;
            rt.duration_ms = dur_ms;
            wr.repetition_timings_ms.push_back(rt);
            durations.push_back(dur_ms);

            // Numerical agreement check against serial gold
            if (p > 1 && rep == 0) {
                for (size_t c = 0; c < batch.size(); ++c) {
                    for (size_t node = 0; node <= cfg.mesh_size_N; ++node) {
                        double diff = std::abs(trial_results[c].node_temp_fea[node] - 
                                               serial_gold_results[c].node_temp_fea[node]);
                        summary.max_absolute_temp_diff_C = std::max(summary.max_absolute_temp_diff_C, diff);
                    }
                }
            }
        }

        // Calculate statistics
        std::vector<double> sorted_dur = durations;
        std::sort(sorted_dur.begin(), sorted_dur.end());
        wr.median_duration_ms = sorted_dur[sorted_dur.size() / 2];
        wr.min_duration_ms = sorted_dur.front();
        wr.max_duration_ms = sorted_dur.back();

        double sum = 0.0;
        for (double d : durations) sum += d;
        wr.mean_duration_ms = sum / static_cast<double>(durations.size());

        if (p == 1) {
            baseline_T1_median = wr.median_duration_ms;
            wr.speedup = 1.0;
            wr.efficiency = 1.0;
        } else {
            wr.speedup = (wr.median_duration_ms > 0.0) ? (baseline_T1_median / wr.median_duration_ms) : 1.0;
            wr.efficiency = wr.speedup / static_cast<double>(p);
        }

        summary.worker_results.push_back(wr);
    }

    summary.numerical_agreement_passed = (summary.max_absolute_temp_diff_C <= 1e-8);
    return summary;
}

std::string BenchmarkSummary::to_json() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "{";
    ss << "\"cpu_model\":\"" << cpu_model << "\",";
    ss << "\"hardware_concurrency\":" << hardware_concurrency << ",";
    ss << "\"physical_cores\":" << physical_cores << ",";
    ss << "\"total_ram_gb\":" << total_ram_gb << ",";
    ss << "\"compiler_info\":\"" << compiler_info << "\",";
    ss << "\"optimization_flags\":\"" << optimization_flags << "\",";
    ss << "\"launch_to_ready_ms\":" << launch_to_ready_ms << ",";
    ss << "\"warmup_duration_ms\":" << warmup_duration_ms << ",";
    ss << "\"max_absolute_temp_diff_C\":" << std::scientific << std::setprecision(10) << max_absolute_temp_diff_C << ",";
    ss << std::fixed << std::setprecision(4);
    ss << "\"numerical_agreement_passed\":" << (numerical_agreement_passed ? "true" : "false") << ",";
    ss << "\"was_cancelled\":" << (was_cancelled ? "true" : "false") << ",";

    ss << "\"config\":{";
    ss << "\"seed\":" << config.seed << ",";
    ss << "\"case_count\":" << config.case_count << ",";
    ss << "\"mesh_size_N\":" << config.mesh_size_N << ",";
    ss << "\"repetitions\":" << config.repetitions;
    ss << "},";

    ss << "\"results\":[";
    for (size_t i = 0; i < worker_results.size(); ++i) {
        if (i > 0) ss << ",";
        const auto& wr = worker_results[i];
        ss << "{";
        ss << "\"workers\":" << wr.worker_count << ",";
        ss << "\"median_ms\":" << wr.median_duration_ms << ",";
        ss << "\"mean_ms\":" << wr.mean_duration_ms << ",";
        ss << "\"min_ms\":" << wr.min_duration_ms << ",";
        ss << "\"max_ms\":" << wr.max_duration_ms << ",";
        ss << "\"speedup\":" << wr.speedup << ",";
        ss << "\"efficiency\":" << wr.efficiency << ",";
        ss << "\"repetitions\":[";
        for (size_t r = 0; r < wr.repetition_timings_ms.size(); ++r) {
            if (r > 0) ss << ",";
            ss << wr.repetition_timings_ms[r].duration_ms;
        }
        ss << "]";
        ss << "}";
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

std::string BenchmarkSummary::to_csv() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "workers,repetition_1_ms,repetition_2_ms,repetition_3_ms,repetition_4_ms,repetition_5_ms,median_ms,mean_ms,speedup_T1_over_Tp,efficiency\n";

    for (const auto& wr : worker_results) {
        ss << wr.worker_count;
        for (const auto& rt : wr.repetition_timings_ms) {
            ss << "," << rt.duration_ms;
        }
        // Pad if less than 5
        for (size_t k = wr.repetition_timings_ms.size(); k < 5; ++k) {
            ss << ",N/A";
        }
        ss << "," << wr.median_duration_ms << "," << wr.mean_duration_ms 
           << "," << wr.speedup << "," << wr.efficiency << "\n";
    }

    return ss.str();
}

} // namespace workbench
