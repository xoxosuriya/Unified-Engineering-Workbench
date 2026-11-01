/**
 * Unified Engineering Workbench - Thermal FEA 1D Rod View (Module 3 / Task C)
 * 1D Steady-State Heat Conduction FEA solver, convergence study, and Chart.js analytics
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.ThermalView = (function() {
  'use strict';

  let containerEl = null;
  let statusLine = null;
  let profileChart = null;
  let convergenceChart = null;
  let lastConvergenceData = null;
  let lastSolveData = null;

  function renderHTML() {
    return `
      <!-- Left Column: Controls & Parameters -->
      <div class="detail-left-col">
        <div class="detail-text-stack">
          <div class="detail-mask">
            <span class="lbl detail-subtitle">TASK C / THERMAL FEA</span>
          </div>
          <div class="detail-mask">
            <h1 class="detail-title">Thermal FEA, 1D Rod</h1>
          </div>
          <div class="detail-mask">
            <span class="lbl detail-meta">STEADY-STATE FEA / 2-NODE ELEMENTS / C++17</span>
          </div>
          <div class="detail-mask">
            <p class="detail-blurb">
              Steady-state 1D heat conduction with uniform volumetric source q, linear 2-node elements, and asymptotic mesh convergence against the exact analytical solution.
            </p>
          </div>
          <div class="detail-mask">
            <p class="detail-credit">
              Engineering Workbench assessment module. Candidate ${CONFIG.candidateId}.
            </p>
          </div>
        </div>

        <div class="detail-controls-panel">
          <div class="controls-title">FEA PARAMETER CONFIGURATION</div>

          <!-- Error Banner Mask -->
          <div class="error-banner-mask">
            <div class="error-banner" id="fea-error-banner">
              <span class="error-banner-title">CALCULATION ERROR</span>
              <span class="error-banner-text"></span>
              <span class="error-banner-hint"></span>
            </div>
          </div>

          <!-- Form inputs -->
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px;">
            <div class="control-group">
              <label class="control-label">Length L (m)</label>
              <input type="number" id="inp-fea-L" class="control-input" value="1.0" step="0.1" />
              <span class="input-error-msg" id="err-fea-L"></span>
            </div>
            <div class="control-group">
              <label class="control-label">Area A (m²)</label>
              <input type="number" id="inp-fea-A" class="control-input" value="0.0001" step="0.00005" />
              <span class="input-error-msg" id="err-fea-A"></span>
            </div>
            <div class="control-group">
              <label class="control-label">Conductivity k (W/m·K)</label>
              <input type="number" id="inp-fea-k" class="control-input" value="10.0" step="1" />
              <span class="input-error-msg" id="err-fea-k"></span>
            </div>
            <div class="control-group">
              <label class="control-label">Source q (W/m³)</label>
              <input type="number" id="inp-fea-q" class="control-input" value="10000.0" step="1000" />
              <span class="input-error-msg" id="err-fea-q"></span>
            </div>
            <div class="control-group">
              <label class="control-label">Boundary T(0) (°C)</label>
              <input type="number" id="inp-fea-T0" class="control-input" value="100.0" step="5" />
              <span class="input-error-msg" id="err-fea-T0"></span>
            </div>
            <div class="control-group">
              <label class="control-label">Boundary T(L) (°C)</label>
              <input type="number" id="inp-fea-TL" class="control-input" value="20.0" step="5" />
              <span class="input-error-msg" id="err-fea-TL"></span>
            </div>
          </div>

          <div class="control-group">
            <label class="control-label">Mesh Elements (N)</label>
            <div style="display: flex; gap: 6px;">
              <select id="sel-fea-N" class="control-select" style="flex: 1;">
                <option value="10">10 Elements (h = 0.100 m)</option>
                <option value="20">20 Elements (h = 0.050 m)</option>
                <option value="40">40 Elements (h = 0.025 m)</option>
                <option value="80">80 Elements (h = 0.0125 m)</option>
                <option value="custom">Custom N...</option>
              </select>
              <input type="number" id="inp-fea-N-custom" class="control-input" style="display: none; width: 90px;" min="1" max="100000" value="50" />
            </div>
            <span class="input-error-msg" id="err-fea-N"></span>
          </div>

          <div style="display: flex; gap: 8px; margin-top: 4px;">
            <button class="box" id="btn-fea-solve" style="flex: 1; height: 32px;">
              SOLVE SELECTED MESH
            </button>
            <button class="box cobalt" id="btn-fea-convergence" style="flex: 1.4; height: 32px;">
              RUN CONVERGENCE STUDY
            </button>
          </div>

          <!-- Convergence Data Table -->
          <div id="fea-table-wrap" style="display: none; margin-top: 6px; border: 1px solid var(--rule); background: #FFFFFF; padding: 8px;">
            <div style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--ink); border-bottom: 1px solid var(--rule); padding-bottom: 4px;">
              MESH REFINEMENT CONVERGENCE (N = 10, 20, 40, 80)
            </div>
            <div style="overflow-x: auto; margin-top: 6px;">
              <table style="width: 100%; border-collapse: collapse; font-family: var(--font-mono); font-size: 10px; text-align: right;">
                <thead>
                  <tr style="border-bottom: 1px solid var(--rule); color: var(--muted); text-align: right;">
                    <th style="padding: 4px 6px; text-align: left;">N</th>
                    <th style="padding: 4px 6px;">h (m)</th>
                    <th style="padding: 4px 6px;">Max Mid Err</th>
                    <th style="padding: 4px 6px;">qh²/8k</th>
                    <th style="padding: 4px 6px;">Nodal Err</th>
                    <th style="padding: 4px 6px;">Ratio</th>
                    <th style="padding: 4px 6px;">Rate</th>
                  </tr>
                </thead>
                <tbody id="fea-convergence-tbody"></tbody>
              </table>
            </div>
            <div style="font-family: var(--font-sans); font-size: 9.5px; color: var(--muted); font-style: italic; margin-top: 6px;">
              * Midpoint error uses FE linear interpolation.
            </div>
          </div>

          <!-- Export Buttons -->
          <div style="display: flex; gap: 6px; margin-top: 4px;">
            <button class="box" id="btn-export-fea-csv" style="flex: 1; height: 26px; font-size: 9.5px;">
              EXPORT CONVERGENCE CSV
            </button>
            <button class="box" id="btn-export-fea-profile" style="flex: 1; height: 26px; font-size: 9.5px;">
              EXPORT PROFILES CSV
            </button>
          </div>

          <div id="fea-status-container"></div>
        </div>
      </div>

      <!-- Right Column: Dual Charts Visualization -->
      <div class="detail-right-col" style="display: flex; flex-direction: column; background: #FFFFFF; padding: 18px; gap: 14px; overflow-y: auto;">
        <!-- Chart 1: Temperature Profile T(x) -->
        <div style="flex: 1; width: 100%; min-height: 260px; position: relative;">
          <div style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--ink); margin-bottom: 6px; letter-spacing: 0.06em;">
            TEMPERATURE DISTRIBUTION T(x) ACROSS ROD LENGTH (m)
          </div>
          <div style="position: absolute; inset: 24px 0 0 0;">
            <canvas id="chart-fea-profile"></canvas>
          </div>
        </div>

        <div style="height: 1px; background: var(--rule); width: 100%;"></div>

        <!-- Chart 2: Log-Log Midpoint Error vs h -->
        <div style="flex: 1; width: 100%; min-height: 240px; position: relative;">
          <div style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--ink); margin-bottom: 6px; letter-spacing: 0.06em;">
            ASYMPTOTIC CONVERGENCE: LOG-LOG MIDPOINT ERROR vs ELEMENT SIZE h (SLOPE ≈ 2.0)
          </div>
          <div style="position: absolute; inset: 24px 0 0 0;">
            <canvas id="chart-fea-convergence"></canvas>
          </div>
        </div>
      </div>
    `;
  }

  function getFormValues() {
    const L = parseFloat(containerEl.querySelector('#inp-fea-L').value);
    const A = parseFloat(containerEl.querySelector('#inp-fea-A').value);
    const k = parseFloat(containerEl.querySelector('#inp-fea-k').value);
    const q = parseFloat(containerEl.querySelector('#inp-fea-q').value);
    const T0 = parseFloat(containerEl.querySelector('#inp-fea-T0').value);
    const TL = parseFloat(containerEl.querySelector('#inp-fea-TL').value);

    let N = 10;
    const selN = containerEl.querySelector('#sel-fea-N').value;
    if (selN === 'custom') {
      N = parseInt(containerEl.querySelector('#inp-fea-N-custom').value, 10);
    } else {
      N = parseInt(selN, 10);
    }

    return { L, A, k, q, T0, TL, N };
  }

  function validateForm() {
    const vals = getFormValues();
    let valid = true;

    function check(cond, errId, msg) {
      const errEl = containerEl.querySelector(errId);
      if (!cond) {
        errEl.textContent = msg;
        errEl.style.display = 'block';
        valid = false;
      } else {
        errEl.textContent = '';
        errEl.style.display = 'none';
      }
    }

    check(vals.L > 0 && isFinite(vals.L), '#err-fea-L', 'L must be > 0');
    check(vals.A > 0 && isFinite(vals.A), '#err-fea-A', 'A must be > 0');
    check(vals.k >= 1e-6 && vals.k <= 1e5 && isFinite(vals.k), '#err-fea-k', 'k must be in 1e-6..1e5');
    check(isFinite(vals.q), '#err-fea-q', 'q must be finite');
    check(vals.T0 >= -273.15 && vals.T0 <= 5000 && isFinite(vals.T0), '#err-fea-T0', 'T0 in -273.15..5000 °C');
    check(vals.TL >= -273.15 && vals.TL <= 5000 && isFinite(vals.TL), '#err-fea-TL', 'TL in -273.15..5000 °C');
    check(Number.isInteger(vals.N) && vals.N >= 1 && vals.N <= 100000, '#err-fea-N', 'N must be integer 1..100,000');

    const solveBtn = containerEl.querySelector('#btn-fea-solve');
    const convBtn = containerEl.querySelector('#btn-fea-convergence');
    if (solveBtn) solveBtn.disabled = !valid;
    if (convBtn) convBtn.disabled = !valid;

    return valid;
  }

  // Calculate Dense Exact Solution (400 points)
  function computeExactCurve(L, k, q, T0, TL, numPoints = 400) {
    const data = [];
    const step = L / (numPoints - 1);
    for (let i = 0; i < numPoints; i++) {
      const x = i * step;
      const t = T0 - (T0 - TL) * (x / L) + (q / (2.0 * k)) * x * (L - x);
      data.push({ x: Math.round(x * 10000) / 10000, y: Math.round(t * 100) / 100 });
    }
    return data;
  }

  function initProfileChart() {
    if (typeof Chart === 'undefined') return;
    const ctx = containerEl.querySelector('#chart-fea-profile');
    if (!ctx) return;

    profileChart = new Chart(ctx, {
      type: 'line',
      data: {
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
          },
          tooltip: {
            callbacks: {
              label: (ctx) => `${ctx.dataset.label}: x=${ctx.parsed.x.toFixed(3)}m, T=${ctx.parsed.y.toFixed(2)}°C`
            }
          }
        },
        scales: {
          x: {
            type: 'linear',
            title: {
              display: true,
              text: 'Position along rod x (m)',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          },
          y: {
            title: {
              display: true,
              text: 'Temperature T (°C)',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          }
        }
      }
    });
  }

  function initConvergenceChart() {
    if (typeof Chart === 'undefined') return;
    const ctx = containerEl.querySelector('#chart-fea-convergence');
    if (!ctx) return;

    convergenceChart = new Chart(ctx, {
      type: 'line',
      data: {
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
            type: 'logarithmic',
            title: {
              display: true,
              text: 'Element Size h = L/N (m) [Log Scale]',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          },
          y: {
            type: 'logarithmic',
            title: {
              display: true,
              text: 'Max Element Midpoint Error (°C) [Log Scale]',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          }
        }
      }
    });
  }

  function renderConvergenceTable(rows) {
    const tableWrap = containerEl.querySelector('#fea-table-wrap');
    const tbody = containerEl.querySelector('#fea-convergence-tbody');
    if (!tableWrap || !tbody) return;

    tbody.innerHTML = '';
    rows.forEach(r => {
      const tr = document.createElement('tr');
      tr.style.borderBottom = '1px solid #EEEEEE';
      tr.innerHTML = `
        <td style="padding: 4px 6px; text-align: left; font-weight: 700; color: var(--cobalt);">${r.N}</td>
        <td style="padding: 4px 6px;">${r.h.toFixed(4)}</td>
        <td style="padding: 4px 6px;">${r.max_midpoint_error.toFixed(5)}</td>
        <td style="padding: 4px 6px; color: var(--muted);">${r.theoretical_midpoint_error.toFixed(5)}</td>
        <td style="padding: 4px 6px;">${r.max_nodal_error.toExponential(2)}</td>
        <td style="padding: 4px 6px; font-weight: 600;">${r.reduction_ratio ? r.reduction_ratio.toFixed(2) : '-'}</td>
        <td style="padding: 4px 6px; font-weight: 700; color: #2E7D32;">${r.observed_rate ? r.observed_rate.toFixed(2) : '-'}</td>
      `;
      tbody.appendChild(tr);
    });

    tableWrap.style.display = 'block';
  }

  async function runConvergenceStudy() {
    if (!validateForm()) return;
    UI.hideErrorBanner(containerEl);
    statusLine.setRunning('RUNNING MESH CONVERGENCE (N=10, 20, 40, 80)');

    const vals = getFormValues();

    try {
      const data = await API.convergenceFea({
        L: vals.L,
        A: vals.A,
        k: vals.k,
        q: vals.q,
        T0: vals.T0,
        TL: vals.TL
      });

      lastConvergenceData = data;
      renderConvergenceTable(data.rows);

      // Plot all 4 meshes overlaid on exact curve in Chart 1
      if (profileChart) {
        const exactPoints = computeExactCurve(vals.L, vals.k, vals.q, vals.T0, vals.TL);
        const meshColors = ['#9E9E9E', '#FF9800', '#2196F3', '#1F3FB0'];

        const datasets = [
          {
            label: 'Exact Analytical Curve (400 pts)',
            data: exactPoints,
            borderColor: '#E0452B',
            borderWidth: 2.5,
            pointRadius: 0,
            tension: 0.1
          }
        ];

        data.solves.forEach((s, idx) => {
          const points = s.x.map((xi, i) => ({ x: xi, y: s.T_fea[i] }));
          datasets.push({
            label: `Mesh N=${s.N} (h=${s.h}m)`,
            data: points,
            borderColor: meshColors[idx],
            borderWidth: 1.5,
            pointRadius: idx === 0 ? 3 : 2,
            pointBackgroundColor: meshColors[idx],
            borderDash: idx === 0 ? [4, 4] : []
          });

          // Element midpoint markers for N=10
          if (s.N === 10) {
            const midPoints = s.midpoints.map(m => ({ x: m.x_mid, y: m.T_fea_mid }));
            datasets.push({
              label: 'N=10 Midpoints (Max Err)',
              data: midPoints,
              borderColor: '#141414',
              backgroundColor: '#1F3FB0',
              pointRadius: 4,
              pointStyle: 'triangle',
              showLine: false
            });
          }
        });

        profileChart.data.datasets = datasets;
        profileChart.update();
      }

      // Plot Log-Log Convergence Chart (Chart 2)
      if (convergenceChart) {
        const measuredPoints = data.rows.map(r => ({ x: r.h, y: r.max_midpoint_error }));
        const theoreticalPoints = data.rows.map(r => ({ x: r.h, y: r.theoretical_midpoint_error }));

        convergenceChart.data.datasets = [
          {
            label: 'Measured Max Midpoint Error',
            data: measuredPoints,
            borderColor: '#1F3FB0',
            backgroundColor: '#1F3FB0',
            borderWidth: 2,
            pointRadius: 5
          },
          {
            label: 'Theoretical qh²/8k (Slope = 2.0)',
            data: theoreticalPoints,
            borderColor: '#E0452B',
            borderWidth: 1.8,
            borderDash: [6, 4],
            pointRadius: 3
          }
        ];
        convergenceChart.update();
      }

      statusLine.setDone('CONVERGENCE STUDY SOLVED (OBSERVED RATE ≈ 2.00)');
      UI.showToast('Thermal convergence study completed');
    } catch (err) {
      statusLine.setError('CONVERGENCE FAILED');
      UI.showErrorBanner(containerEl, err.message, 'Check parameter bounds and connectivity.');
    }
  }

  async function solveSelectedMesh() {
    if (!validateForm()) return;
    UI.hideErrorBanner(containerEl);
    const vals = getFormValues();

    statusLine.setRunning(`SOLVING MESH N=${vals.N}`);

    try {
      const s = await API.solveFea(vals);
      lastSolveData = s;

      if (profileChart) {
        const exactPoints = computeExactCurve(vals.L, vals.k, vals.q, vals.T0, vals.TL);
        const feaPoints = s.x.map((xi, i) => ({ x: xi, y: s.T_fea[i] }));
        const midPoints = s.midpoints.map(m => ({ x: m.x_mid, y: m.T_fea_mid }));

        profileChart.data.datasets = [
          {
            label: 'Exact Analytical Curve',
            data: exactPoints,
            borderColor: '#E0452B',
            borderWidth: 2.5,
            pointRadius: 0
          },
          {
            label: `FEA Solution (N=${s.N}, h=${s.h}m)`,
            data: feaPoints,
            borderColor: '#1F3FB0',
            borderWidth: 2,
            pointRadius: 3,
            pointBackgroundColor: '#1F3FB0'
          },
          {
            label: 'Element Midpoint Interps',
            data: midPoints,
            borderColor: '#141414',
            backgroundColor: '#FF9800',
            pointRadius: 4,
            pointStyle: 'triangle',
            showLine: false
          }
        ];
        profileChart.update();
      }

      statusLine.setDone(`SOLVED N=${s.N}: MAX MIDPOINT ERR = ${s.max_midpoint_error.toFixed(5)} °C`);
      UI.showToast(`Mesh N=${s.N} solved successfully`);
    } catch (err) {
      statusLine.setError('SOLVE FAILED');
      UI.showErrorBanner(containerEl, err.message, 'Verify inputs are valid.');
    }
  }

  function exportConvergenceCsv() {
    if (!lastConvergenceData) {
      UI.showToast('Please run the convergence study first');
      return;
    }
    let csv = '# PASS Science Works Unified Engineering Workbench\n# Module: Task C - Thermal FEA Convergence\n';
    csv += 'N,h_m,max_midpoint_error_C,theoretical_error_C,nodal_error_C,reduction_ratio,observed_rate\n';
    lastConvergenceData.rows.forEach(r => {
      csv += `${r.N},${r.h},${r.max_midpoint_error},${r.theoretical_midpoint_error},${r.max_nodal_error},${r.reduction_ratio || ''},${r.observed_rate || ''}\n`;
    });
    API.triggerDownload('/api/export/fea_csv', 'fea_convergence_study.csv', csv);
  }

  function exportProfilesCsv() {
    if (!lastConvergenceData && !lastSolveData) {
      UI.showToast('Please solve a mesh first');
      return;
    }
    let csv = '# PASS Science Works Unified Engineering Workbench\n# Module: Task C - Temperature Profiles\n';
    if (lastConvergenceData) {
      csv += 'x_m,T_exact_C,T_fea_N10,T_fea_N20,T_fea_N40,T_fea_N80\n';
      const s0 = lastConvergenceData.solves[0];
      s0.x.forEach((xi, i) => {
        csv += `${xi},${s0.T_exact[i]},${s0.T_fea[i]},${lastConvergenceData.solves[1].T_fea[i*2] || ''},${lastConvergenceData.solves[2].T_fea[i*4] || ''},${lastConvergenceData.solves[3].T_fea[i*8] || ''}\n`;
      });
    } else if (lastSolveData) {
      csv += 'x_m,T_fea_C,T_exact_C\n';
      lastSolveData.x.forEach((xi, i) => {
        csv += `${xi},${lastSolveData.T_fea[i]},${lastSolveData.T_exact[i]}\n`;
      });
    }
    API.triggerDownload('/api/export/fea_profile_csv', 'fea_temperature_profiles.csv', csv);
  }

  function bindEvents() {
    const selN = containerEl.querySelector('#sel-fea-N');
    const customInp = containerEl.querySelector('#inp-fea-N-custom');
    if (selN && customInp) {
      selN.addEventListener('change', () => {
        if (selN.value === 'custom') {
          customInp.style.display = 'inline-block';
        } else {
          customInp.style.display = 'none';
        }
        validateForm();
      });
    }

    // Input validations on change
    containerEl.querySelectorAll('input').forEach(inp => {
      inp.addEventListener('input', validateForm);
    });

    const solveBtn = containerEl.querySelector('#btn-fea-solve');
    if (solveBtn) solveBtn.addEventListener('click', solveSelectedMesh);

    const convBtn = containerEl.querySelector('#btn-fea-convergence');
    if (convBtn) convBtn.addEventListener('click', runConvergenceStudy);

    const expConvBtn = containerEl.querySelector('#btn-export-fea-csv');
    if (expConvBtn) expConvBtn.addEventListener('click', exportConvergenceCsv);

    const expProfBtn = containerEl.querySelector('#btn-export-fea-profile');
    if (expProfBtn) expProfBtn.addEventListener('click', exportProfilesCsv);
  }

  return {
    resize: function() {
      if (profileChart) profileChart.resize();
      if (convergenceChart) convergenceChart.resize();
    },

    mount: function(parentContainer) {
      containerEl = parentContainer;
      containerEl.innerHTML = renderHTML();

      const statusContainer = containerEl.querySelector('#fea-status-container');
      statusLine = UI.createStatusLine(statusContainer);

      initProfileChart();
      initConvergenceChart();
      bindEvents();

      // Automatically run convergence study on first open as specified
      runConvergenceStudy();
    },

    unmount: function() {
      if (profileChart) {
        profileChart.destroy();
        profileChart = null;
      }
      if (convergenceChart) {
        convergenceChart.destroy();
        convergenceChart = null;
      }
      lastConvergenceData = null;
      lastSolveData = null;
      containerEl = null;
    }
  };
})();
