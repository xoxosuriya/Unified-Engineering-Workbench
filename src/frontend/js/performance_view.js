// Performance View: Multicore / Multithreaded Execution Benchmark & Scaling Analysis
const PerformanceView = {
  speedupChart: null,
  isBenchmarkRunning: false,
  abortController: null,

  init() {
    this.bindEvents();
    // Load initial benchmark
    this.runBenchmark();
  },

  bindEvents() {
    const runBtn = document.getElementById('perf-run-btn');
    if (runBtn) {
      runBtn.addEventListener('click', () => this.runBenchmark());
    }

    const cancelBtn = document.getElementById('perf-cancel-btn');
    if (cancelBtn) {
      cancelBtn.addEventListener('click', () => this.cancelBenchmark());
    }

    const exportBtn = document.getElementById('perf-export-csv-btn');
    if (exportBtn) {
      exportBtn.addEventListener('click', () => {
        const p = this.getParams();
        if (!p) return;
        window.location.href = `/api/export/performance_csv?cases=${p.cases}&mesh=${p.mesh}&seed=${p.seed}`;
      });
    }
  },

  getParams() {
    const casesEl = document.getElementById('perf-param-cases');
    const meshEl = document.getElementById('perf-param-mesh');
    const seedEl = document.getElementById('perf-param-seed');

    const cases_str = casesEl ? casesEl.value.trim() : "";
    const mesh_str = meshEl ? meshEl.value.trim() : "";
    const seed_str = seedEl ? seedEl.value.trim() : "";

    const cases = parseInt(cases_str, 10);
    const mesh = parseInt(mesh_str, 10);
    const seed = parseInt(seed_str, 10);

    // Strict validation
    if (!cases_str || isNaN(cases) || cases < 1) {
      App.showToast("Batch case count must be an integer >= 1.", "error");
      return null;
    }
    if (!mesh_str || isNaN(mesh) || mesh < 2) {
      App.showToast("Mesh size per case N must be an integer >= 2.", "error");
      return null;
    }
    if (!seed_str || isNaN(seed) || seed < 0) {
      App.showToast("PRNG seed must be a non-negative integer.", "error");
      return null;
    }

    return { cases, mesh, seed };
  },

  cancelBenchmark() {
    App.showToast("Requesting backend cooperative thread pool cancellation...", "warning");
    fetch('/api/performance/cancel', { method: 'POST' }).catch(() => {});
    if (this.abortController) {
      this.abortController.abort();
    }
    this.setRunning(false);
    App.showToast("Benchmark execution cancelled by user.", "warning");
  },

  startProgressPolling() {
    this.stopProgressPolling();
    const label = document.getElementById('perf-progress-label');
    const bar = document.getElementById('perf-progress-bar');

    this.progressInterval = setInterval(() => {
      fetch('/api/performance/progress')
        .then(r => r.json())
        .then(p => {
          if (!this.isBenchmarkRunning) return;
          if (label) {
            if (p.phase === 'measure') {
              label.textContent = `[MEASURE] Workers: ${p.worker_count} | Rep: ${p.rep} | Step: ${p.step}/${p.total_steps} (${Math.round(p.elapsed_ms)} ms)`;
            } else if (p.phase === 'generate') {
              label.textContent = `[GENERATE] Synthesizing test cases... (${Math.round(p.elapsed_ms)} ms)`;
            } else if (p.phase === 'warmup') {
              label.textContent = `[WARMUP] Priming CPU caches and thread queues... (${Math.round(p.elapsed_ms)} ms)`;
            } else if (p.phase === 'gold') {
              label.textContent = `[BASELINE] Computing serial gold verification... (${Math.round(p.elapsed_ms)} ms)`;
            } else {
              label.textContent = `[RUNNING] Active benchmark (${Math.round(p.elapsed_ms)} ms)...`;
            }
          }
          if (bar && p.total_steps > 0) {
            bar.classList.remove('indeterminate-bar');
            const pct = Math.min(100, Math.round((p.step / p.total_steps) * 100));
            bar.style.width = pct + '%';
          }
        })
        .catch(() => {});
    }, 150);
  },

  stopProgressPolling() {
    if (this.progressInterval) {
      clearInterval(this.progressInterval);
      this.progressInterval = null;
    }
    const bar = document.getElementById('perf-progress-bar');
    if (bar) {
      bar.classList.add('indeterminate-bar');
      bar.style.width = '100%';
    }
  },

  setRunning(running) {
    this.isBenchmarkRunning = running;
    const runBtn = document.getElementById('perf-run-btn');
    const cancelBtn = document.getElementById('perf-cancel-btn');
    const progress = document.getElementById('perf-progress-container');
    const label = document.getElementById('perf-progress-label');

    if (runBtn) runBtn.disabled = running;
    if (cancelBtn) cancelBtn.style.display = running ? 'inline-flex' : 'none';
    if (progress) progress.style.display = running ? 'block' : 'none';
    if (label) {
      label.textContent = running ? "Initializing benchmark..." : "Ready";
    }

    if (running) {
      this.startProgressPolling();
    } else {
      this.stopProgressPolling();
    }
  },

  runBenchmark() {
    const p = this.getParams();
    if (!p) return;

    this.setRunning(true);
    App.showToast(`Running multicore batch benchmark (${p.cases} cases, mesh N=${p.mesh})...`, "info");

    this.abortController = new AbortController();

    fetch(`/api/performance/run?cases=${p.cases}&mesh=${p.mesh}&seed=${p.seed}`, {
      signal: this.abortController.signal
    })
      .then(r => {
        if (!r.ok) return r.text().then(t => { throw new Error(t); });
        return r.json();
      })
      .then(data => {
        this.setRunning(false);
        this.renderHardwareSpecs(data);
        this.renderRepetitionsTable(data.results);
        this.renderSpeedupChart(data.results);

        // Agreement Check Badge
        const badge = document.getElementById('perf-agreement-badge');
        const diffText = document.getElementById('perf-max-diff-val');
        if (badge && diffText) {
          diffText.textContent = data.max_absolute_temp_diff_C.toExponential(4) + ' °C';
          if (data.numerical_agreement_passed) {
            badge.className = 'pill pill-success';
            badge.textContent = 'PASSED (<= 1e-8 °C)';
          } else {
            badge.className = 'pill pill-danger';
            badge.textContent = 'FAILED';
          }
        }

        App.showToast("Multicore benchmark completed successfully!", "success");
      })
      .catch(err => {
        this.setRunning(false);
        if (err.name !== 'AbortError') {
          App.showToast("Benchmark failed: " + err.message, "error");
        }
      });
  },

  renderHardwareSpecs(data) {
    const cpuEl = document.getElementById('perf-cpu-model');
    const coresEl = document.getElementById('perf-cores-logical');
    const ramEl = document.getElementById('perf-ram-total');
    const compEl = document.getElementById('perf-compiler-flags');
    const launchEl = document.getElementById('perf-launch-ready-time');
    const warmupEl = document.getElementById('perf-warmup-time');

    if (cpuEl) cpuEl.textContent = data.cpu_model || 'Host Multi-Core CPU';
    if (coresEl) coresEl.textContent = `${data.physical_cores} Physical Cores / ${data.hardware_concurrency} Threads`;
    if (ramEl) ramEl.textContent = `${data.total_ram_gb ? data.total_ram_gb.toFixed(1) : 16.0} GB RAM`;
    if (compEl) compEl.textContent = `${data.compiler_info} (${data.optimization_flags})`;
    if (launchEl) launchEl.textContent = `${data.launch_to_ready_ms.toFixed(2)} ms`;
    if (warmupEl) warmupEl.textContent = `${data.warmup_duration_ms.toFixed(2)} ms`;
  },

  renderRepetitionsTable(results) {
    const tbody = document.getElementById('perf-repetitions-tbody');
    if (!tbody) return;

    tbody.innerHTML = '';
    results.forEach(r => {
      const tr = document.createElement('tr');
      const repCols = r.repetitions.map(dur => `<td>${dur.toFixed(2)} ms</td>`).join('');
      tr.innerHTML = `
        <td style="font-weight:700; color:#fff;">p = ${r.workers} Worker${r.workers > 1 ? 's' : ''}</td>
        ${repCols}
        <td style="font-weight:700; color:#38bdf8;">${r.median_ms.toFixed(2)} ms</td>
        <td style="font-weight:700; color:#34d399;">${r.speedup.toFixed(2)}x</td>
        <td><span class="pill pill-cyan">${(r.efficiency * 100).toFixed(1)}%</span></td>
      `;
      tbody.appendChild(tr);
    });
  },

  renderSpeedupChart(results) {
    const ctx = document.getElementById('perf-speedup-chart');
    if (!ctx) return;

    if (this.speedupChart) this.speedupChart.destroy();

    const labels = results.map(r => `${r.workers} Core${r.workers > 1 ? 's' : ''}`);
    const speedups = results.map(r => r.speedup);
    const ideal = results.map(r => r.workers);

    this.speedupChart = new Chart(ctx, {
      type: 'bar',
      data: {
        labels: labels,
        datasets: [
          {
            label: 'Measured Speedup (T1 / Tp)',
            data: speedups,
            backgroundColor: 'rgba(6, 182, 212, 0.75)',
            borderColor: '#06b6d4',
            borderWidth: 1
          },
          {
            label: 'Ideal Linear Speedup',
            data: ideal,
            type: 'line',
            borderColor: '#10b981',
            borderDash: [4, 4],
            borderWidth: 2,
            pointBackgroundColor: '#10b981',
            fill: false
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { labels: { color: '#f3f4f6', font: { family: 'inherit', size: 12 } } },
          tooltip: {
            callbacks: {
              afterBody: (ctxs) => {
                const idx = ctxs[0].dataIndex;
                const eff = (results[idx].efficiency * 100).toFixed(1);
                return `Parallel Efficiency: ${eff}%`;
              }
            }
          }
        },
        scales: {
          x: { ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255, 255, 255, 0.05)' } },
          y: {
            title: { display: true, text: 'Speedup Factor', color: '#9ca3af' },
            ticks: { color: '#9ca3af' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          }
        }
      }
    });
  }
};
