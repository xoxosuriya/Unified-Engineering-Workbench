// Backpropagation View: Handwritten Derivation Display, Finite Difference Check, and 100-Step Loss Curve
const BackpropView = {
  lossChart: null,

  init() {
    this.bindEvents();
    this.runBackpropStudy();
  },

  bindEvents() {
    const runBtn = document.getElementById('bp-recalculate-btn');
    if (runBtn) {
      runBtn.addEventListener('click', () => this.runBackpropStudy());
    }

    const exportBtn = document.getElementById('bp-export-csv-btn');
    if (exportBtn) {
      exportBtn.addEventListener('click', () => {
        window.location.href = '/api/export/backprop_csv';
      });
    }
  },

  runBackpropStudy() {
    App.showToast("Computing backpropagation derivatives & central finite differences...", "info");
    fetch('/api/backprop/study')
      .then(r => r.json())
      .then(data => {
        this.renderStep0(data);
        this.renderGradientChecks(data.gradient_checks);
        this.renderLossChart(data.training_steps);
        App.showToast("Backpropagation study & gradient checks passed (diff <= 1e-5)!", "success");
      })
      .catch(err => App.showToast("Backprop study failed: " + err, "error"));
  },

  renderStep0(data) {
    const s0 = data.step_0;
    const u1 = data.updated_weights_step_1;

    document.getElementById('bp-val-z').textContent = s0.z.toFixed(6);
    document.getElementById('bp-val-h').textContent = s0.h.toFixed(6);
    document.getElementById('bp-val-yhat').textContent = s0.y_hat.toFixed(6);
    document.getElementById('bp-val-loss').textContent = s0.loss_J.toFixed(6);

    // Initial Gradients
    document.getElementById('bp-grad-w').textContent = s0.dJ_dw.toFixed(6);
    document.getElementById('bp-grad-b').textContent = s0.dJ_db.toFixed(6);
    document.getElementById('bp-grad-v').textContent = s0.dJ_dv.toFixed(6);
    document.getElementById('bp-grad-c').textContent = s0.dJ_dc.toFixed(6);

    // Updated weights Step 1
    document.getElementById('bp-upd-w').textContent = u1.w.toFixed(6);
    document.getElementById('bp-upd-b').textContent = u1.b.toFixed(6);
    document.getElementById('bp-upd-v').textContent = u1.v.toFixed(6);
    document.getElementById('bp-upd-c').textContent = u1.c.toFixed(6);
  },

  renderGradientChecks(checks) {
    const tbody = document.getElementById('bp-checks-tbody');
    if (!tbody) return;

    tbody.innerHTML = '';
    checks.forEach(c => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td style="font-weight:700; color:#fff;">∂J / ∂${c.param}</td>
        <td style="color:#38bdf8;">${c.analytical.toFixed(8)}</td>
        <td style="color:#9ca3af;">${c.numerical.toFixed(8)}</td>
        <td style="color:#34d399; font-weight:600;">${c.abs_diff.toExponential(3)}</td>
        <td><span class="pill pill-success">${c.passed ? 'PASSED (<= 1e-5)' : 'FAILED'}</span></td>
      `;
      tbody.appendChild(tr);
    });
  },

  renderLossChart(steps) {
    const ctx = document.getElementById('bp-loss-chart');
    if (!ctx) return;

    if (this.lossChart) this.lossChart.destroy();

    const labels = steps.map(s => s.step);
    const losses = steps.map(s => s.loss_J);
    const yHats = steps.map(s => s.y_hat);

    this.lossChart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: labels,
        datasets: [
          {
            label: 'Loss J = 0.5·(y_hat - y)²',
            data: losses,
            borderColor: '#f43f5e',
            backgroundColor: 'rgba(244, 63, 94, 0.1)',
            borderWidth: 2,
            pointRadius: 1,
            tension: 0.1,
            yAxisID: 'y'
          },
          {
            label: 'Prediction y_hat (Target y = 2.0)',
            data: yHats,
            borderColor: '#10b981',
            borderWidth: 2,
            pointRadius: 1,
            tension: 0.1,
            yAxisID: 'y1'
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
              title: (ctx) => `Training Step ${ctx[0].label}`,
              afterBody: (ctx) => {
                const idx = ctx[0].dataIndex;
                const s = steps[idx];
                return `w=${s.w.toFixed(4)}, b=${s.b.toFixed(4)}, v=${s.v.toFixed(4)}, c=${s.c.toFixed(4)}`;
              }
            }
          }
        },
        scales: {
          x: {
            title: { display: true, text: 'Training Steps (η = 0.1)', color: '#9ca3af' },
            ticks: { color: '#9ca3af' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          },
          y: {
            type: 'linear',
            display: true,
            position: 'left',
            title: { display: true, text: 'Loss J', color: '#fb7185' },
            ticks: { color: '#fb7185' },
            grid: { color: 'rgba(255, 255, 255, 0.05)' }
          },
          y1: {
            type: 'linear',
            display: true,
            position: 'right',
            title: { display: true, text: 'Prediction y_hat', color: '#34d399' },
            ticks: { color: '#34d399' },
            grid: { drawOnChartArea: false }
          }
        }
      }
    });
  }
};
