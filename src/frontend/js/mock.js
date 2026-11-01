/**
 * Unified Engineering Workbench - Deterministic Mock Backend
 * Offline mock engine executing mathematical formulations and mock telemetry
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.MockBackend = (function() {
  'use strict';

  // Internal simulated state
  let perfRunning = false;
  let perfCancelled = false;
  let perfProgress = { step: 0, total_steps: 25, phase: 'IDLE', worker_count: 1, rep: 1, elapsed_ms: 0 };
  let perfTimer = null;

  const hardwareState = {
    connectedDevice: null,
    activeFault: null,
    sequence: 0,
    startTime: Date.now()
  };

  // Helper to generate deterministic L-Bracket mesh facets (ASCII STL)
  function generateLBracketSTL() {
    // 24 facets composing an L-bracket with a triangular reinforcing rib
    // Dimensions: base 0.10m x 0.05m x 0.02m; vertical leg 0.02m x 0.05m x 0.10m
    const vertices = [];
    function addQuad(p1, p2, p3, p4) {
      // Triangle 1: p1, p2, p3
      vertices.push(...p1, ...p2, ...p3);
      // Triangle 2: p1, p3, p4
      vertices.push(...p1, ...p3, ...p4);
    }
    
    // Base block: x: [0, 0.10], y: [0, 0.02], z: [0, 0.05]
    // Vertical leg: x: [0, 0.02], y: [0.02, 0.10], z: [0, 0.05]
    // Bottom (y=0)
    addQuad([0,0,0], [0.1,0,0], [0.1,0,0.05], [0,0,0.05]);
    // Front base (x=0.1)
    addQuad([0.1,0,0], [0.1,0.02,0], [0.1,0.02,0.05], [0.1,0,0.05]);
    // Top base step (y=0.02, x from 0.02 to 0.1)
    addQuad([0.02,0.02,0], [0.1,0.02,0], [0.1,0.02,0.05], [0.02,0.02,0.05]);
    // Inner vertical face (x=0.02, y from 0.02 to 0.1)
    addQuad([0.02,0.02,0], [0.02,0.02,0.05], [0.02,0.1,0.05], [0.02,0.1,0]);
    // Top of vertical leg (y=0.1)
    addQuad([0,0.1,0], [0.02,0.1,0], [0.02,0.1,0.05], [0,0.1,0.05]);
    // Back of vertical leg (x=0)
    addQuad([0,0,0], [0,0,0.05], [0,0.1,0.05], [0,0.1,0]);
    // Side z=0 (two quads)
    addQuad([0,0,0], [0.1,0,0], [0.1,0.02,0], [0,0.02,0]);
    addQuad([0,0.02,0], [0.02,0.02,0], [0.02,0.1,0], [0,0.1,0]);
    // Side z=0.05 (two quads)
    addQuad([0,0,0.05], [0,0.02,0.05], [0.1,0.02,0.05], [0.1,0,0.05]);
    addQuad([0,0.02,0.05], [0,0.1,0.05], [0.02,0.1,0.05], [0.02,0.02,0.05]);

    // Triangular Gusset / Rib in center (z from 0.02 to 0.03)
    // Triangular face at z=0.02: (0.02, 0.08, 0.02) to (0.02, 0.02, 0.02) to (0.08, 0.02, 0.02)
    vertices.push(0.02, 0.08, 0.02,  0.02, 0.02, 0.02,  0.08, 0.02, 0.02);
    // Triangular face at z=0.03
    vertices.push(0.02, 0.08, 0.03,  0.08, 0.02, 0.03,  0.02, 0.02, 0.03);
    // Hypotenuse quad of rib
    addQuad([0.02, 0.08, 0.02], [0.08, 0.02, 0.02], [0.08, 0.02, 0.03], [0.02, 0.08, 0.03]);

    const facetCount = vertices.length / 9;
    return {
      success: true,
      solid_name: "L_BRACKET_RIB_STRUCTURAL",
      facet_count: facetCount,
      file_size_bytes: 4820,
      surface_area: 0.04825, // m^2
      bounds: {
        min: [0.0, 0.0, 0.0],
        max: [0.10, 0.10, 0.05]
      },
      vertices: vertices
    };
  }

  // Generate ANSYS Nodal Temperature Point Cloud (2,000 synthetic nodes)
  function generateAnsysData() {
    const nodeCount = 2000;
    const positions = [];
    const temperatures = [];
    const nodeIds = [];
    
    // Seeded random distribution across a turbine blade/block geometry
    let seed = 12345;
    function rand() {
      seed = (seed * 16807) % 2147483647;
      return (seed - 1) / 2147483646;
    }

    let minT = Infinity;
    let maxT = -Infinity;
    let sumT = 0;

    for (let i = 0; i < nodeCount; i++) {
      nodeIds.push(i + 1);
      // Dimensions: L=0.25m, W=0.08m, H=0.04m
      const x = rand() * 0.25;
      const y = (rand() - 0.5) * 0.08 * (1 - 0.5 * (x / 0.25)); // tapering
      const z = (rand() - 0.5) * 0.04;
      positions.push(x, y, z);

      // Temperature field: hot combustion root (x=0) to cooler tip (x=0.25)
      // T(x, y, z) = 185 - 150 * (x/0.25)^0.8 + 15 * sin(pi*y/0.04) + 8 * (rand() - 0.5)
      const t = 185 - 150 * Math.pow(x / 0.25, 0.8) + 12 * Math.cos(y * 40) + 6 * (rand() - 0.5);
      const roundedT = Math.round(t * 10) / 10;
      temperatures.push(roundedT);

      if (roundedT < minT) minT = roundedT;
      if (roundedT > maxT) maxT = roundedT;
      sumT += roundedT;
    }

    return {
      success: true,
      is_synthetic: true,
      node_count: nodeCount,
      file_size_bytes: 65420,
      min_temp_C: minT,
      max_temp_C: maxT,
      avg_temp_C: Math.round((sumT / nodeCount) * 10) / 10,
      bounds: {
        min: [0.0, -0.04, -0.02],
        max: [0.25, 0.04, 0.02]
      },
      node_ids: nodeIds,
      positions: positions,
      temperatures: temperatures
    };
  }

  // Exact 1D Steady-State Heat Conduction with Heat Generation:
  // -k * d^2T/dx^2 = q  =>  T(x) = T0 - (T0 - TL)*x/L + (q / (2*k)) * x * (L - x)
  function solve1DExact(x, L, k, q, T0, TL) {
    return T0 - (T0 - TL) * (x / L) + (q / (2.0 * k)) * x * (L - x);
  }

  // Real 1D FEA Solver using Linear 2-node Elements and Thomas Algorithm
  function solve1DFEA(N, L, A, k, q, T0, TL) {
    const h = L / N;
    const numNodes = N + 1;
    
    // Tridiagonal stiffness matrix: K_ii = 2*k*A/h, K_i,i-1 = -k*A/h, K_i,i+1 = -k*A/h
    // Load vector F_i = q * A * h
    const diag = new Float64Array(numNodes);
    const sub = new Float64Array(numNodes);
    const sup = new Float64Array(numNodes);
    const rhs = new Float64Array(numNodes);

    const kElem = (k * A) / h;
    const fElem = (q * A * h) / 2.0;

    for (let i = 1; i < N; i++) {
      diag[i] = 2.0 * kElem;
      sub[i] = -kElem;
      sup[i] = -kElem;
      rhs[i] = 2.0 * fElem;
    }

    // Apply Dirichlet Boundary Conditions: T[0] = T0, T[N] = TL
    rhs[1] -= sub[1] * T0;
    rhs[N - 1] -= sup[N - 1] * TL;

    // Thomas Algorithm for interior nodes 1 to N-1
    const nInt = N - 1;
    const cPrime = new Float64Array(nInt + 1);
    const dPrime = new Float64Array(nInt + 1);

    cPrime[1] = sup[1] / diag[1];
    dPrime[1] = rhs[1] / diag[1];

    for (let i = 2; i <= nInt; i++) {
      const m = diag[i] - sub[i] * cPrime[i - 1];
      cPrime[i] = sup[i] / m;
      dPrime[i] = (rhs[i] - sub[i] * dPrime[i - 1]) / m;
    }

    const T = new Float64Array(numNodes);
    T[0] = T0;
    T[N] = TL;
    T[nInt] = dPrime[nInt];
    for (let i = nInt - 1; i >= 1; i--) {
      T[i] = dPrime[i] - cPrime[i] * T[i + 1];
    }

    const xArr = [];
    const Tfea = [];
    const Texact = [];
    let maxNodalError = 0;

    for (let i = 0; i < numNodes; i++) {
      const xi = i * h;
      const tExact = solve1DExact(xi, L, k, q, T0, TL);
      const err = Math.abs(T[i] - tExact);
      if (err > maxNodalError) maxNodalError = err;
      xArr.push(Math.round(xi * 10000) / 10000);
      Tfea.push(Math.round(T[i] * 10000) / 10000);
      Texact.push(Math.round(tExact * 10000) / 10000);
    }

    // Evaluate Element Midpoints (where linear shape functions have maximum interpolation error)
    // Theoretical error for linear element: q * h^2 / (8 * k)
    const midpoints = [];
    let maxMidpointError = 0;
    for (let e = 0; e < N; e++) {
      const xMid = (e + 0.5) * h;
      const tFeaMid = (T[e] + T[e + 1]) / 2.0;
      const tExactMid = solve1DExact(xMid, L, k, q, T0, TL);
      const absError = Math.abs(tFeaMid - tExactMid);
      if (absError > maxMidpointError) maxMidpointError = absError;
      midpoints.push({
        elem: e + 1,
        x_mid: Math.round(xMid * 10000) / 10000,
        T_fea_mid: Math.round(tFeaMid * 10000) / 10000,
        T_exact_mid: Math.round(tExactMid * 10000) / 10000,
        abs_error: Math.round(absError * 100000) / 100000
      });
    }

    return {
      N: N,
      h: Math.round(h * 10000) / 10000,
      L: L,
      A: A,
      k: k,
      q: q,
      T0: T0,
      TL: TL,
      max_midpoint_error: maxMidpointError,
      max_nodal_error: maxNodalError,
      x: xArr,
      T_fea: Tfea,
      T_exact: Texact,
      midpoints: midpoints
    };
  }

  return {
    getStatus: function() {
      return Promise.resolve({
        status: 'ok',
        app_name: 'Unified Engineering Workbench',
        version: '1.0.0-rc2',
        cpu_model: 'AMD Ryzen 9 7950X / Intel Core i9-13900K',
        hardware_concurrency: 16,
        physical_cores: 16,
        total_ram_gb: 32,
        launch_to_ready_ms: 16.4,
        active_port: 8080,
        session_token: 'tok_pass_science_offline_mock_ra2311047010144'
      });
    },

    shutdown: function() {
      return Promise.resolve({ success: true, message: 'Workbench stopped. You can close this tab.' });
    },

    getStlSample: function() {
      return Promise.resolve(generateLBracketSTL());
    },

    parseStl: function(rawText) {
      if (!rawText || rawText.trim().length === 0) {
        return Promise.reject({
          success: false,
          error_message: 'STL file is empty or contains zero facets. Check that the file is an ASCII STL.'
        });
      }
      if (!rawText.includes('solid') || !rawText.includes('facet')) {
        return Promise.reject({
          success: false,
          error_message: 'Malformed STL: header does not begin with "solid" or lacks valid facet definitions.'
        });
      }
      return Promise.resolve(generateLBracketSTL());
    },

    getAnsysSample: function() {
      return Promise.resolve(generateAnsysData());
    },

    parseAnsys: function(rawCsv) {
      if (!rawCsv || rawCsv.trim().length === 0) {
        return Promise.reject({
          success: false,
          error_message: 'ANSYS CSV file is empty. Expected columns: node_id,x_m,y_m,z_m,temperature_C.'
        });
      }
      return Promise.resolve(generateAnsysData());
    },

    solveFea: function(params) {
      const N = parseInt(params.N || 10, 10);
      const L = parseFloat(params.L || 1.0);
      const A = parseFloat(params.A || 1e-4);
      const k = parseFloat(params.k || 10.0);
      const q = parseFloat(params.q || 10000.0);
      const T0 = parseFloat(params.T0 || 100.0);
      const TL = parseFloat(params.TL || 20.0);
      return Promise.resolve(solve1DFEA(N, L, A, k, q, T0, TL));
    },

    convergenceFea: function(params) {
      const L = parseFloat(params.L || 1.0);
      const A = parseFloat(params.A || 1e-4);
      const k = parseFloat(params.k || 10.0);
      const q = parseFloat(params.q || 10000.0);
      const T0 = parseFloat(params.T0 || 100.0);
      const TL = parseFloat(params.TL || 20.0);

      const meshes = [10, 20, 40, 80];
      const solves = meshes.map(N => solve1DFEA(N, L, A, k, q, T0, TL));
      const rows = [];

      for (let i = 0; i < solves.length; i++) {
        const s = solves[i];
        const theoretical = (q * s.h * s.h) / (8.0 * k);
        let ratio = null;
        let rate = null;
        if (i > 0) {
          ratio = solves[i - 1].max_midpoint_error / s.max_midpoint_error;
          rate = Math.log2(ratio);
        }
        rows.push({
          N: s.N,
          h: s.h,
          max_midpoint_error: s.max_midpoint_error,
          theoretical_midpoint_error: theoretical,
          max_nodal_error: s.max_nodal_error,
          reduction_ratio: ratio ? Math.round(ratio * 100) / 100 : null,
          observed_rate: rate ? Math.round(rate * 100) / 100 : null
        });
      }

      return Promise.resolve({
        base_params: { L, A, k, q, T0, TL },
        rows: rows,
        solves: solves
      });
    },

    runPerformance: function(cases, mesh, seed) {
      perfRunning = true;
      perfCancelled = false;
      perfProgress = { step: 0, total_steps: 25, phase: 'WARMING_UP', worker_count: 1, rep: 1, elapsed_ms: 0 };
      
      const startTime = Date.now();
      const totalSteps = 25;

      return new Promise((resolve, reject) => {
        clearInterval(perfTimer);
        perfTimer = setInterval(() => {
          if (perfCancelled) {
            clearInterval(perfTimer);
            perfRunning = false;
            resolve({
              was_cancelled: true,
              results: []
            });
            return;
          }

          perfProgress.step += 1;
          perfProgress.elapsed_ms = Date.now() - startTime;
          const pct = perfProgress.step / totalSteps;

          if (pct < 0.2) {
            perfProgress.phase = 'SERIAL_BASELINE';
            perfProgress.worker_count = 1;
          } else if (pct < 0.4) {
            perfProgress.phase = 'PARALLEL_POOL_2';
            perfProgress.worker_count = 2;
          } else if (pct < 0.6) {
            perfProgress.phase = 'PARALLEL_POOL_4';
            perfProgress.worker_count = 4;
          } else if (pct < 0.8) {
            perfProgress.phase = 'PARALLEL_POOL_8';
            perfProgress.worker_count = 8;
          } else {
            perfProgress.phase = 'PARALLEL_POOL_16';
            perfProgress.worker_count = 16;
          }

          if (perfProgress.step >= totalSteps) {
            clearInterval(perfTimer);
            perfRunning = false;
            
            // Baseline T1 ~ 1820 ms
            const t1 = 1824.5;
            const workerCounts = [1, 2, 4, 8, 16];
            const idealScalings = [1.0, 1.94, 3.76, 7.12, 12.85];

            const results = workerCounts.map((w, idx) => {
              const speedup = idealScalings[idx];
              const median = Math.round((t1 / speedup) * 10) / 10;
              const efficiency = Math.round((speedup / w) * 100) / 100;
              return {
                workers: w,
                median_ms: median,
                mean_ms: Math.round((median * 1.01) * 10) / 10,
                min_ms: Math.round((median * 0.98) * 10) / 10,
                max_ms: Math.round((median * 1.03) * 10) / 10,
                speedup: speedup,
                efficiency: efficiency,
                repetitions: [
                  Math.round(median * 1.02 * 10) / 10,
                  Math.round(median * 0.99 * 10) / 10,
                  median,
                  Math.round(median * 1.01 * 10) / 10,
                  Math.round(median * 0.98 * 10) / 10
                ]
              };
            });

            resolve({
              cpu_model: 'AMD Ryzen 9 7950X 16-Core / Intel Core i9',
              hardware_concurrency: 16,
              physical_cores: 16,
              total_ram_gb: 32,
              compiler_info: 'GCC 12.2.0 (Ubuntu x86_64)',
              optimization_flags: '-O3 -mavx2 -mfma -pthread',
              launch_to_ready_ms: 18.2,
              warmup_duration_ms: 245.0,
              max_absolute_temp_diff_C: 0.000000,
              numerical_agreement_passed: true,
              was_cancelled: false,
              config: {
                seed: seed || 42,
                case_count: cases || 20000,
                mesh_size_N: mesh || 200,
                repetitions: 5
              },
              results: results
            });
          }
        }, 120);
      });
    },

    getPerformanceProgress: function() {
      return Promise.resolve({
        running: perfRunning,
        phase: perfProgress.phase,
        worker_count: perfProgress.worker_count,
        rep: perfProgress.rep,
        step: perfProgress.step,
        total_steps: perfProgress.total_steps,
        elapsed_ms: perfProgress.elapsed_ms
      });
    },

    cancelPerformance: function() {
      perfCancelled = true;
      perfRunning = false;
      return Promise.resolve({ success: true });
    },

    hardwareDiscover: function() {
      return Promise.resolve([
        {
          id: 'dev_mcu_01',
          name: 'STM32H753ZI High-Performance Cortex-M7',
          type: 'MCU',
          manufacturer: 'STMicroelectronics',
          vid_pid: '0483:374B',
          serial: '002B0032-4753-RA23',
          firmware: 'v2.4.1-rtos',
          voltages: '3.3V / 1.8V Core',
          connector: 'Micro-USB / SWD',
          protocol: 'ARM SWD / USART Telemetry',
          is_simulated: true
        },
        {
          id: 'dev_fpga_01',
          name: 'Xilinx Artix-7 XC7A100T-2CSG324C',
          type: 'FPGA',
          manufacturer: 'AMD Xilinx',
          vid_pid: '0403:6010',
          serial: 'XC7A-88421-SW',
          firmware: 'Bitstream Rev D (Thermal Accel)',
          voltages: '1.0V (VCCINT) / 1.8V (VCCAUX) / 3.3V (VCCO)',
          connector: 'USB-C / 14-pin JTAG',
          protocol: 'IEEE 1149.1 JTAG / AXI-Stream',
          is_simulated: true
        },
        {
          id: 'dev_jtag_01',
          name: 'J-Link PRO High-Speed Debug Probe',
          type: 'JTAG',
          manufacturer: 'SEGGER Microcontroller',
          vid_pid: '1366:0105',
          serial: 'SEG-6942001-PRO',
          firmware: 'V11.00 (Mar 2026)',
          voltages: 'Target VREF: 1.2V - 5.0V Active Sensing',
          connector: 'Standard 20-pin JTAG / USB 2.0 High-Speed',
          protocol: 'IEEE 1149.1 / 1149.7 / SWD / SWO',
          is_simulated: true
        }
      ]);
    },

    hardwareConnect: function(deviceId) {
      hardwareState.connectedDevice = deviceId;
      hardwareState.activeFault = null;
      return Promise.resolve({ success: true, connected: true, deviceId: deviceId });
    },

    hardwareDisconnect: function(deviceId) {
      if (hardwareState.connectedDevice === deviceId) {
        hardwareState.connectedDevice = null;
      }
      return Promise.resolve({ success: true, connected: false });
    },

    hardwareInjectFault: function(faultPayload) {
      // payload format: "deviceId:timeout" | "deviceId:crc" | "deviceId:voltage"
      const parts = faultPayload.split(':');
      const fault = parts[1] || 'timeout';
      hardwareState.activeFault = fault;
      return Promise.resolve({ success: true, activeFault: fault });
    },

    hardwareRecover: function(deviceId) {
      hardwareState.activeFault = null;
      return Promise.resolve({ success: true, recovered: true });
    },

    hardwareTelemetry: function(deviceId) {
      hardwareState.sequence++;
      const t = (Date.now() - hardwareState.startTime) / 1000.0;
      const isFault = !!hardwareState.activeFault;

      let state = 'NORMAL';
      let statusMsg = 'Link operating normally. Telemetry nominal.';

      if (hardwareState.activeFault === 'timeout') {
        state = 'FAULT_TIMEOUT';
        statusMsg = 'ERROR: Response timeout (> 500ms). Target did not acknowledge frame.';
      } else if (hardwareState.activeFault === 'crc') {
        state = 'FAULT_CRC';
        statusMsg = 'ERROR: CRC-32 checksum mismatch in packet payload. Frame discarded.';
      } else if (hardwareState.activeFault === 'voltage') {
        state = 'FAULT_UNDERVOLTAGE';
        statusMsg = 'ERROR: Target VREF dropped below threshold (Brown-out detected).';
      }

      return Promise.resolve({
        device_id: deviceId,
        sequence_id: hardwareState.sequence,
        timestamp_sec: Math.round(t * 10) / 10,
        is_simulated: true,
        state: state,
        active_fault: hardwareState.activeFault,
        status_message: statusMsg,
        mcu: {
          core_temp_C: Math.round((42.5 + Math.sin(t * 0.5) * 4.2) * 10) / 10,
          vcc_volts: isFault && hardwareState.activeFault === 'voltage' ? 2.15 : Math.round((3.29 + Math.sin(t * 1.5) * 0.02) * 100) / 100,
          uptime_ms: Math.round(t * 1000),
          adc_ch0: Math.round(2048 + Math.sin(t) * 1024),
          gpio_state: '0x00A5'
        },
        fpga: {
          die_temp_C: Math.round((54.1 + Math.cos(t * 0.3) * 6.5) * 10) / 10,
          vccint_volts: Math.round((0.998 + Math.sin(t * 2) * 0.005) * 1000) / 1000,
          vccaux_volts: 1.802,
          lut_util_pct: 64.2,
          pll_locked: true,
          bitstream_hash: '0x9A4F82D1'
        },
        jtag: {
          target_vref_volts: isFault && hardwareState.activeFault === 'voltage' ? 1.45 : 3.31,
          tck_frequency_mhz: 12.0,
          tap_detected_idcode: '0x06413041',
          tap_device_name: 'ARM Cortex-M7 TAP / Boundary Scan Chain',
          packets_transferred: hardwareState.sequence * 8
        }
      });
    },

    hardwareArchitecture: function() {
      return Promise.resolve({
        system_name: 'PASS Science Works Heterogeneous Testbench Architecture',
        jtag_architectural_explanation: 
          'JTAG (IEEE 1149.1 Standard Test Access Port and Boundary-Scan Architecture) is a shift-register debug and verification interface designed for chip testing, flash programming, and boundary scanning. It consists of a 4- or 5-wire synchronous bus (TCK, TMS, TDI, TDO, and optional nTRST) driven by a 16-state TAP Controller finite state machine. It is fundamentally not a high-speed data bus or compute accelerator; treating JTAG as a coprocessor interconnect creates extreme bandwidth bottlenecks (10-50 MHz serial clock) and lacks memory management or DMA arbitration.',
        safe_integration_plan:
          '1. Use JTAG exclusively for TAP discovery, boundary-scan integrity testing, and non-volatile configuration memory loading.\n2. Dedicate PCIe or USB 3.2 High-Speed AXI-Stream for continuous runtime telemetry and compute tensor offload.\n3. Implement galvanic isolation on the JTAG target reference line (VREF) to prevent ground loops during motor and thermal actuation transients.'
      });
    },

    backpropStudy: function() {
      // Input x=1.5, target y=0.5
      // 1-hidden-layer MLP: z = w*x + b, h = tanh(z), y_hat = v*h + c
      // Loss J = 0.5 * (y_hat - y)^2
      const x = 1.5;
      const target_y = 0.5;
      const lr = 0.1;
      
      let w = 0.8;
      let b = 0.2;
      let v = -0.5;
      let c = 0.3;

      function forward(w_val, b_val, v_val, c_val) {
        const z = w_val * x + b_val;
        const h = Math.tanh(z);
        const y_hat = v_val * h + c_val;
        const err = y_hat - target_y;
        const loss = 0.5 * err * err;
        return { z, h, y_hat, err, loss };
      }

      function computeGradients(w_val, b_val, v_val, c_val) {
        const { z, h, y_hat, err, loss } = forward(w_val, b_val, v_val, c_val);
        // dJ/dc = (y_hat - y) * 1
        const dJ_dc = err;
        // dJ/dv = (y_hat - y) * h
        const dJ_dv = err * h;
        // dJ/dh = (y_hat - y) * v
        const dJ_dh = err * v_val;
        // dh/dz = 1 - h^2 (tanh derivative)
        const dh_dz = 1.0 - h * h;
        // dJ/dz = dJ/dh * dh/dz
        const dJ_dz = dJ_dh * dh_dz;
        // dJ/dw = dJ/dz * x
        const dJ_dw = dJ_dz * x;
        // dJ/db = dJ/dz * 1
        const dJ_db = dJ_dz;

        return { z, h, y_hat, loss_J: loss, dJ_dw, dJ_db, dJ_dv, dJ_dc };
      }

      const step0 = computeGradients(w, b, v, c);
      const eps = 1e-5;

      // Central finite difference numerical check: [J(theta + eps) - J(theta - eps)] / (2*eps)
      const gradChecks = [
        {
          param: 'w',
          analytical: step0.dJ_dw,
          numerical: (forward(w + eps, b, v, c).loss - forward(w - eps, b, v, c).loss) / (2.0 * eps)
        },
        {
          param: 'b',
          analytical: step0.dJ_db,
          numerical: (forward(w, b + eps, v, c).loss - forward(w, b - eps, v, c).loss) / (2.0 * eps)
        },
        {
          param: 'v',
          analytical: step0.dJ_dv,
          numerical: (forward(w, b, v + eps, c).loss - forward(w, b, v - eps, c).loss) / (2.0 * eps)
        },
        {
          param: 'c',
          analytical: step0.dJ_dc,
          numerical: (forward(w, b, v, c + eps).loss - forward(w, b, v, c - eps).loss) / (2.0 * eps)
        }
      ].map(chk => {
        const diff = Math.abs(chk.analytical - chk.numerical);
        return {
          param: chk.param,
          analytical: Math.round(chk.analytical * 1000000) / 1000000,
          numerical: Math.round(chk.numerical * 1000000) / 1000000,
          abs_diff: diff,
          passed: diff < 1e-6
        };
      });

      // Updated step 1 weights: theta = theta - lr * grad
      const updatedStep1 = {
        w: Math.round((w - lr * step0.dJ_dw) * 100000) / 100000,
        b: Math.round((b - lr * step0.dJ_db) * 100000) / 100000,
        v: Math.round((v - lr * step0.dJ_dv) * 100000) / 100000,
        c: Math.round((c - lr * step0.dJ_dc) * 100000) / 100000
      };

      // 100 training steps
      const trainingSteps = [];
      let curW = w;
      let curB = b;
      let curV = v;
      let curC = c;

      for (let s = 0; s < 100; s++) {
        const grads = computeGradients(curW, curB, curV, curC);
        trainingSteps.push({
          step: s,
          w: Math.round(curW * 10000) / 10000,
          b: Math.round(curB * 10000) / 10000,
          v: Math.round(curV * 10000) / 10000,
          c: Math.round(curC * 10000) / 10000,
          loss: Math.round(grads.loss_J * 1000000) / 1000000,
          y_hat: Math.round(grads.y_hat * 10000) / 10000
        });

        // Gradient descent update
        curW -= lr * grads.dJ_dw;
        curB -= lr * grads.dJ_db;
        curV -= lr * grads.dJ_dv;
        curC -= lr * grads.dJ_dc;
      }

      return Promise.resolve({
        input_x: x,
        target_y: target_y,
        learning_rate: lr,
        epsilon: eps,
        step_0: {
          z: Math.round(step0.z * 10000) / 10000,
          h: Math.round(step0.h * 10000) / 10000,
          y_hat: Math.round(step0.y_hat * 10000) / 10000,
          loss_J: Math.round(step0.loss_J * 100000) / 100000,
          dJ_dw: Math.round(step0.dJ_dw * 100000) / 100000,
          dJ_db: Math.round(step0.dJ_db * 100000) / 100000,
          dJ_dv: Math.round(step0.dJ_dv * 100000) / 100000,
          dJ_dc: Math.round(step0.dJ_dc * 100000) / 100000
        },
        gradient_checks: gradChecks,
        all_checks_passed: gradChecks.every(c => c.passed),
        updated_weights_step_1: updatedStep1,
        training_steps: trainingSteps
      });
    }
  };
})();
