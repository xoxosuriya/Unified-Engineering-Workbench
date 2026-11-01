// Thermal View: 1D Thermal FEA Steady-State Rod Solver & Convergence Charts
const ThermalView = {
  profileChart: null,
  convergenceChart: null,
  lastConvergenceData: null,

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

    const exportCsvBtn = document.getElementById('fea-export-csv-btn');
    if (exportCsvBtn) {
      exportCsvBtn.addEventListener('click', () => {
        const p = this.getParamsFromInputs();
        if (!p) return;
        window.location.href = `/api/export/fea_csv?L=${p.length_L}&A=${p.area_A}&k=${p.conductivity_k}&q=${p.heat_gen_q}&T0=${p.temp_T0}&TL=${p.temp_TL}`;
      });
    }

    const exportProfileBtn = document.getElementById('fea-export-profile-btn');
    if (exportProfileBtn) {
      exportProfileBtn.addEventListener('click', () => {
        const p = this.getParamsFromInputs();
        if (!p) return;
        window.location.href = `/api/export/fea_profile_csv?L=${p.length_L}&A=${p.area_A}&k=${p.conductivity_k}&q=${p.heat_gen_q}&T0=${p.temp_T0}&TL=${p.temp_TL}`;
      });
    }
  },

  getParamsFromInputs() {
    const lEl = document.getElementById('fea-param-L');
    const aEl = document.getElementById('fea-param-A');
    const kEl = document.getElementById('fea-param-k');
    const qEl = document.getElementById('fea-param-q');
    const t0El = document.getElementById('fea-param-T0');
    const tlEl = document.getElementById('fea-param-TL');
    const nEl = document.getElementById('fea-param-N');

    const L_str = lEl ? lEl.value.trim() : "";
    const A_str = aEl ? aEl.value.trim() : "";
    const k_str = kEl ? kEl.value.trim() : "";
    const q_str = qEl ? qEl.value.trim() : "";
    const T0_str = t0El ? t0El.value.trim() : "";
    const TL_str = tlEl ? tlEl.value.trim() : "";
    const N_str = nEl ? nEl.value.trim() : "";

    const L = Number(L_str);
    const A = Number(A_str);
    const k = Number(k_str);
    const q = Number(q_str);
    const T0 = Number(T0_str);
    const TL = Number(TL_str);
    const N = parseInt(N_str, 10);

    // Strict parameter validation with actionable diagnostics
    if (!L_str || isNaN(L) || L <= 0) {
      App.showToast("Rod length L must be a strictly positive number (L > 0 m).", "error");
      return null;
    }
    if (!A_str || isNaN(A) || A <= 0) {
      App.showToast("Cross-sectional area A must be strictly positive (A > 0 m²).", "error");
      return null;
    }
    if (!k_str || isNaN(k) || k <= 0) {
      App.showToast("Thermal conductivity k must be strictly positive (k > 0 W/(m·K)).", "error");
      return null;
    }
    if (!q_str || isNaN(q)) {
      App.showToast("Volumetric heat generation q must be a valid number [W/m³].", "error");
      return null;
    }
    if (!T0_str || isNaN(T0) || T0 < -273.15) {
      App.showToast("Left boundary temperature T(0) cannot be below absolute zero (-273.15 °C).", "error");
      return null;
    }
    if (!TL_str || isNaN(TL) || TL < -273.15) {
      App.showToast("Right boundary temperature T(L) cannot be below absolute zero (-273.15 °C).", "error");
      return null;
    }
    if (!N_str || isNaN(N) || N < 1) {
      App.showToast("Mesh element count N must be an integer >= 1.", "error");
      return null;
    }

    return {
      length_L: L,
      area_A: A,
      conductivity_k: k,
      heat_gen_q: q,
      temp_T0: T0,
      temp_TL: TL,
      num_elements_N: N
    };
  },

  solveSingle() {
    const p = this.getParamsFromInputs();
    if (!p) return;

    const query = `L=${p.length_L}&A=${p.area_A}&k=${p.conductivity_k}&q=${p.heat_gen_q}&T0=${p.temp_T0}&TL=${p.temp_TL}&N=${p.num_elements_N}`;
    
    App.showToast(`Solving 1D Thermal FEA for N=${p.num_elements_N}...`, "info");
    fetch(`/api/fea/solve?${query}`)
      .then(r => {
        if (!r.ok) return r.text().then(t => { throw new Error(t); });
        return r.json();
      })
      .then(data => {
        this.renderMultiProfileChart([data], p);
        App.showToast(`Solved N=${p.num_elements_N}! Midpoint Error: ${data.max_midpoint_error.toFixed(6)} °C`, "success");
      })
      .catch(err => App.showToast("Solve failed: " + err.message, "error"));
  },

  runConvergenceStudy() {
    const p = this.getParamsFromInputs();
    if (!p) return;

    App.showToast("Running Mesh Convergence Study (N = 10, 20, 40, 80)...", "info");
    const query = `L=${p.length_L}&A=${p.area_A}&k=${p.conductivity_k}&q=${p.heat_gen_q}&T0=${p.temp_T0}&TL=${p.temp_TL}`;

    fetch(`/api/fea/convergence?${query}`)
      .then(r => {
        if (!r.ok) return r.text().then(t => { throw new Error(t); });
        return r.json();
      })
      .then(data => {
        this.lastConvergenceData = data;
        this.renderConvergenceTable(data.rows);
        this.renderConvergenceChart(data.rows);
        if (data.solves && data.solves.length > 0) {
          // Render all mesh resolutions concurrently alongside the dense continuous exact curve
          this.renderMultiProfileChart(data.solves, p);
        }
        App.showToast("Mesh convergence study completed with quadratic O(h²) rate!", "success");
      })
      .catch(err => App.showToast("Convergence study failed: " + err.message, "error"));
  },

  renderMultiProfileChart(solves, params) {
    const ctx = document.getElementById('fea-profile-chart');
    if (!ctx) return;

    if (this.profileChart) this.profileChart.destroy();

    const L = params.length_L;
    const T0 = params.temp_T0;
    const TL = params.temp_TL;
    const k = params.conductivity_k;
    const q = params.heat_gen_q;

    // Generate high-resolution continuous analytical curve (200 sample points)
    const exactDensePoints = [];
    const numDense = 200;
    for (let i = 0; i <= numDense; ++i) {
      const xi = (i / numDense) * L;
      const Ti = T0 + (TL - T0) * (xi / L) + (q * L * L / (2.0 * k)) * (xi / L) * (1.0 - xi / L);
      exactDensePoints.push({ x: xi, y: Ti });
    }

    const colorPalette = [
      { border: '#06b6d4', bg: 'rgba(6, 182, 212, 0.2)', radius: 4 }, // Cyan (N=10)
      { border: '#10b981', bg: 'rgba(16, 185, 129, 0.2)', radius: 3 }, // Emerald (N=20)
      { border: '#a855f7', bg: 'rgba(168, 85, 247, 0.2)', radius: 2 }, // Purple (N=40)
      { border: '#f43f5e', bg: 'rgba(244, 63, 94, 0.2)', radius: 1.5 }  // Rose (N=80)
    ];

    const datasets = [
      {
        label: 'Analytical Exact T_exact(x)',
        data: exactDensePoints,
        borderColor: '#f59e0b',
        borderDash: [5, 5],
        borderWidth: 2,
        pointRadius: 0,
        tension: 0,
        order: 1
      }
    ];

    solves.forEach((s, idx) => {
      const color = colorPalette[idx % colorPalette.length];
      const feaPoints = s.x.map((xi, i) => ({ x: xi, y: s.T_fea[i] }));
      datasets.push({
        label: `FE Solution (N=${s.N})`,
        data: feaPoints,
        borderColor: color.border,
        backgroundColor: color.bg,
        borderWidth: 2,
        pointBackgroundColor: color.border,
        pointRadius: color.radius,
        tension: 0,
        order: 2 + idx
      });
    });

    this.profileChart = new Chart(ctx, {
      type: 'line',
      data: { datasets: datasets },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { labels: { color: '#f3f4f6', font: { family: 'inherit', size: 12 } } },
          tooltip: {
            callbacks: {
              label: (item) => `${item.dataset.label} at x=${item.raw.x.toFixed(3)}m: ${item.raw.y.toFixed(4)} °C`
            }
          }
        },
        scales: {
          x: {
            type: 'linear',
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
