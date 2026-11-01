/**
 * Unified Engineering Workbench - Embedded Hardware View (Module 6 / Task E)
 * Simulated MCU, FPGA, and JTAG hardware adapters with fault injection, live telemetry, and TAP architecture
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.HardwareView = (function() {
  'use strict';

  let containerEl = null;
  let statusLine = null;
  let devices = [];
  let selectedDeviceId = null;
  let connectedDeviceId = null;
  let telemetryTimer = null;
  let archData = null;

  function renderHTML() {
    return `
      <!-- Left Column: Device Control & Fault Injection -->
      <div class="detail-left-col">
        <div class="detail-text-stack">
          <div class="detail-mask">
            <span class="lbl detail-subtitle">TASK E / EMBEDDED HARDWARE</span>
          </div>
          <div class="detail-mask">
            <h1 class="detail-title">Embedded Hardware</h1>
          </div>
          <div class="detail-mask" style="display: flex; align-items: center; gap: 8px;">
            <span class="lbl detail-meta">MCU / FPGA / JTAG PROBE CONTROLLER</span>
            <span style="background: #FFF0ED; color: var(--vermilion); border: 1px solid var(--vermilion); font-family: var(--font-sans); font-weight: 700; font-size: 9px; padding: 1px 6px; border-radius: 2px;">
              SIMULATED
            </span>
          </div>
          <div class="detail-mask">
            <p class="detail-blurb">
              Simulated heterogeneous hardware testbench: STM32 Cortex-M7 MCU, Xilinx Artix-7 FPGA accelerator, and SEGGER J-Link debug probe with runtime fault injection and brownout recovery.
            </p>
          </div>
          <div class="detail-mask">
            <p class="detail-credit">
              Engineering Workbench assessment module. Candidate ${CONFIG.candidateId}.
            </p>
          </div>
        </div>

        <div class="detail-controls-panel">
          <div class="controls-title">HARDWARE ADAPTER SELECTOR</div>

          <!-- Device Selection Tabs -->
          <div id="hw-device-tabs" style="display: flex; gap: 4px; margin-bottom: 4px;"></div>

          <!-- Device Specs Card -->
          <div id="hw-device-specs" style="border: 1px solid var(--rule); background: #FFFFFF; padding: 10px; font-family: var(--font-mono); font-size: 10px; display: flex; flex-direction: column; gap: 4px;">
            <!-- Rendered dynamically -->
          </div>

          <!-- Connect / Disconnect Buttons -->
          <div style="display: flex; gap: 8px; margin-top: 4px;">
            <button class="box cobalt" id="btn-hw-connect" style="flex: 1; height: 32px;">
              CONNECT DEVICE
            </button>
            <button class="box" id="btn-hw-disconnect" style="display: none; flex: 1; height: 32px; border-color: var(--vermilion); color: var(--vermilion);">
              DISCONNECT
            </button>
          </div>

          <!-- Fault Injection Actions Panel -->
          <div id="hw-fault-panel" style="display: none; flex-direction: column; gap: 6px; margin-top: 8px; border-top: 1px solid var(--rule); padding-top: 10px;">
            <div style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--ink); letter-spacing: 0.08em;">
              FAULT INJECTION & RECOVERY TESTING
            </div>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 6px;">
              <button class="box" id="btn-inject-timeout" style="font-size: 9.5px; height: 26px;">
                INJECT TIMEOUT
              </button>
              <button class="box" id="btn-inject-crc" style="font-size: 9.5px; height: 26px;">
                INJECT CRC ERROR
              </button>
              <button class="box" id="btn-inject-voltage" style="font-size: 9.5px; height: 26px;">
                INJECT VOLTAGE DROP
              </button>
              <button class="box cobalt" id="btn-execute-recovery" style="font-size: 9.5px; height: 26px;">
                EXECUTE RECOVERY
              </button>
            </div>
          </div>

          <!-- Active Fault Alert Banner -->
          <div class="error-banner-mask" style="margin-top: 6px;">
            <div class="error-banner" id="hw-fault-banner">
              <span class="error-banner-title" id="hw-fault-state">FAULT ACTIVE</span>
              <span class="error-banner-text" id="hw-fault-msg"></span>
              <span class="error-banner-hint">Click 'EXECUTE RECOVERY' to reset state machine.</span>
            </div>
          </div>

          <div id="hw-status-container"></div>
        </div>
      </div>

      <!-- Right Column: Live Telemetry, Block Diagram & Architectural Whitepaper -->
      <div class="detail-right-col" style="display: flex; flex-direction: column; background: #FFFFFF; padding: 18px; gap: 14px; overflow-y: auto;">
        <!-- Live Telemetry Stream Card -->
        <div style="width: 100%; border: 1px solid var(--rule); background: #FAF9F6; padding: 12px;">
          <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--rule); padding-bottom: 6px; margin-bottom: 8px;">
            <div style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--ink);">
              LIVE 100ms TELEMETRY STREAM
            </div>
            <div id="telemetry-rate-badge" style="font-family: var(--font-mono); font-size: 9.5px; color: var(--muted);">
              POLLING: DISCONNECTED
            </div>
          </div>
          <div id="telemetry-metrics-grid" style="display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 8px;">
            <div style="font-family: var(--font-mono); font-size: 10px; color: var(--muted); grid-column: 1 / -1; text-align: center; padding: 12px;">
              Connect to a device to begin 100 ms telemetry streaming.
            </div>
          </div>
        </div>

        <!-- Inline SVG System Block Diagram -->
        <div style="width: 100%; border: 1px solid var(--rule); background: #FFFFFF; padding: 12px;">
          <div style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--ink); margin-bottom: 8px;">
            HETEROGENEOUS TESTBENCH INTERFACE ARCHITECTURE
          </div>
          <div style="width: 100%; overflow-x: auto;">
            <svg viewBox="0 0 620 220" width="100%" height="220" style="min-width: 500px; display: block;">
              <!-- Background Grid -->
              <defs>
                <pattern id="diag-grid" width="20" height="20" patternUnits="userSpaceOnUse">
                  <path d="M 20 0 L 0 0 0 20" fill="none" stroke="#F0EFEA" stroke-width="1" />
                </pattern>
              </defs>
              <rect width="620" height="220" fill="url(#diag-grid)" />

              <!-- Host PC -->
              <rect x="20" y="55" width="110" height="110" fill="#141414" rx="4" />
              <text x="75" y="105" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="12" fill="#FFFFFF" text-anchor="middle">HOST PC</text>
              <text x="75" y="122" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="8.5" fill="#E8E1D0" text-anchor="middle">C++17 ENGINE</text>
              <text x="75" y="140" font-family="'Inter Tight', sans-serif" font-weight="500" font-size="8" fill="#AAA" text-anchor="middle">USB 2.0 / 3.0</text>

              <!-- USB Bus Lines -->
              <path d="M 130 80 L 220 50" fill="none" stroke="#1F3FB0" stroke-width="2" />
              <text x="175" y="60" font-family="'Inter Tight', sans-serif" font-size="8" font-weight="600" fill="#1F3FB0" text-anchor="middle">USB-UART</text>

              <path d="M 130 110 L 220 110" fill="none" stroke="#1F3FB0" stroke-width="2" />
              <text x="175" y="104" font-family="'Inter Tight', sans-serif" font-size="8" font-weight="600" fill="#1F3FB0" text-anchor="middle">USB-JTAG</text>

              <path d="M 130 140 L 220 170" fill="none" stroke="#1F3FB0" stroke-width="2" />
              <text x="175" y="162" font-family="'Inter Tight', sans-serif" font-size="8" font-weight="600" fill="#1F3FB0" text-anchor="middle">USB-AXI</text>

              <!-- JTAG Debug Probe (Center Controller) -->
              <rect x="220" y="85" width="110" height="50" fill="#1F3FB0" rx="3" />
              <text x="275" y="110" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="10" fill="#FFFFFF" text-anchor="middle">J-LINK PROBE</text>
              <text x="275" y="124" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="7.5" fill="#E8E1D0" text-anchor="middle">TAP BUS MASTER</text>

              <!-- JTAG Bus Links to Targets -->
              <!-- Link to MCU -->
              <path d="M 330 100 L 450 65" fill="none" stroke="#E0452B" stroke-width="2" stroke-dasharray="3,3" />
              <text x="390" y="78" font-family="'Inter Tight', sans-serif" font-size="8" font-weight="700" fill="#E0452B" text-anchor="middle">TCK/TMS/TDI/TDO</text>

              <!-- Link to FPGA -->
              <path d="M 330 120 L 450 155" fill="none" stroke="#E0452B" stroke-width="2" stroke-dasharray="3,3" />
              <text x="390" y="146" font-family="'Inter Tight', sans-serif" font-size="8" font-weight="700" fill="#E0452B" text-anchor="middle">IEEE 1149.1 CHAIN</text>

              <!-- Target 1: MCU Board -->
              <rect x="450" y="30" width="150" height="70" fill="#EDEBE6" stroke="#141414" stroke-width="1.5" rx="3" />
              <text x="525" y="52" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="10" fill="#141414" text-anchor="middle">STM32H753ZI (MCU)</text>
              <text x="525" y="68" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="8" fill="#1F3FB0" text-anchor="middle">ARM Cortex-M7 @ 480 MHz</text>
              <text x="525" y="84" font-family="'Inter Tight', sans-serif" font-size="7.5" fill="#6D6A63" text-anchor="middle">Domain: 3.3V I/O &bull; 1.8V Core</text>

              <!-- Target 2: FPGA Board -->
              <rect x="450" y="120" width="150" height="70" fill="#EDEBE6" stroke="#141414" stroke-width="1.5" rx="3" />
              <text x="525" y="142" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="10" fill="#141414" text-anchor="middle">ARTIX-7 XC7A100T (FPGA)</text>
              <text x="525" y="158" font-family="'Inter Tight', sans-serif" font-weight="600" font-size="8" fill="#1F3FB0" text-anchor="middle">Parallel FEA Accel Kernel</text>
              <text x="525" y="174" font-family="'Inter Tight', sans-serif" font-size="7.5" fill="#6D6A63" text-anchor="middle">Domain: 1.0V VCCINT &bull; 1.8V AUX</text>
            </svg>
          </div>
        </div>

        <!-- Collapsible Architectural Analysis -->
        <details style="border: 1px solid var(--rule); background: #FAF9F6; padding: 10px; cursor: pointer;">
          <summary style="font-family: var(--font-sans); font-size: 10.5px; font-weight: 700; color: var(--cobalt);">
            Why JTAG is a debug interface, not a compute board
          </summary>
          <div id="hw-arch-content" style="margin-top: 10px; font-family: var(--font-sans); font-size: 11px; line-height: 1.5; color: var(--ink);">
            Loading architectural whitepaper from C++ backend...
          </div>
        </details>
      </div>
    `;
  }

  function renderDeviceTabs() {
    const tabsContainer = containerEl.querySelector('#hw-device-tabs');
    if (!tabsContainer) return;

    tabsContainer.innerHTML = '';
    devices.forEach(d => {
      const btn = document.createElement('button');
      btn.className = `box ${d.id === selectedDeviceId ? 'cobalt' : ''}`;
      btn.textContent = `${d.type}: ${d.name.split(' ')[0]}`;
      btn.addEventListener('click', () => {
        selectedDeviceId = d.id;
        renderDeviceTabs();
        renderDeviceSpecs();
      });
      tabsContainer.appendChild(btn);
    });
  }

  function renderDeviceSpecs() {
    const specsContainer = containerEl.querySelector('#hw-device-specs');
    const connectBtn = containerEl.querySelector('#btn-hw-connect');
    const disconnectBtn = containerEl.querySelector('#btn-hw-disconnect');
    const faultPanel = containerEl.querySelector('#hw-fault-panel');

    const dev = devices.find(d => d.id === selectedDeviceId);
    if (!dev || !specsContainer) return;

    const isConnected = connectedDeviceId === dev.id;

    specsContainer.innerHTML = `
      <div><strong>Device:</strong> ${dev.name}</div>
      <div><strong>Type / Mfr:</strong> ${dev.type} / ${dev.manufacturer}</div>
      <div><strong>VID:PID:</strong> ${dev.vid_pid} &bull; <strong>Serial:</strong> ${dev.serial}</div>
      <div><strong>Voltages:</strong> ${dev.voltages}</div>
      <div><strong>Connector:</strong> ${dev.connector} &bull; <strong>Protocol:</strong> ${dev.protocol}</div>
    `;

    if (isConnected) {
      connectBtn.style.display = 'none';
      disconnectBtn.style.display = 'inline-flex';
      faultPanel.style.display = 'flex';
    } else {
      connectBtn.style.display = 'inline-flex';
      disconnectBtn.style.display = 'none';
      faultPanel.style.display = 'none';
    }
  }

  function renderTelemetry(data) {
    const grid = containerEl.querySelector('#telemetry-metrics-grid');
    const badge = containerEl.querySelector('#telemetry-rate-badge');
    const banner = containerEl.querySelector('#hw-fault-banner');
    if (!grid) return;

    badge.textContent = `LINK: ONLINE &bull; SEQ #${data.sequence_id} &bull; 100ms`;
    badge.style.color = '#2E7D32';

    // Handle Active Fault Banners
    if (data.state !== 'NORMAL') {
      banner.classList.add('is-visible');
      containerEl.querySelector('#hw-fault-state').textContent = data.state;
      containerEl.querySelector('#hw-fault-msg').textContent = data.status_message;
    } else {
      banner.classList.remove('is-visible');
    }

    grid.innerHTML = '';
    function addMetric(label, val, unit, color) {
      const card = document.createElement('div');
      card.style.border = '1px solid var(--rule)';
      card.style.background = '#FFFFFF';
      card.style.padding = '8px';
      card.style.display = 'flex';
      card.style.flexDirection = 'column';
      card.style.gap = '2px';
      card.innerHTML = `
        <span style="font-family:var(--font-sans); font-size:9px; color:var(--muted); text-transform:uppercase;">${label}</span>
        <span style="font-family:var(--font-mono); font-size:12px; font-weight:700; color:${color || 'var(--ink)'};">${val} <small style="font-size:9px;">${unit}</small></span>
      `;
      grid.appendChild(card);
    }

    if (data.mcu && (selectedDeviceId.includes('mcu') || connectedDeviceId.includes('mcu'))) {
      addMetric('Core Temp', data.mcu.core_temp_C, '°C', '#1F3FB0');
      addMetric('VCC Rail', data.mcu.vcc_volts, 'V', data.mcu.vcc_volts < 2.5 ? '#E0452B' : '#141414');
      addMetric('Uptime', (data.mcu.uptime_ms / 1000).toFixed(1), 's');
      addMetric('ADC Channel 0', data.mcu.adc_ch0, 'raw');
      addMetric('GPIO Port State', data.mcu.gpio_state, '');
    } else if (data.fpga && (selectedDeviceId.includes('fpga') || connectedDeviceId.includes('fpga'))) {
      addMetric('Die Temp', data.fpga.die_temp_C, '°C', '#1F3FB0');
      addMetric('VCCINT Core', data.fpga.vccint_volts, 'V');
      addMetric('VCCAUX Rail', data.fpga.vccaux_volts, 'V');
      addMetric('Logic LUT Util', data.fpga.lut_util_pct, '%');
      addMetric('MMCM / PLL Lock', data.fpga.pll_locked ? 'LOCKED' : 'UNLOCKED', '', '#2E7D32');
      addMetric('Bitstream Hash', data.fpga.bitstream_hash, '');
    } else if (data.jtag) {
      addMetric('Target VREF', data.jtag.target_vref_volts, 'V', data.jtag.target_vref_volts < 2.0 ? '#E0452B' : '#1F3FB0');
      addMetric('TCK Frequency', data.jtag.tck_frequency_mhz, 'MHz');
      addMetric('TAP IDCODE', data.jtag.tap_detected_idcode, '');
      addMetric('Detected Core', data.jtag.tap_device_name, '');
      addMetric('Packets Tx', data.jtag.packets_transferred, 'pkts');
    }
  }

  function startTelemetry() {
    stopTelemetry();
    telemetryTimer = setInterval(async () => {
      if (!connectedDeviceId) return;
      try {
        const data = await API.hardwareTelemetry(connectedDeviceId);
        renderTelemetry(data);
      } catch (err) {
        // Handle stream drops
      }
    }, 100);
  }

  function stopTelemetry() {
    if (telemetryTimer) {
      clearInterval(telemetryTimer);
      telemetryTimer = null;
    }
    const badge = containerEl?.querySelector('#telemetry-rate-badge');
    if (badge) {
      badge.textContent = 'POLLING: DISCONNECTED';
      badge.style.color = 'var(--muted)';
    }
  }

  async function connectDevice() {
    if (!selectedDeviceId) return;
    statusLine.setRunning(`CONNECTING TO ${selectedDeviceId}`);
    try {
      await API.hardwareConnect(selectedDeviceId);
      connectedDeviceId = selectedDeviceId;
      renderDeviceSpecs();
      startTelemetry();
      statusLine.setDone(`CONNECTED: ${selectedDeviceId}`);
      UI.showToast(`Connected to ${selectedDeviceId}`);
    } catch (err) {
      statusLine.setError('CONNECTION FAILED');
      UI.showErrorBanner(containerEl, err.message, 'Check adapter availability.');
    }
  }

  async function disconnectDevice() {
    if (!connectedDeviceId) return;
    statusLine.setRunning(`DISCONNECTING`);
    try {
      await API.hardwareDisconnect(connectedDeviceId);
      connectedDeviceId = null;
      stopTelemetry();
      renderDeviceSpecs();
      statusLine.setIdle('DEVICE DISCONNECTED');
      UI.showToast('Hardware link disconnected');
    } catch (err) {
      statusLine.setError('DISCONNECT FAILED');
    }
  }

  async function injectFault(type) {
    if (!connectedDeviceId) return;
    statusLine.setRunning(`INJECTING ${type.toUpperCase()}`);
    try {
      await API.hardwareInjectFault(`${connectedDeviceId}:${type}`);
      statusLine.setError(`FAULT INJECTED: ${type.toUpperCase()}`);
      UI.showToast(`Fault ${type} injected into ${connectedDeviceId}`);
    } catch (err) {
      statusLine.setError('FAULT INJECTION FAILED');
    }
  }

  async function executeRecovery() {
    if (!connectedDeviceId) return;
    statusLine.setRunning('EXECUTING RECOVERY SEQUENCE');
    try {
      await API.hardwareRecover(connectedDeviceId);
      const banner = containerEl.querySelector('#hw-fault-banner');
      if (banner) banner.classList.remove('is-visible');
      statusLine.setDone('RECOVERY COMPLETED (NORMAL STATE RESTORED)');
      UI.showToast('Adapter recovered to normal state');
    } catch (err) {
      statusLine.setError('RECOVERY FAILED');
    }
  }

  async function loadInitialData() {
    try {
      devices = await API.hardwareDiscover();
      if (devices.length > 0) {
        selectedDeviceId = devices[0].id;
        renderDeviceTabs();
        renderDeviceSpecs();
      }

      archData = await API.hardwareArchitecture();
      const archContent = containerEl.querySelector('#hw-arch-content');
      if (archContent && archData) {
        archContent.innerHTML = `
          <p style="margin-bottom:8px;"><strong>${archData.system_name}</strong></p>
          <p style="margin-bottom:8px;">${archData.jtag_architectural_explanation}</p>
          <div style="background:#FFFFFF; border:1px solid var(--rule); padding:8px; margin-top:6px;">
            <strong>Recommended Integration Architecture:</strong>
            <pre style="font-family:var(--font-mono); font-size:10px; margin-top:4px; white-space:pre-wrap;">${archData.safe_integration_plan}</pre>
          </div>
        `;
      }
    } catch (err) {
      statusLine.setError('DISCOVERY FAILED');
    }
  }

  function bindEvents() {
    const connectBtn = containerEl.querySelector('#btn-hw-connect');
    if (connectBtn) connectBtn.addEventListener('click', connectDevice);

    const disconnectBtn = containerEl.querySelector('#btn-hw-disconnect');
    if (disconnectBtn) disconnectBtn.addEventListener('click', disconnectDevice);

    const timeoutBtn = containerEl.querySelector('#btn-inject-timeout');
    if (timeoutBtn) timeoutBtn.addEventListener('click', () => injectFault('timeout'));

    const crcBtn = containerEl.querySelector('#btn-inject-crc');
    if (crcBtn) crcBtn.addEventListener('click', () => injectFault('crc'));

    const voltBtn = containerEl.querySelector('#btn-inject-voltage');
    if (voltBtn) voltBtn.addEventListener('click', () => injectFault('voltage'));

    const recoverBtn = containerEl.querySelector('#btn-execute-recovery');
    if (recoverBtn) recoverBtn.addEventListener('click', executeRecovery);
  }

  return {
    mount: function(parentContainer) {
      containerEl = parentContainer;
      containerEl.innerHTML = renderHTML();

      const statusContainer = containerEl.querySelector('#hw-status-container');
      statusLine = UI.createStatusLine(statusContainer);

      bindEvents();
      loadInitialData();
    },

    unmount: function() {
      stopTelemetry();
      if (connectedDeviceId) {
        API.hardwareDisconnect(connectedDeviceId).catch(() => {});
        connectedDeviceId = null;
      }
      containerEl = null;
    }
  };
})();
