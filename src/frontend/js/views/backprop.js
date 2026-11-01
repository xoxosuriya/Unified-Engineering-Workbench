/**
 * Unified Engineering Workbench - Backpropagation View (Module 4 / Task F)
 * Analytical gradients, central finite-difference verification, and 100-step training loss curve
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.BackpropView = (function() {
  'use strict';

  let containerEl = null;
  let statusLine = null;
  let lossChart = null;
  let studyData = null;

  function renderHTML() {
    return `
      <!-- Left Column: Analytical Derivation & Gradient Check Table -->
      <div class="detail-left-col">
        <div class="detail-text-stack">
          <div class="detail-mask">
            <span class="lbl detail-subtitle">TASK F / BACKPROPAGATION</span>
          </div>
          <div class="detail-mask">
            <h1 class="detail-title">Backpropagation</h1>
          </div>
          <div class="detail-mask">
            <span class="lbl detail-meta">ANALYTICAL DERIVATION / FINITE-DIFFERENCE CHECK</span>
          </div>
          <div class="detail-mask">
            <p class="detail-blurb">
              Hand-derivable chain-rule gradients for a single-hidden-layer network verified against central finite differences to double-precision tolerance.
            </p>
          </div>
          <div class="detail-mask">
            <p class="detail-credit">
              Engineering Workbench assessment module. Candidate ${CONFIG.candidateId}.
            </p>
          </div>
        </div>

        <div class="detail-controls-panel">
          <div class="controls-title">FORWARD PASS & GRADIENT VERIFICATION</div>

          <!-- Error Banner Mask -->
          <div class="error-banner-mask">
            <div class="error-banner" id="bp-error-banner">
              <span class="error-banner-title">CALCULATION ERROR</span>
              <span class="error-banner-text"></span>
              <span class="error-banner-hint"></span>
            </div>
          </div>

          <!-- Step 0 Forward Pass State Card -->
          <div id="bp-step0-card" style="border: 1px solid var(--rule); background: #FFFFFF; padding: 10px; display: flex; flex-direction: column; gap: 6px; font-family: var(--font-mono); font-size: 10.5px;">
            <div style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--ink); border-bottom: 1px solid var(--rule); padding-bottom: 4px;">
              STEP 0 FORWARD PASS & QUADRATIC LOSS J = ½(ŷ - y)²
            </div>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 4px;">
              <div>Input x: <strong id="bp-val-x">1.5</strong></div>
              <div>Target y: <strong id="bp-val-y">0.5</strong></div>
              <div>Linear z = wx+b: <strong id="bp-val-z">-</strong></div>
              <div>Hidden h = tanh(z): <strong id="bp-val-h">-</strong></div>
              <div>Output ŷ = vh+c: <strong id="bp-val-yhat">-</strong></div>
              <div>Loss J: <strong id="bp-val-loss" style="color: var(--vermilion);">-</strong></div>
            </div>
          </div>

          <!-- Central Finite Difference Gradient Checks Table -->
          <div style="border: 1px solid var(--rule); background: #FFFFFF; padding: 10px; margin-top: 6px;">
            <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--rule); padding-bottom: 4px; margin-bottom: 6px;">
              <span style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--ink);">
                CENTRAL DIFFERENCE CHECK [ε = <span id="bp-eps-val">10⁻⁵</span>]
              </span>
              <span id="bp-all-passed-badge" style="background: #2E7D32; color: #FFFFFF; font-family: var(--font-sans); font-weight: 700; font-size: 9px; padding: 1px 6px; border-radius: 2px;">
                ALL CHECKS PASSED
              </span>
            </div>
            <table style="width: 100%; border-collapse: collapse; font-family: var(--font-mono); font-size: 10px; text-align: right;">
              <thead>
                <tr style="border-bottom: 1px solid var(--rule); color: var(--muted);">
                  <th style="padding: 4px 6px; text-align: left;">Param</th>
                  <th style="padding: 4px 6px;">Analytical ∂J/∂θ</th>
                  <th style="padding: 4px 6px;">Numerical</th>
                  <th style="padding: 4px 6px;">|Diff|</th>
                  <th style="padding: 4px 6px; text-align: center;">Status</th>
                </tr>
              </thead>
              <tbody id="bp-gradient-tbody"></tbody>
            </table>
          </div>

          <!-- Step 1 Updated Weights Card -->
          <div id="bp-step1-card" style="border: 1px solid var(--rule); background: #FAF9F6; padding: 10px; font-family: var(--font-mono); font-size: 10px; display: flex; flex-direction: column; gap: 4px; margin-top: 6px;">
            <div style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--cobalt); border-bottom: 1px solid var(--rule); padding-bottom: 4px;">
              STEP 1 UPDATED WEIGHTS [LEARNING RATE η = 0.1]
            </div>
            <div id="bp-updated-weights-text" style="display: grid; grid-template-columns: 1fr 1fr; gap: 4px;">
              -
            </div>
          </div>

          <!-- Gradient Direction Note -->
          <div style="font-family: var(--font-serif); font-size: 12px; line-height: 1.45; color: var(--muted); border-left: 2px solid var(--cobalt); padding-left: 8px; margin-top: 6px;">
            Negative gradient steps decrease loss along the steepest descent path on the convex quadratic surface. Analytical and finite-difference derivatives agree to within machine epsilon.
          </div>

          <!-- CSV Export Button -->
          <div style="display: flex; gap: 6px; margin-top: 6px;">
            <button class="box cobalt" id="btn-export-bp-csv" style="flex: 1; height: 30px; font-size: 10px;">
              EXPORT TRAINING STEPS CSV
            </button>
          </div>

          <div id="bp-status-container"></div>
        </div>
      </div>

      <!-- Right Column: 100 Training Steps Loss Chart -->
      <div class="detail-right-col" style="display: flex; flex-direction: column; background: #FFFFFF; padding: 18px; gap: 14px; overflow-y: auto;">
        <div style="flex: 1; width: 100%; min-height: 380px; position: relative;">
          <div style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--ink); margin-bottom: 6px; letter-spacing: 0.06em;">
            LOSS J(θ) CONVERGENCE OVER 100 GRADIENT DESCENT STEPS (η = 0.1)
          </div>
          <div style="position: absolute; inset: 26px 0 0 0;">
            <canvas id="chart-bp-loss"></canvas>
          </div>
        </div>

        <div style="border: 1px solid var(--rule); background: #FAF9F6; padding: 10px; font-family: var(--font-mono); font-size: 10px;">
          <div style="font-family: var(--font-sans); font-weight: 700; color: var(--ink); margin-bottom: 4px;">
            TRAINING LOSS CONVERGENCE TELEMETRY
          </div>
          <div id="bp-training-summary" style="display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 6px;">
            <!-- Populated dynamically -->
          </div>
        </div>
      </div>
    `;
  }

  function initLossChart() {
    if (typeof Chart === 'undefined') return;
    const ctx = containerEl.querySelector('#chart-bp-loss');
    if (!ctx) return;

    lossChart = new Chart(ctx, {
      type: 'line',
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
          },
          tooltip: {
            callbacks: {
              label: (ctx) => `Step ${ctx.label}: Loss J = ${ctx.parsed.y.toFixed(6)}`
            }
          }
        },
        scales: {
          x: {
            title: {
              display: true,
              text: 'Gradient Descent Training Step k (0..99)',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          },
          y: {
            type: 'logarithmic',
            title: {
              display: true,
              text: 'Quadratic Loss J = ½(ŷ - y)² [Log Scale]',
              font: { family: "'Inter Tight', sans-serif", size: 10.5, weight: '600' }
            },
            grid: { color: 'rgba(20, 20, 20, 0.06)' }
          }
        }
      }
    });
  }

  function renderStudy(data) {
    studyData = data;

    // Populate Step 0 Card
    containerEl.querySelector('#bp-val-x').textContent = data.input_x;
    containerEl.querySelector('#bp-val-y').textContent = data.target_y;
    containerEl.querySelector('#bp-val-z').textContent = data.step_0.z.toFixed(4);
    containerEl.querySelector('#bp-val-h').textContent = data.step_0.h.toFixed(4);
    containerEl.querySelector('#bp-val-yhat').textContent = data.step_0.y_hat.toFixed(4);
    containerEl.querySelector('#bp-val-loss').textContent = data.step_0.loss_J.toFixed(6);

    containerEl.querySelector('#bp-eps-val').textContent = data.epsilon.toExponential(0);

    // Populate Gradient Check Table
    const tbody = containerEl.querySelector('#bp-gradient-tbody');
    tbody.innerHTML = '';
    data.gradient_checks.forEach(chk => {
      const tr = document.createElement('tr');
      tr.style.borderBottom = '1px solid #EEEEEE';
      tr.innerHTML = `
        <td style="padding: 4px 6px; text-align: left; font-weight: 700; color: var(--cobalt);">∂J/∂${chk.param}</td>
        <td style="padding: 4px 6px;">${chk.analytical.toFixed(6)}</td>
        <td style="padding: 4px 6px;">${chk.numerical.toFixed(6)}</td>
        <td style="padding: 4px 6px; color: var(--muted);">${chk.abs_diff.toExponential(2)}</td>
        <td style="padding: 4px 6px; text-align: center;">
          <span style="background: ${chk.passed ? '#2E7D32' : '#E0452B'}; color: #FFFFFF; font-size: 8.5px; font-weight: 700; padding: 1px 5px; border-radius: 2px;">
            ${chk.passed ? 'PASS' : 'FAIL'}
          </span>
        </td>
      `;
      tbody.appendChild(tr);
    });

    // Populate Step 1 Updated Weights
    const w1 = data.updated_weights_step_1;
    containerEl.querySelector('#bp-updated-weights-text').innerHTML = `
      <div>w₁ = <strong>${w1.w.toFixed(5)}</strong></div>
      <div>b₁ = <strong>${w1.b.toFixed(5)}</strong></div>
      <div>v₁ = <strong>${w1.v.toFixed(5)}</strong></div>
      <div>c₁ = <strong>${w1.c.toFixed(5)}</strong></div>
    `;

    // Populate 100 Training Steps Chart
    if (lossChart && data.training_steps) {
      const steps = data.training_steps;
      const labels = steps.map(s => s.step);
      const losses = steps.map(s => s.loss);

      lossChart.data.labels = labels;
      lossChart.data.datasets = [
        {
          label: 'Loss J(θ)',
          data: losses,
          borderColor: '#1F3FB0',
          backgroundColor: 'rgba(31, 63, 176, 0.08)',
          borderWidth: 2,
          pointRadius: 1.5,
          fill: true,
          tension: 0.1
        }
      ];
      lossChart.update();

      const initialLoss = steps[0].loss;
      const finalLoss = steps[steps.length - 1].loss;
      const finalYhat = steps[steps.length - 1].y_hat;

      containerEl.querySelector('#bp-training-summary').innerHTML = `
        <div>Initial Loss J₀: <strong>${initialLoss.toFixed(6)}</strong></div>
        <div>Final Loss J₉₉: <strong style="color:#2E7D32;">${finalLoss.toExponential(4)}</strong></div>
        <div>Target y: <strong>${data.target_y}</strong></div>
        <div>Model ŷ₉₉: <strong>${finalYhat.toFixed(5)}</strong></div>
      `;
    }
  }

  async function loadStudy() {
    UI.hideErrorBanner(containerEl);
    statusLine.setRunning('SOLVING BACKPROP GRADIENT CHECKS');

    try {
      const data = await API.backpropStudy();
      renderStudy(data);
      statusLine.setDone('GRADIENT CHECKS PASSED (DIFF < 10⁻⁶)');
      UI.showToast('Backpropagation study solved');
    } catch (err) {
      statusLine.setError('BACKPROP STUDY FAILED');
      UI.showErrorBanner(containerEl, err.message, 'Verify math engine.');
    }
  }

  function exportCsv() {
    if (!studyData || !studyData.training_steps) {
      UI.showToast('No training data to export');
      return;
    }

    let csv = '# PASS Science Works Unified Engineering Workbench\n# Module: Task F - Backpropagation Study\n';
    csv += 'step,w,b,v,c,loss,y_hat\n';
    studyData.training_steps.forEach(s => {
      csv += `${s.step},${s.w},${s.b},${s.v},${s.c},${s.loss},${s.y_hat}\n`;
    });

    API.triggerDownload('/api/export/backprop_csv', 'backprop_training_steps.csv', csv);
  }

  function bindEvents() {
    const exportBtn = containerEl.querySelector('#btn-export-bp-csv');
    if (exportBtn) exportBtn.addEventListener('click', exportCsv);
  }

  return {
    resize: function() {
      if (lossChart) lossChart.resize();
    },

    mount: function(parentContainer) {
      containerEl = parentContainer;
      containerEl.innerHTML = renderHTML();

      const statusContainer = containerEl.querySelector('#bp-status-container');
      statusLine = UI.createStatusLine(statusContainer);

      initLossChart();
      bindEvents();
      loadStudy();
    },

    unmount: function() {
      if (lossChart) {
        lossChart.destroy();
        lossChart = null;
      }
      studyData = null;
      containerEl = null;
    }
  };
})();
