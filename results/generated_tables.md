# UNIFIED ENGINEERING WORKBENCH - REPRODUCIBLE EVIDENCE REPORT
*Generated programmatically from a single run executed in this session on 2026-11-01 20:56:05*

### System Environment & Evidence Run Metadata
| Parameter | Recorded Value |
|---|---|
| **Candidate ID** | RA2311047010144 |
| **CPU Model** | 13th Gen Intel(R) Core(TM) i5-13420H |
| **Physical / Logical Cores** | 8 Physical / 12 Logical Cores |
| **Total System RAM** | 15.7108 GB |
| **Compiler & Platform** | GNU 16.2.0 (Windows 11 x64, MinGW Static) |
| **Optimization Flags** | "-O3 -DNDEBUG -O3" |
| **Benchmark Workload** | 20000 Cases, Mesh N = 200, Seed 42, 5 Repetitions |
| **Launch-to-Ready Time** | 0 ms |
| **Batch Generation Time** | 2.14 ms |
| **Warmup Duration** | 258.66 ms |
| **Max Serial/Parallel Diff** | 0.0000E+000 C |

### Multicore Parallel Speedup & Scaling Results
| Workers (p) | Rep 1 (ms) | Rep 2 (ms) | Rep 3 (ms) | Rep 4 (ms) | Rep 5 (ms) | Median (ms) | Mean (ms) | Speedup (Sp) | Efficiency (Ep) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 1 | 285.5 | 276.63 | 265.08 | 277.57 | 272.5 | **276.63** | 275.46 | **1x** | 100.0% |
| 2 | 144.46 | 148.64 | 236.18 | 178.2 | 153.61 | **153.61** | 172.22 | **1.8009x** | 90.0% |
| 4 | 87.46 | 85.04 | 82.26 | 82.76 | 91.24 | **85.04** | 85.75 | **3.2531x** | 81.3% |

### 1D Thermal FEA Convergence Verification
| Mesh (N) | Element Size h (m) | Max Midpoint Error (C) | Theoretical Error q*h^2/8k (C) | Max Nodal Error (C) | Error Reduction Ratio | Observed Rate (p = log2 r) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 10 | 0.1 | 1.25000000 | 1.25000000 | 0.0E+000 | 1.0000 | **2.0000** |
| 20 | 0.05 | 0.31250000 | 0.31250000 | 0.0E+000 | 4.0000 | **2.0000** |
| 40 | 0.025 | 0.07812500 | 0.07812500 | 0.0E+000 | 4.0000 | **2.0000** |
| 80 | 0.0125 | 0.01953125 | 0.01953125 | 0.0E+000 | 4.0000 | **2.0000** |

### Backpropagation Mathematics & Finite-Difference Verification
**Sample Input:** x = 1.0, Target y = 2.0, Learning Rate eta = 0.1, epsilon = 1e-6

#### Step 0 Forward & Analytical Gradients
| Quantity | Symbol | Formula | Program Output |
|---|---|---|---|
| Initial Weights | w0, b0, v0, c0 | Given | w = 0.5, b = 0.0, v = 1.0, c = 0.0 |
| Linear Combination | z0 | w*x + b | **0.5** |
| Hidden Activation | h0 | tanh(z0) | **0.46211716** |
| Output Prediction | y_hat_0 | v*h0 + c | **0.46211716** |
| Step 0 Loss | J0 | 0.5*(y_hat_0 - y)^2 | **1.18254182** |
| Gradient wrt c | dJ/dc | y_hat_0 - y | **-1.53788284** |
| Gradient wrt v | dJ/dv | (y_hat_0 - y)*h0 | **-0.71068205** |
| Gradient wrt b | dJ/db | (y_hat_0 - y)*v*(1 - h0^2) | **-1.20946448** |
| Gradient wrt w | dJ/dw | (dJ/db)*x | **-1.20946448** |

#### Step 1 Updated Parameters (theta_1 = theta_0 - eta * grad_J)
| Parameter | Symbol | Computation | Value at Step 1 |
|---|---|---|---|
| Weight w | w1 | 0.5 - 0.1 * (-1.20946448) | **0.62094645** |
| Bias b | b1 | 0.0 - 0.1 * (-1.20946448) | **0.12094645** |
| Weight v | v1 | 1.0 - 0.1 * (-0.71068205) | **1.07106820** |
| Bias c | c1 | 0.0 - 0.1 * (-1.53788284) | **0.15378828** |

#### Step 100 Converged State
- y_hat_100 = 2.00000000 (Target = 2.0)
- J_100 = 0.0000E+000
- Gradients at Step 100: grad_J ~ 1e-8

### Measured Parser Capacities & File Size Limits
| Format | Tested Entities | File Size (MB) | File Size (Bytes) | Server Parse Time (ms) | Response Payload (Bytes) | Status |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **STL (ASCII)** | 50,000 facets | 7.18 MB | 75,32,053 | **3573.3 ms** | 81,00,244 | **SUCCESS** |
| **STL (ASCII)** | 1,80,000 facets | 25.86 MB | 2,71,15,255 | **13044.45 ms** | 2,91,60,247 | **SUCCESS** |
| **STL (ASCII)** | 2,70,000 facets | 38.79 MB | 4,06,72,855 | **20789.51 ms** | 4,37,40,248 | **SUCCESS** |
| **ANSYS (CSV)** | 5,00,000 nodes | 13.84 MB | 1,45,10,530 | **4281.23 ms** | 2,21,22,474 | **SUCCESS** |