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
  let isChecking = false;
  let backendOnline = false;
  let lastStatusData = null;

  // Check URL query params (?mock=1, ?backend=https://... or ?apiBase=...)
  const urlParams = new URLSearchParams(window.location.search);
  const forceMock = urlParams.get('mock') === '1';
  const backendOverrideParam = urlParams.get('backend') || urlParams.get('apiBase');
  if (backendOverrideParam !== null) {
    try {
      window.localStorage.setItem('uew_thermal3d_base_url', backendOverrideParam.trim().replace(/\/+$/, ''));
    } catch (e) {}
  }

  function getApiBaseUrl() {
    // 1. Explicit query parameter override (?backend=... or ?apiBase=...)
    if (backendOverrideParam && backendOverrideParam.trim()) {
      return backendOverrideParam.trim().replace(/\/+$/, '');
    }

    // 2. localStorage cached base URL
    try {
      const stored = window.localStorage.getItem('uew_thermal3d_base_url');
      if (stored !== null && stored.trim()) return stored.trim().replace(/\/+$/, '');
    } catch (e) {}

    // 3. Global window.WORKBENCH_API_BASE_URL
    if (window.WORKBENCH_API_BASE_URL && window.WORKBENCH_API_BASE_URL.trim()) {
      return window.WORKBENCH_API_BASE_URL.trim().replace(/\/+$/, '');
    }

    // 4. Localhost or desktop development
    const hostname = window.location.hostname || '';
    if (/^(localhost|127\.0\.0\.1)$/i.test(hostname)) {
      if (window.location.port === '3000') {
        // Vite dev server running on port 3000 -> target backend on 8080
        return 'http://127.0.0.1:8080';
      }
      return '';
    }

    // 5. Cloud production environment (Vercel or GitHub Pages)
    if (hostname.endsWith('vercel.app') || hostname.endsWith('github.io')) {
      return (window.CONFIG && CONFIG.productionApiUrl) ? CONFIG.productionApiUrl : 'https://unified-engineering-workbench-backend.onrender.com';
    }

    // 6. Config fallback
    if (window.CONFIG && CONFIG.apiBase && CONFIG.apiBase.trim()) {
      return CONFIG.apiBase.trim().replace(/\/+$/, '');
    }
    if (window.CONFIG && CONFIG.thermal3dApiBase && CONFIG.thermal3dApiBase.trim()) {
      return CONFIG.thermal3dApiBase.trim().replace(/\/+$/, '');
    }

    return '';
  }

  function getExplicitBackendBase() {
    return getApiBaseUrl();
  }

  async function checkBackendAvailability() {
    if (forceMock) {
      setMockMode(true);
      const mockStatus = await MockBackend.getStatus();
      sessionToken = mockStatus.session_token;
      activePort = mockStatus.active_port;
      notifyStatus(mockStatus, true, false);
      return false;
    }

    isChecking = true;
    notifyStatus(lastStatusData, backendOnline, isChecking);

    const baseUrl = getApiBaseUrl();
    const timeoutMs = (window.CONFIG && CONFIG.mockTimeoutMs) || 8000;

    // 1. Primary authoritative check: /api/thermal3d/health (the real C++17 engine health endpoint)
    try {
      const healthUrl = (baseUrl ? baseUrl : '') + '/api/thermal3d/health';
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

      const res = await fetch(healthUrl, {
        method: 'GET',
        headers: { 'Accept': 'application/json' },
        signal: controller.signal
      });
      clearTimeout(timeoutId);

      if (res.ok) {
        const data = await res.json();
        if (data && data.status === 'ONLINE') {
          backendOnline = true;
          isChecking = false;
          lastStatusData = data;
          setMockMode(false);
          notifyStatus(data, true, false);

          // Background non-blocking fetch to populate session token / hardware concurrency if available
          fetch((baseUrl ? baseUrl : '') + '/api/status', {
            headers: { 'Accept': 'application/json' }
          }).then(r => r.ok ? r.json() : null).then(st => {
            if (st) {
              sessionToken = st.session_token || sessionToken;
              activePort = st.active_port || activePort;
            }
          }).catch(() => {});

          return true;
        }
      }
    } catch (e) {
      // Primary check timed out or failed; attempt fallback to /api/status
      try {
        const statusUrl = (baseUrl ? baseUrl : '') + '/api/status';
        const controller2 = new AbortController();
        const timeoutId2 = setTimeout(() => controller2.abort(), Math.min(timeoutMs, 4000));

        const res2 = await fetch(statusUrl, {
          method: 'GET',
          headers: { 'Accept': 'application/json' },
          signal: controller2.signal
        });
        clearTimeout(timeoutId2);

        if (res2.ok) {
          const data2 = await res2.json();
          sessionToken = data2.session_token || null;
          activePort = data2.active_port || 8080;
          backendOnline = true;
          isChecking = false;
          lastStatusData = data2;
          setMockMode(false);
          notifyStatus(data2, true, false);
          return true;
        }
      } catch (e2) {}
    }

    // Both checks failed: truthful offline state
    backendOnline = false;
    isChecking = false;
    setMockMode(false);
    notifyStatus(null, false, false);
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

  function notifyStatus(statusData, isHealthy, isCheckingState) {
    statusListeners.forEach(fn => fn(statusData, isHealthy, isMock, isCheckingState));
  }

  async function request(endpoint, options = {}) {
    if (isMock) {
      throw new Error('API routing to mock');
    }

    const headers = options.headers || {};
    if (sessionToken && !headers['X-Session-Token']) {
      headers['X-Session-Token'] = sessionToken;
    }

    const explicitBase = getExplicitBackendBase();
    const targetUrl = (explicitBase ? explicitBase : '') + endpoint;

    const res = await fetch(targetUrl, {
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
        notifyStatus(st, true, false);
        return;
      }
      await checkBackendAvailability();
    }, (window.CONFIG && CONFIG.statusPollIntervalMs) || 6000);
  }

  return {
    init: async function() {
      await checkBackendAvailability();
      startStatusPolling();
    },

    checkBackendAvailability: function() {
      return checkBackendAvailability();
    },

    setBackendOnline: function(data) {
      backendOnline = true;
      isChecking = false;
      lastStatusData = data || lastStatusData;
      setMockMode(false);
      notifyStatus(lastStatusData, true, false);
    },

    setBackendOffline: function() {
      backendOnline = false;
      isChecking = false;
      notifyStatus(null, false, false);
    },

    isBackendOnline: function() {
      return backendOnline;
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

    // =========================================================================
    // 3D Thermal FEA Real C++17 REST Endpoints (No mock fabrication allowed)
    // =========================================================================
    getThermal3dBaseUrl: function() {
      return getApiBaseUrl();
    },

    setThermal3dBaseUrl: function(url) {
      const cleaned = (url || '').trim().replace(/\/+$/, '');
      try {
        window.localStorage.setItem('uew_thermal3d_base_url', cleaned);
      } catch (e) {}
      return cleaned;
    },

    resetThermal3dBaseUrl: function() {
      try {
        window.localStorage.removeItem('uew_thermal3d_base_url');
      } catch (e) {}
      return this.getThermal3dBaseUrl();
    },

    enableDemoMode: async function() {
      setMockMode(true);
      const mockStatus = await MockBackend.getStatus();
      sessionToken = mockStatus.session_token;
      activePort = mockStatus.active_port;
      notifyStatus(mockStatus, true);
      return mockStatus;
    },

    disableDemoMode: function() {
      setMockMode(false);
      return checkBackendAvailability();
    },

    thermal3dRequest: async function(path, options = {}) {
      const base = this.getThermal3dBaseUrl();
      const url = (base ? base : '') + path;
      const timeoutMs = options.timeoutMs || 5000;
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

      if (options.signal) {
        if (options.signal.aborted) {
          clearTimeout(timeoutId);
          throw new DOMException('Aborted', 'AbortError');
        }
        options.signal.addEventListener('abort', () => controller.abort(), { once: true });
      }

      try {
        const res = await fetch(url, {
          method: options.method || 'GET',
          headers: options.headers || { 'Accept': 'application/json' },
          body: options.body,
          signal: controller.signal
        });
        clearTimeout(timeoutId);

        const rawText = await res.text();
        let parsed = null;
        try {
          parsed = rawText ? JSON.parse(rawText) : null;
        } catch (parseErr) {
          const invalidErr = new Error('Invalid JSON response from C++ backend');
          invalidErr.code = 'INVALID_RESPONSE';
          invalidErr.httpStatus = res.status;
          invalidErr.rawText = rawText;
          invalidErr.url = url;
          throw invalidErr;
        }

        if (!res.ok) {
          const errMsg = (parsed && (parsed.error_message || parsed.error || parsed.message)) || `HTTP ${res.status} ${res.statusText}`;
          const httpErr = new Error(errMsg);
          httpErr.code = res.status >= 400 ? 'HTTP_ERROR' : 'NETWORK_ERROR';
          httpErr.httpStatus = res.status;
          httpErr.data = parsed;
          httpErr.rawText = rawText;
          httpErr.url = url;
          throw httpErr;
        }

        return { data: parsed, rawText: rawText, httpStatus: res.status, url: url };
      } catch (err) {
        clearTimeout(timeoutId);
        if (err.name === 'AbortError' && (!options.signal || !options.signal.aborted)) {
          const timeoutErr = new Error(`Backend request timed out (${timeoutMs} ms)`);
          timeoutErr.code = 'OFFLINE';
          timeoutErr.url = url;
          throw timeoutErr;
        }
        if (!err.code && (err instanceof TypeError || /Failed to fetch|NetworkError|Load failed/i.test(err.message))) {
          err.code = 'OFFLINE';
          err.url = url;
        }
        throw err;
      }
    },

    thermal3dHealth: function(signal) {
      return this.thermal3dRequest('/api/thermal3d/health', { timeoutMs: 8000, signal });
    },

    thermal3dModels: function(signal) {
      return this.thermal3dRequest('/api/thermal3d/models', { timeoutMs: 8000, signal });
    },

    thermal3dConfig: function(signal) {
      return this.thermal3dRequest('/api/thermal3d/config', { timeoutMs: 8000, signal });
    },

    thermal3dSolve: function(payload, signal) {
      return this.thermal3dRequest('/api/thermal3d/solve', {
        method: 'POST',
        headers: {
          'Accept': 'application/json',
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(payload),
        timeoutMs: 60000,
        signal: signal
      });
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
