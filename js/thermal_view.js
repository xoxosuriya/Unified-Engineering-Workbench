// Thermal View: 1D Thermal FEA Steady-State Rod Solver & Convergence Charts
const ThermalView = {
  profileChart: null,
  convergenceChart: null,

  init() {
    this.bindEvents();
    // Run initial convergence study
    this.runConvergenceStudy();
  },

  bindEvents() {
    const solveBtn = document.getElementById('fea-solve-btn');
    if (solveBtn) {
      solveBtn.addEventListener('click', () => this.solveSingle());
    }

    const convBtn = document.getElementById('fea-run-convergence-btn');
    if (convBtn) {
      convBtn.addEventListener('click', () => this.runConvergenceStudy());
    }

    const exportBtn = document.getElementById('fea-export-csv-btn');
    if (exportBtn) {
      exportBtn.addEventListener('click', () => {
        window.location.href = '/api/export/fea_csv';
      });
    }
  },

  getParamsFromInputs() {
    return {
      length_L: parseFloat(document.getElementById('fea-param-L').value) || 1.0,
      area_A: parseFloat(document.getElementById('fea-param-A').value) || 1e-4,
      conductivity_k: parseFloat(document.getElementById('fea-param-k').value) || 10.0,
      heat_gen_q: parseFloat(document.getElementById('fea-param-q').value) || 10000.0,
      temp_T0: parseFloat(document.getElementById('fea-param-T0').value) || 100.0,
      temp_TL: parseFloat(document.getElementById('fea-param-TL').value) || 20.0,
      num_elements_N: parseInt(document.getElementById('fea-param-N').value) || 10
    };
  },

  solveSingle() {
    const p = this.getParamsFromInputs();
    const query = `L=${p.length_L}&A=${p.area_A}&k=${p.conductivity_k}&q=${p.heat_gen_q}&T0=${p.temp_T0}&TL=${p.temp_TL}&N=${p.num_elements_N}`;
    
    App.showToast(`Solving 1D Thermal FEA for N=${p.num_elements_N}...`, "info");
    fetch(`/api/fea/solve?${query}`)
      .then(r => r.json())
      .then(data => {
        this.renderProfileChart(data);
        App.showToast(`Solved! Max Midpoint Error: ${data.max_midpoint_error.toFixed(6)} °C`, "success");
      })
      .catch(err => App.showToast("Solve failed: " + err, "error"));
  },

  runConvergenceStudy() {
    App.showToast("Running Mesh Convergence Study (N = 10, 20, 40, 80)...", "info");
    fetch('/api/fea/convergence')
      .then(r => r.json())
      .then(data => {
        this.renderConvergenceTable(data.rows);
        this.renderConvergenceChart(data.rows);
        if (data.solves && data.solves.length > 0) {
          // Render N=10 profile by default
          this.renderProfileChart(data.solves[0]);
        }
        App.showToast("Mesh convergence study completed with quadratic O(h²) rate!", "success");
      })
      .catch(err => App.showToast("Convergence study failed: " + err, "error"));
  },

  renderProfileChart(solve) {
    const ctx = document.getElementById('fea-profile-chart');
    if (!ctx) return;

    if (this.profileChart) this.profileChart.destroy();

    const labels = solve.x.map(val => val.toFixed(3));
    
    // Also build high-res exact curve for visual comparison
    const exactData = solve.T_exact;
    const feaData = solve.T_fea;

    this.profileChart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: labels,
        datasets: [
          {
            label: `FE Solution (N=${solve.N})`,
            data: feaData,
            borderColor: '#06b6d4',
            backgroundColor: 'rgba(6, 182, 212, 0.15)',
            borderWidth: 2,
            pointBackgroundColor: '#06b6d4',
            pointRadius: 4,
            tension: 0
          },
          {
            label: 'Exact Analytical T_exact(x)',
            data: exactData,
            borderColor: '#f59e0b',
            borderDash: [5, 5],
            borderWidth: 2,
            pointRadius: 0,
            tension: 0.1
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
              label: (ctx) => `${ctx.dataset.label}: ${ctx.parsed.y.toFixed(4)} °C`
            }
          }
        },
        scales: {
          x: {
            title: { display: true, text: 'Axial Position x [m]', color: '#9ca3af' },
            ticks: { color: '#9ca3af' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          },
          y: {
            title: { display: true, text: 'Temperature T [°C]', color: '#9ca3af' },
            ticks: { color: '#9ca3af' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          }
        }
      }
    });
  },

  renderConvergenceTable(rows) {
    const tbody = document.getElementById('fea-convergence-tbody');
    if (!tbody) return;

    tbody.innerHTML = '';
    rows.forEach(r => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td style="font-weight:700; color:#fff;">N = ${r.N}</td>
        <td>${r.h.toFixed(4)} m</td>
        <td style="color:#38bdf8; font-weight:600;">${r.max_midpoint_error.toFixed(6)} °C</td>
        <td style="color:#9ca3af;">${r.theoretical_midpoint_error.toFixed(6)} °C</td>
        <td style="color:#34d399; font-weight:600;">${r.max_nodal_error.toExponential(2)} °C</td>
        <td style="font-weight:600;">${r.reduction_ratio.toFixed(2)}x</td>
        <td><span class="pill pill-success">${r.observed_rate.toFixed(3)} (O(h²))</span></td>
      `;
      tbody.appendChild(tr);
    });
  },

  renderConvergenceChart(rows) {
    const ctx = document.getElementById('fea-convergence-chart');
    if (!ctx) return;

    if (this.convergenceChart) this.convergenceChart.destroy();

    const labels = rows.map(r => `N=${r.N} (h=${r.h.toFixed(3)}m)`);
    const midpointErrors = rows.map(r => r.max_midpoint_error);
    const theoreticalErrors = rows.map(r => r.theoretical_midpoint_error);

    this.convergenceChart = new Chart(ctx, {
      type: 'bar',
      data: {
        labels: labels,
        datasets: [
          {
            label: 'Measured Midpoint Error (°C)',
            data: midpointErrors,
            backgroundColor: 'rgba(6, 182, 212, 0.7)',
            borderColor: '#06b6d4',
            borderWidth: 1
          },
          {
            label: 'Theoretical Error q·h²/(8k) (°C)',
            data: theoreticalErrors,
            backgroundColor: 'rgba(245, 158, 11, 0.7)',
            borderColor: '#f59e0b',
            borderWidth: 1
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
              afterBody: () => 'Convergence rate p = 2.000 (Exact quadratic reduction by 4x per mesh halving)'
            }
          }
        },
        scales: {
          x: { ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255, 255, 255, 0.05)' } },
          y: {
            title: { display: true, text: 'Absolute Midpoint Error [°C]', color: '#9ca3af' },
            ticks: { color: '#9ca3af' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          }
        }
      }
    });
  }
};
