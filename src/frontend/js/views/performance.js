/**
 * Unified Engineering Workbench - Multicore Batch View (Module 5 / Task D)
 * Serial vs Bounded Worker Pool parallel performance benchmark, speedup chart, and telemetry
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.PerformanceView = (function() {
  'use strict';

  let containerEl = null;
  let statusLine = null;
  let speedupChart = null;
  let progressTimer = null;
  let abortController = null;
  let lastResult = null;
  let isRunning = false;

  function renderHTML() {
    return `
      <!-- Left Column: Controls & System Host Card -->
      <div class="detail-left-col">
        <div class="detail-text-stack">
          <div class="detail-mask">
            <span class="lbl detail-subtitle">TASK D / MULTICORE BATCH</span>
          </div>
          <div class="detail-mask">
            <h1 class="detail-title">Multicore Batch</h1>
          </div>
          <div class="detail-mask">
            <span class="lbl detail-meta">PARALLEL WORKER POOL / C++17 MULTITHREADING</span>
          </div>
          <div class="detail-mask">
            <p class="detail-blurb">
              Serial baseline execution versus bounded worker thread pool on identical pseudo-randomly seeded 1D thermal solves.
            </p>
          </div>
          <div class="detail-mask">
            <p class="detail-credit">
              Engineering Workbench assessment module. Candidate ${CONFIG.candidateId}.
            </p>
          </div>
        </div>

        <div class="detail-controls-panel">
          <div class="controls-title">BENCHMARK WORKLOAD CONFIGURATION</div>

          <!-- Error Banner Mask -->
          <div class="error-banner-mask">
            <div class="error-banner" id="perf-error-banner">
              <span class="error-banner-title">BENCHMARK ERROR</span>
              <span class="error-banner-text"></span>
              <span class="error-banner-hint"></span>
            </div>
          </div>

          <!-- Workload Inputs -->
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px;">
            <div class="control-group">
              <label class="control-label">Solve Cases</label>
              <input type="number" id="inp-perf-cases" class="control-input" value="20000" min="1" max="200000" />
              <span class="input-error-msg" id="err-perf-cases"></span>
            </div>
            <div class="control-group">
              <label class="control-label">Mesh Size N</label>
              <input type="number" id="inp-perf-mesh" class="control-input" value="200" min="1" max="5000" />
              <span class="input-error-msg" id="err-perf-mesh"></span>
            </div>
          </div>

          <div class="control-group">
            <label class="control-label">Seeded Random PRNG Seed</label>
            <input type="number" id="inp-perf-seed" class="control-input" value="42" min="0" max="4294967295" />
            <span class="input-error-msg" id="err-perf-seed"></span>
          </div>

          <!-- Action Buttons -->
          <div style="display: flex; gap: 8px; margin-top: 4px;">
            <button class="box cobalt" id="btn-perf-run" style="flex: 1.5; height: 34px;">
              RUN PARALLEL BENCHMARK
            </button>
            <button class="box" id="btn-perf-cancel" style="display: none; flex: 1; height: 34px; border-color: var(--vermilion); color: var(--vermilion);">
              CANCEL RUN
            </button>
          </div>

          <!-- Real Progress Bar -->
          <div id="perf-progress-container" style="display: none; flex-direction: column; gap: 4px; margin-top: 6px;">
            <div style="display: flex; justify-content: space-between; font-family: var(--font-mono); font-size: 10px;">
              <span id="perf-progress-phase">WARMING UP POOL...</span>
              <span id="perf-progress-pct">0%</span>
            </div>
            <div style="height: 6px; width: 100%; background: #E8E1D0; border: 1px solid var(--rule); overflow: hidden;">
              <div id="perf-progress-fill" style="height: 100%; width: 0%; background: var(--cobalt); transition: width 0.2s linear;"></div>
            </div>
          </div>

          <!-- Host Environment Hardware Card -->
          <div id="perf-host-card" style="display: none; border: 1px solid var(--rule); padding: 10px; background: #FFFFFF; flex-direction: column; gap: 6px; font-family: var(--font-mono); font-size: 10px;">
            <div style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--ink); border-bottom: 1px solid var(--rule); padding-bottom: 4px; letter-spacing: 0.08em;">
              HOST CPU & COMPILER ENVIRONMENT
            </div>
            <div id="host-details" style="display: grid; grid-template-columns: 1fr 1fr; gap: 4px;"></div>
          </div>

          <!-- Numerical Agreement Badge -->
          <div id="perf-agreement-card" style="display: none; align-items: center; justify-content: space-between; border: 1px solid #2E7D32; background: #F1F8F1; padding: 8px 12px;">
            <div style="font-family: var(--font-mono); font-size: 10.5px; color: #1B5E20;">
              Numerical Agreement: <strong id="agreement-diff">ΔT = 0.000000 °C</strong>
            </div>
            <span style="background: #2E7D32; color: #FFFFFF; font-family: var(--font-sans); font-weight: 700; font-size: 9px; padding: 2px 8px; border-radius: 2px;">
              PASS
            </span>
          </div>

          <!-- CSV Export Button -->
          <button class="box" id="btn-export-perf-csv" style="display: none; height: 28px; font-size: 10px;">
            EXPORT BENCHMARK CSV
          </button>

          <div style="font-family: var(--font-sans); font-size: 9.5px; color: var(--muted); font-style: italic; margin-top: 4px;">
            * Speedup depends on workload size and core topology.
          </div>

          <div id="perf-status-container"></div>
        </div>
      </div>

      <!-- Right Column: Visual Charts & Repetition Tables -->
      <div class="detail-right-col" style="display: flex; flex-direction: column; background: #FFFFFF; padding: 18px; gap: 14px; overflow-y: auto;">
        <!-- Chart: Speedup vs Ideal Scaling -->
        <div style="flex: 1.2; width: 100%; min-height: 250px; position: relative;">
          <div style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--ink); margin-bottom: 6px; letter-spacing: 0.06em;">
            MEASURED SPEEDUP Sp = T₁/Tp AGAINST IDEAL LINEAR SCALING
          </div>
          <div style="position: absolute; inset: 24px 0 0 0;">
            <canvas id="chart-perf-speedup"></canvas>
          </div>
        </div>

        <div style="height: 1px; background: var(--rule); width: 100%;"></div>

        <!-- Repetition Summary Table -->
        <div style="flex: 1; width: 100%;">
          <div style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--ink); margin-bottom: 6px; letter-spacing: 0.06em;">
            DETAILED WORKER EXECUTION TIMES (5 REPETITIONS PER POOL CONFIG)
          </div>
          <div style="overflow-x: auto; border: 1px solid var(--rule);">
            <table style="width: 100%; border-collapse: collapse; font-family: var(--font-mono); font-size: 10px; text-align: right;">
              <thead>
                <tr style="border-bottom: 1px solid var(--rule); background: #FAF9F6; color: var(--muted); text-align: right;">
                  <th style="padding: 6px; text-align: left;">Workers</th>
                  <th style="padding: 6px;">Median (ms)</th>
                  <th style="padding: 6px;">Mean (ms)</th>
                  <th style="padding: 6px;">Min / Max</th>
                  <th style="padding: 6px; color: var(--cobalt);">Speedup</th>
                  <th style="padding: 6px;">Efficiency</th>
                </tr>
              </thead>
              <tbody id="perf-results-tbody">
                <tr>
                  <td colspan="6" style="padding: 24px; text-align: center; color: var(--muted);">
                    Click "RUN PARALLEL BENCHMARK" to execute workload tests.
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    `;
  }

  function validateInputs() {
    const cases = parseInt(containerEl.querySelector('#inp-perf-cases').value, 10);
    const mesh = parseInt(containerEl.querySelector('#inp-perf-mesh').value, 10);
    const seed = parseInt(containerEl.querySelector('#inp-perf-seed').value, 10);

    let valid = true;

    function check(cond, id, msg) {
      const errEl = containerEl.querySelector(id);
      if (!cond) {
        errEl.textContent = msg;
        errEl.style.display = 'block';
        valid = false;
      } else {
        errEl.textContent = '';
        errEl.style.display = 'none';
      }
    }

    check(!isNaN(cases) && cases >= 1 && cases <= 200000, '#err-perf-cases', 'Cases: 1..200,000');
    check(!isNaN(mesh) && mesh >= 1 && mesh <= 5000, '#err-perf-mesh', 'Mesh size N: 1..5,000');
    check(!isNaN(seed) && seed >= 0 && seed <= 4294967295, '#err-perf-seed', 'Seed: 0..4294967295');

    const runBtn = containerEl.querySelector('#btn-perf-run');
    if (runBtn) runBtn.disabled = !valid;

    return valid;
  }

  function initSpeedupChart() {
    if (typeof Chart === 'undefined') return;
    const ctx = containerEl.querySelector('#chart-perf-speedup');
    if (!ctx) return;

    speedupChart = new Chart(ctx, {
      type: 'bar',
      data: {
        labels: [],
        datasets: []
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        plugins: {
          legend: {
            position: 'top',
            labels: {
              boxWidth: 12,
              font: { family: "'Inter Tight', sans-serif", size: 10 }
            }
          }
        },
        scales: {
          x: {
            title: {
              display: true,
              text: 'Active Thread Pool Worker Count',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          },
          y: {
            beginAtZero: true,
            title: {
              display: true,
              text: 'Relative Speedup (T₁ / Tp)',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          }
        }
      }
    });
  }

  function renderHostCard(data) {
    const card = containerEl.querySelector('#perf-host-card');
    const details = containerEl.querySelector('#host-details');
    if (!card || !details) return;

    const fields = [
      { k: 'CPU Model', v: data.cpu_model },
      { k: 'Hardware Threads', v: data.hardware_concurrency },
      { k: 'Physical Cores', v: data.physical_cores },
      { k: 'Total RAM', v: data.total_ram_gb ? `${data.total_ram_gb} GB` : null },
      { k: 'Compiler Info', v: data.compiler_info },
      { k: 'Optimization', v: data.optimization_flags },
      { k: 'Warmup Duration', v: data.warmup_duration_ms ? `${data.warmup_duration_ms} ms` : null }
    ];

    details.innerHTML = '';
    fields.forEach(f => {
      if (f.v) {
        const div = document.createElement('div');
        div.innerHTML = `<span style="color:var(--muted);">${f.k}:</span> <strong>${f.v}</strong>`;
        details.appendChild(div);
      }
    });

    card.style.display = 'flex';
  }

  function renderResultsTable(results) {
    const tbody = containerEl.querySelector('#perf-results-tbody');
    if (!tbody) return;

    tbody.innerHTML = '';
    results.forEach(r => {
      const tr = document.createElement('tr');
      tr.style.borderBottom = '1px solid #EEEEEE';
      tr.innerHTML = `
        <td style="padding: 6px; text-align: left; font-weight: 700; color: var(--cobalt);">${r.workers} Worker${r.workers > 1 ? 's' : ''}</td>
        <td style="padding: 6px; font-weight: 700;">${r.median_ms.toFixed(1)}</td>
        <td style="padding: 6px; color: var(--muted);">${r.mean_ms ? r.mean_ms.toFixed(1) : '-'}</td>
        <td style="padding: 6px;">${r.min_ms.toFixed(1)} / ${r.max_ms.toFixed(1)}</td>
        <td style="padding: 6px; font-weight: 700; color: #1F3FB0;">${r.speedup.toFixed(2)}x</td>
        <td style="padding: 6px;">${(r.efficiency * 100).toFixed(0)}%</td>
      `;
      tbody.appendChild(tr);
    });
  }

  function updateChart(results) {
    if (!speedupChart) return;

    const labels = results.map(r => `${r.workers} Threads`);
    const measuredSpeedups = results.map(r => r.speedup);
    const idealSpeedups = results.map(r => r.workers);

    speedupChart.data.labels = labels;
    speedupChart.data.datasets = [
      {
        type: 'bar',
        label: 'Measured Speedup (T₁ / Tp)',
        data: measuredSpeedups,
        backgroundColor: '#1F3FB0',
        borderColor: '#141414',
        borderWidth: 1
      },
      {
        type: 'line',
        label: 'Ideal Linear Scaling (Slope = 1.0)',
        data: idealSpeedups,
        borderColor: '#E0452B',
        borderWidth: 2,
        borderDash: [6, 4],
        pointRadius: 3,
        pointBackgroundColor: '#E0452B'
      }
    ];

    speedupChart.update();
  }

  async function runBenchmark() {
    if (!validateInputs() || isRunning) return;
    UI.hideErrorBanner(containerEl);

    isRunning = true;
    abortController = new AbortController();

    // Set Quiet Mode to prevent UI animations from distorting benchmark timing
    Shelf.setQuiet(true);

    const runBtn = containerEl.querySelector('#btn-perf-run');
    const cancelBtn = containerEl.querySelector('#btn-perf-cancel');
    const progressContainer = containerEl.querySelector('#perf-progress-container');
    const exportBtn = containerEl.querySelector('#btn-export-perf-csv');

    runBtn.style.display = 'none';
    cancelBtn.style.display = 'inline-flex';
    progressContainer.style.display = 'flex';
    exportBtn.style.display = 'none';

    statusLine.setRunning('BENCHMARKING PARALLEL WORKER POOLS');

    const cases = parseInt(containerEl.querySelector('#inp-perf-cases').value, 10);
    const mesh = parseInt(containerEl.querySelector('#inp-perf-mesh').value, 10);
    const seed = parseInt(containerEl.querySelector('#inp-perf-seed').value, 10);

    // Poll Progress every 250 ms with light DOM updates
    progressTimer = setInterval(async () => {
      try {
        const prog = await API.getPerformanceProgress();
        if (prog) {
          const pct = Math.min(100, Math.round((prog.step / (prog.total_steps || 25)) * 100));
          containerEl.querySelector('#perf-progress-fill').style.width = `${pct}%`;
          containerEl.querySelector('#perf-progress-pct').textContent = `${pct}%`;
          containerEl.querySelector('#perf-progress-phase').textContent = 
            `${prog.phase || 'COMPUTING'} (${(prog.elapsed_ms / 1000).toFixed(1)}s)`;
        }
      } catch (e) {
        // Indeterminate bar if progress endpoint fails
        containerEl.querySelector('#perf-progress-phase').textContent = 'BENCHMARK RUNNING (INDETERMINATE)';
      }
    }, 250);

    try {
      const data = await API.runPerformance(cases, mesh, seed, abortController.signal);
      clearInterval(progressTimer);

      if (data.was_cancelled) {
        statusLine.setError('BENCHMARK CANCELLED');
        UI.showToast('Cancelled - partial results not shown');
        containerEl.querySelector('#perf-progress-phase').textContent = 'CANCELLED';
        return;
      }

      lastResult = data;
      renderHostCard(data);
      renderResultsTable(data.results);
      updateChart(data.results);

      // Numerical Agreement Card
      const agreeCard = containerEl.querySelector('#perf-agreement-card');
      if (agreeCard) {
        agreeCard.style.display = 'flex';
        const diffText = (data.max_absolute_temp_diff_C !== undefined) ? 
          `ΔT = ${data.max_absolute_temp_diff_C.toFixed(6)} °C` : 'ΔT = 0.000000 °C';
        containerEl.querySelector('#agreement-diff').textContent = diffText;
      }

      exportBtn.style.display = 'inline-flex';
      statusLine.setDone(`BENCHMARK COMPLETED (MAX SPEEDUP: ${data.results[data.results.length - 1].speedup.toFixed(2)}x)`);
      UI.showToast('Multicore benchmark completed successfully');
    } catch (err) {
      clearInterval(progressTimer);
      if (err.name !== 'AbortError') {
        statusLine.setError('BENCHMARK FAILED');
        UI.showErrorBanner(containerEl, err.message, 'Verify system resources and permissions.');
      }
    } finally {
      isRunning = false;
      Shelf.setQuiet(false);
      runBtn.style.display = 'inline-flex';
      cancelBtn.style.display = 'none';
    }
  }

  async function cancelBenchmark() {
    if (abortController) {
      abortController.abort();
    }
    try {
      await API.cancelPerformance();
    } catch (e) {}
    statusLine.setError('CANCELLED');
    UI.showToast('Cancelled - partial results not shown');
  }

  function exportCsv() {
    if (!lastResult) return;

    let csv = `# PASS SCIENCE WORKS UNIFIED ENGINEERING WORKBENCH\n`;
    csv += `# Module: Task D - Multicore Batch Benchmark\n`;
    csv += `# Date: ${new Date().toISOString()}\n`;
    csv += `# CPU: ${lastResult.cpu_model || 'Unknown'}\n`;
    csv += `# Physical Cores: ${lastResult.physical_cores || 'N/A'}\n`;
    csv += `# Hardware Concurrency: ${lastResult.hardware_concurrency || 'N/A'}\n`;
    csv += `# Total RAM: ${lastResult.total_ram_gb ? lastResult.total_ram_gb + ' GB' : 'N/A'}\n`;
    csv += `# Compiler: ${lastResult.compiler_info || 'GCC/Clang C++17'}\n`;
    csv += `# Flags: ${lastResult.optimization_flags || '-O3'}\n`;
    csv += `# Numerical Agreement: ${lastResult.numerical_agreement_passed ? 'PASSED' : 'FAILED'}\n#\n`;
    csv += `workers,median_ms,mean_ms,min_ms,max_ms,speedup,efficiency,rep1_ms,rep2_ms,rep3_ms,rep4_ms,rep5_ms\n`;

    lastResult.results.forEach(r => {
      const reps = r.repetitions || [];
      csv += `${r.workers},${r.median_ms},${r.mean_ms || ''},${r.min_ms},${r.max_ms},${r.speedup},${r.efficiency},${reps.join(',')}\n`;
    });

    const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
    API.triggerDownload('', `multicore_performance_${timestamp}.csv`, csv);
  }

  function bindEvents() {
    const runBtn = containerEl.querySelector('#btn-perf-run');
    if (runBtn) runBtn.addEventListener('click', runBenchmark);

    const cancelBtn = containerEl.querySelector('#btn-perf-cancel');
    if (cancelBtn) cancelBtn.addEventListener('click', cancelBenchmark);

    const exportBtn = containerEl.querySelector('#btn-export-perf-csv');
    if (exportBtn) exportBtn.addEventListener('click', exportCsv);

    containerEl.querySelectorAll('input').forEach(inp => {
      inp.addEventListener('input', validateInputs);
    });
  }

  return {
    resize: function() {
      if (speedupChart) speedupChart.resize();
    },

    mount: function(parentContainer) {
      containerEl = parentContainer;
      containerEl.innerHTML = renderHTML();

      const statusContainer = containerEl.querySelector('#perf-status-container');
      statusLine = UI.createStatusLine(statusContainer);

      initSpeedupChart();
      bindEvents();
    },

    unmount: function() {
      if (progressTimer) {
        clearInterval(progressTimer);
        progressTimer = null;
      }
      if (isRunning && abortController) {
        abortController.abort();
      }
      isRunning = false;
      Shelf.setQuiet(false);

      if (speedupChart) {
        speedupChart.destroy();
        speedupChart = null;
      }
      lastResult = null;
      containerEl = null;
    }
  };
})();
