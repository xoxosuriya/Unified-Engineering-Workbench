// Main Application Controller & View Router
const App = {
  currentView: 'geometry',

  init() {
    this.bindNavigation();
    this.checkBackendStatus();

    // Initialize all views
    GeometryView.init();
    AnsysView.init();
    ThermalView.init();
    PerformanceView.init();
    HardwareView.init();
    BackpropView.init();

    // Auto-load sample STL on start for instant gratification
    setTimeout(() => {
      GeometryView.loadSample();
    }, 400);
  },

  bindNavigation() {
    document.querySelectorAll('.nav-tab').forEach(tab => {
      tab.addEventListener('click', (e) => {
        const viewId = tab.dataset.view;
        this.switchView(viewId);
      });
    });

    const shutdownBtn = document.getElementById('shutdown-server-btn');
    if (shutdownBtn) {
      shutdownBtn.addEventListener('click', () => {
        if (confirm("Shut down the local Workbench C++ background server?")) {
          fetch('/api/shutdown', { method: 'POST' })
            .then(() => {
              App.showToast("Workbench server shutting down. You may close this browser tab.", "warning");
              document.getElementById('header-status-badge').innerHTML = '<span class="status-dot" style="background:#ef4444; box-shadow:none;"></span> OFFLINE';
            })
            .catch(() => {});
        }
      });
    }
  },

  switchView(viewId) {
    this.currentView = viewId;

    // Update active tab buttons
    document.querySelectorAll('.nav-tab').forEach(tab => {
      tab.classList.toggle('active', tab.dataset.view === viewId);
    });

    // Update view panels
    document.querySelectorAll('.view-panel').forEach(panel => {
      panel.classList.toggle('active', panel.id === `view-${viewId}`);
    });

    // Resize 3D viewports if switched to them
    if (viewId === 'geometry') {
      setTimeout(() => GeometryView.onResize(), 50);
    } else if (viewId === 'ansys') {
      setTimeout(() => AnsysView.onResize(), 50);
    }
  },

  checkBackendStatus() {
    fetch('/api/status')
      .then(r => r.json())
      .then(data => {
        document.getElementById('header-cpu-info').textContent = `${data.cpu_model} (${data.hardware_concurrency}T)`;
      })
      .catch(() => {
        const badge = document.getElementById('header-status-badge');
        if (badge) {
          badge.innerHTML = '<span class="status-dot" style="background:#ef4444;"></span> DISCONNECTED';
          badge.style.color = '#ef4444';
        }
      });
  },

  showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    if (!container) return;

    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;

    container.appendChild(toast);

    setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transform = 'translateY(10px)';
      toast.style.transition = 'all 0.3s ease';
      setTimeout(() => toast.remove(), 300);
    }, 4000);
  }
};

window.addEventListener('DOMContentLoaded', () => {
  App.init();
});
