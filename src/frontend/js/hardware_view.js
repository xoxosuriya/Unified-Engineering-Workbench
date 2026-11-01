// Hardware View: Embedded Integration Dashboard, Mock Adapters, and Fault Recovery
const HardwareView = {
  telemetryInterval: null,
  activeDeviceId: 'mcu_stm32h7',

  init() {
    this.bindEvents();
    this.discoverDevices();
    this.startTelemetryLoop();
  },

  bindEvents() {
    // Device selector tabs
    document.querySelectorAll('.hw-device-tab').forEach(tab => {
      tab.addEventListener('click', (e) => {
        document.querySelectorAll('.hw-device-tab').forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        this.activeDeviceId = tab.dataset.deviceId;
        this.updateDeviceCard();
      });
    });

    // Connect / Disconnect Buttons
    const connectBtn = document.getElementById('hw-connect-btn');
    if (connectBtn) {
      connectBtn.addEventListener('click', () => this.toggleConnect());
    }

    // Fault Injection Buttons
    const injectTimeoutBtn = document.getElementById('hw-inject-timeout-btn');
    if (injectTimeoutBtn) {
      injectTimeoutBtn.addEventListener('click', () => this.injectFault('timeout'));
    }

    const injectCrcBtn = document.getElementById('hw-inject-crc-btn');
    if (injectCrcBtn) {
      injectCrcBtn.addEventListener('click', () => this.injectFault('crc'));
    }

    // Recover Button
    const recoverBtn = document.getElementById('hw-recover-btn');
    if (recoverBtn) {
      recoverBtn.addEventListener('click', () => this.recoverFault());
    }
  },

  discoverDevices() {
    fetch('/api/hardware/discover')
      .then(r => r.json())
      .then(devices => {
        this.renderDeviceList(devices);
      })
      .catch(err => App.showToast("Device discovery failed: " + err, "error"));
  },

  renderDeviceList(devices) {
    const listEl = document.getElementById('hw-device-quick-list');
    if (!listEl) return;

    listEl.innerHTML = '';
    devices.forEach(d => {
      const item = document.createElement('div');
      item.className = 'metric-card';
      item.style.cursor = 'pointer';
      item.innerHTML = `
        <div class="metric-label" style="display:flex; justify-content:space-between;">
          <span>${d.type}</span>
          <span class="pill pill-cyan">SIMULATED</span>
        </div>
        <div style="font-size:13px; font-weight:700; color:#fff; margin-top:4px;">${d.name}</div>
        <div style="font-size:11px; color:var(--text-muted); margin-top:2px;">VID/PID: ${d.vid_pid}</div>
      `;
      item.addEventListener('click', () => {
        document.querySelectorAll('.hw-device-tab').forEach(t => {
          if (t.dataset.deviceId === d.id) t.click();
        });
      });
      listEl.appendChild(item);
    });
  },

  toggleConnect() {
    const btn = document.getElementById('hw-connect-btn');
    const isConnected = btn.textContent.includes('Disconnect');
    const endpoint = isConnected ? '/api/hardware/disconnect' : '/api/hardware/connect';

    fetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: this.activeDeviceId
    })
      .then(r => r.json())
      .then(res => {
        if (res.success) {
          App.showToast(isConnected ? "Device Disconnected" : "Device Connected & Streaming", "info");
        } else {
          App.showToast("Connection failed (fault active or port busy)", "error");
        }
      })
      .catch(err => App.showToast("Connection error: " + err, "error"));
  },

  injectFault(faultType) {
    App.showToast(`Injecting simulated fault [${faultType.toUpperCase()}] on ${this.activeDeviceId}...`, "warning");
    fetch('/api/hardware/inject_fault', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: `${this.activeDeviceId}:${faultType}`
    })
      .then(r => r.json())
      .then(() => {
        App.showToast(`Fault [${faultType.toUpperCase()}] active. Notice error state & recovery prompt.`, "error");
      })
      .catch(err => App.showToast("Fault injection failed: " + err, "error"));
  },

  recoverFault() {
    App.showToast(`Executing hardware recovery on ${this.activeDeviceId}...`, "info");
    fetch('/api/hardware/recover', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: this.activeDeviceId
    })
      .then(r => r.json())
      .then(res => {
        if (res.success) {
          App.showToast("Hardware link successfully re-synchronized and nominal!", "success");
          document.getElementById('hw-error-card').style.display = 'none';
        }
      })
      .catch(err => App.showToast("Recovery failed: " + err, "error"));
  },

  startTelemetryLoop() {
    if (this.telemetryInterval) clearInterval(this.telemetryInterval);
    this.telemetryInterval = setInterval(() => {
      this.pollTelemetry();
    }, 100); // 10 Hz polling
  },

  pollTelemetry() {
    fetch(`/api/hardware/telemetry?id=${this.activeDeviceId}`)
      .then(r => r.json())
      .then(pkt => {
        this.renderTelemetry(pkt);
      })
      .catch(() => {});
  },

  renderTelemetry(pkt) {
    const badge = document.getElementById('hw-conn-badge');
    const connectBtn = document.getElementById('hw-connect-btn');
    const errorCard = document.getElementById('hw-error-card');
    const errorMsg = document.getElementById('hw-error-msg');

    // Update connection badge & button
    if (badge) {
      if (pkt.state === 'STREAMING_TELEMETRY') {
        badge.className = 'pill pill-success';
        badge.textContent = 'STREAMING (10 Hz)';
        if (connectBtn) connectBtn.textContent = 'Disconnect';
        if (errorCard) errorCard.style.display = 'none';
      } else if (pkt.state === 'ERROR_FAULTED') {
        badge.className = 'pill pill-danger';
        badge.textContent = 'FAULTED: ' + pkt.active_fault;
        if (connectBtn) connectBtn.textContent = 'Disconnect';
        if (errorCard) {
          errorCard.style.display = 'block';
          if (errorMsg) errorMsg.textContent = pkt.status_message;
        }
      } else {
        badge.className = 'pill pill-warning';
        badge.textContent = 'DISCONNECTED';
        if (connectBtn) connectBtn.textContent = 'Connect Virtual Link';
        if (errorCard) errorCard.style.display = 'none';
      }
    }

    // Update device-specific telemetry fields
    if (this.activeDeviceId === 'mcu_stm32h7') {
      document.getElementById('hw-panel-mcu').style.display = 'block';
      document.getElementById('hw-panel-fpga').style.display = 'none';
      document.getElementById('hw-panel-jtag').style.display = 'none';

      document.getElementById('mcu-temp-val').textContent = `${pkt.mcu.core_temp_C.toFixed(1)} °C`;
      document.getElementById('mcu-vcc-val').textContent = `${pkt.mcu.vcc_volts.toFixed(3)} V`;
      document.getElementById('mcu-uptime-val').textContent = `${(pkt.mcu.uptime_ms / 1000).toFixed(1)} s`;
      document.getElementById('mcu-adc-val').textContent = `${pkt.mcu.adc_ch0} / 4095`;
      document.getElementById('mcu-gpio-val').textContent = `0x0${pkt.mcu.gpio_state.toString(16).toUpperCase()}`;
    } else if (this.activeDeviceId === 'fpga_artix7') {
      document.getElementById('hw-panel-mcu').style.display = 'none';
      document.getElementById('hw-panel-fpga').style.display = 'block';
      document.getElementById('hw-panel-jtag').style.display = 'none';

      document.getElementById('fpga-temp-val').textContent = `${pkt.fpga.die_temp_C.toFixed(1)} °C`;
      document.getElementById('fpga-vccint-val').textContent = `${pkt.fpga.vccint_volts.toFixed(3)} V`;
      document.getElementById('fpga-vccaux-val').textContent = `${pkt.fpga.vccaux_volts.toFixed(3)} V`;
      document.getElementById('fpga-lut-val').textContent = `${pkt.fpga.lut_util_pct.toFixed(1)} %`;
      document.getElementById('fpga-pll-val').textContent = pkt.fpga.pll_locked ? 'LOCKED (100.0 MHz)' : 'UNLOCKED';
    } else if (this.activeDeviceId === 'jtag_jlink') {
      document.getElementById('hw-panel-mcu').style.display = 'none';
      document.getElementById('hw-panel-fpga').style.display = 'none';
      document.getElementById('hw-panel-jtag').style.display = 'block';

      document.getElementById('jtag-vref-val').textContent = `${pkt.jtag.target_vref_volts.toFixed(3)} V`;
      document.getElementById('jtag-tck-val').textContent = `${pkt.jtag.tck_frequency_mhz.toFixed(1)} MHz`;
      document.getElementById('jtag-idcode-val').textContent = pkt.jtag.tap_detected_idcode;
      document.getElementById('jtag-device-name').textContent = pkt.jtag.tap_device_name;
      document.getElementById('jtag-packets-val').textContent = Number(pkt.jtag.packets_transferred).toLocaleString();
    }
  },

  updateDeviceCard() {
    this.pollTelemetry();
  }
};
