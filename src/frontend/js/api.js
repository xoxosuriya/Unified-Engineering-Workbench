/**
 * Unified Engineering Workbench - API Client & Mock Router
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.API = (function() {
  'use strict';

  let isMock = false;
  let sessionToken = null;
  let activePort = 8080;
  let statusListeners = [];
  let pollIntervalId = null;

  // Check URL query param ?mock=1
  const urlParams = new URLSearchParams(window.location.search);
  const forceMock = urlParams.get('mock') === '1';

  async function checkBackendAvailability() {
    if (forceMock) {
      setMockMode(true);
      return false;
    }

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), CONFIG.mockTimeoutMs || 800);

    try {
      const res = await fetch('/api/status', {
        method: 'GET',
        headers: { 'Accept': 'application/json' },
        signal: controller.signal
      });
      clearTimeout(timeout);
      if (res.ok) {
        const data = await res.json();
        sessionToken = data.session_token || null;
        activePort = data.active_port || 8080;
        setMockMode(false);
        notifyStatus(data, true);
        return true;
      }
    } catch (e) {
      // Failed or timed out
    }
    clearTimeout(timeout);
    setMockMode(true);
    const mockStatus = await MockBackend.getStatus();
    sessionToken = mockStatus.session_token;
    activePort = mockStatus.active_port;
    notifyStatus(mockStatus, true);
    return false;
  }

  function setMockMode(active) {
    isMock = active;
    if (active) {
      document.body.dataset.mock = '1';
    } else {
      delete document.body.dataset.mock;
    }
  }

  function notifyStatus(statusData, isHealthy) {
    statusListeners.forEach(fn => fn(statusData, isHealthy, isMock));
  }

  async function request(endpoint, options = {}) {
    if (isMock) {
      throw new Error('API routing to mock');
    }

    const headers = options.headers || {};
    if (sessionToken && !headers['X-Session-Token']) {
      headers['X-Session-Token'] = sessionToken;
    }

    const res = await fetch(endpoint, {
      ...options,
      headers
    });

    const isJson = res.headers.get('content-type')?.includes('application/json');
    const data = isJson ? await res.json() : await res.text();

    if (!res.ok) {
      const errorMsg = (typeof data === 'object') ? 
        (data.error_message || data.error || 'Server error occurred') :
        data;
      const err = new Error(errorMsg);
      err.status = res.status;
      err.data = data;
      throw err;
    }

    if (data && typeof data === 'object' && data.success === false) {
      const err = new Error(data.error_message || 'Operation failed');
      err.data = data;
      throw err;
    }

    return data;
  }

  function startStatusPolling() {
    if (pollIntervalId) clearInterval(pollIntervalId);
    pollIntervalId = setInterval(async () => {
      if (isMock) {
        const st = await MockBackend.getStatus();
        notifyStatus(st, true);
        return;
      }

      try {
        const res = await fetch('/api/status', {
          headers: { 'Accept': 'application/json' }
        });
        if (res.ok) {
          const st = await res.json();
          sessionToken = st.session_token || sessionToken;
          activePort = st.active_port || activePort;
          notifyStatus(st, true);
        } else {
          notifyStatus(null, false);
        }
      } catch (err) {
        notifyStatus(null, false);
      }
    }, CONFIG.statusPollIntervalMs || 5000);
  }

  return {
    init: async function() {
      await checkBackendAvailability();
      startStatusPolling();
    },

    isMock: function() {
      return isMock;
    },

    onStatusChange: function(fn) {
      statusListeners.push(fn);
    },

    getSessionToken: function() {
      return sessionToken;
    },

    getActivePort: function() {
      return activePort;
    },

    // Shutdown backend: POST /api/shutdown
    shutdown: async function() {
      if (isMock) {
        return MockBackend.shutdown();
      }
      return request('/api/shutdown', {
        method: 'POST',
        headers: {
          'X-Session-Token': sessionToken || ''
        }
      });
    },

    // STL Geometry Endpoints
    getStlSample: async function() {
      if (isMock) return MockBackend.getStlSample();
      return request('/api/samples/stl');
    },

    parseStl: async function(rawText) {
      if (isMock) return MockBackend.parseStl(rawText);
      return request('/api/stl/parse', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: rawText
      });
    },

    // ANSYS Endpoints
    getAnsysSample: async function() {
      if (isMock) return MockBackend.getAnsysSample();
      return request('/api/samples/ansys');
    },

    parseAnsys: async function(rawCsv) {
      if (isMock) return MockBackend.parseAnsys(rawCsv);
      return request('/api/ansys/parse', {
        method: 'POST',
        headers: { 'Content-Type': 'text/csv' },
        body: rawCsv
      });
    },

    // FEA Endpoints
    solveFea: async function(params) {
      if (isMock) return MockBackend.solveFea(params);
      const query = new URLSearchParams(params).toString();
      return request('/api/fea/solve?' + query);
    },

    convergenceFea: async function(params) {
      if (isMock) return MockBackend.convergenceFea(params);
      const query = new URLSearchParams(params).toString();
      return request('/api/fea/convergence?' + query);
    },

    // Performance Benchmarking Endpoints
    runPerformance: async function(cases, mesh, seed, abortSignal) {
      if (isMock) return MockBackend.runPerformance(cases, mesh, seed);
      const query = new URLSearchParams({
        cases: cases,
        mesh: mesh,
        seed: seed
      }).toString();
      return request('/api/performance/run?' + query, { signal: abortSignal });
    },

    getPerformanceProgress: async function() {
      if (isMock) return MockBackend.getPerformanceProgress();
      return request('/api/performance/progress');
    },

    cancelPerformance: async function() {
      if (isMock) return MockBackend.cancelPerformance();
      return request('/api/performance/cancel', { method: 'POST' });
    },

    // Hardware Endpoints
    hardwareDiscover: async function() {
      if (isMock) return MockBackend.hardwareDiscover();
      return request('/api/hardware/discover');
    },

    hardwareConnect: async function(deviceId) {
      if (isMock) return MockBackend.hardwareConnect(deviceId);
      return request('/api/hardware/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: deviceId
      });
    },

    hardwareDisconnect: async function(deviceId) {
      if (isMock) return MockBackend.hardwareDisconnect(deviceId);
      return request('/api/hardware/disconnect', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: deviceId
      });
    },

    hardwareInjectFault: async function(payload) {
      if (isMock) return MockBackend.hardwareInjectFault(payload);
      return request('/api/hardware/inject_fault', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: payload
      });
    },

    hardwareRecover: async function(deviceId) {
      if (isMock) return MockBackend.hardwareRecover(deviceId);
      return request('/api/hardware/recover', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: deviceId
      });
    },

    hardwareTelemetry: async function(deviceId) {
      if (isMock) return MockBackend.hardwareTelemetry(deviceId);
      return request('/api/hardware/telemetry?id=' + encodeURIComponent(deviceId));
    },

    hardwareArchitecture: async function() {
      if (isMock) return MockBackend.hardwareArchitecture();
      return request('/api/hardware/architecture');
    },

    // Backprop Endpoints
    backpropStudy: async function() {
      if (isMock) return MockBackend.backpropStudy();
      return request('/api/backprop/study');
    },

    // File Downloads (CSV Exports)
    triggerDownload: function(url, filename, fallbackContent) {
      if (isMock) {
        UI.showToast('Exports are disabled in DEMO DATA mode');
        return;
      }
      if (fallbackContent) {
        const blob = new Blob([fallbackContent], { type: 'text/csv;charset=utf-8;' });
        const link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = filename;
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
      } else {
        const link = document.createElement('a');
        link.href = url;
        link.download = filename;
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
      }
    }
  };
})();
