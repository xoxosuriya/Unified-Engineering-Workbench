/**
 * Unified Engineering Workbench - 3D Thermal FEA View (Module 4 / Task C)
 * Finite-volume 3D thermal analysis of Solid Cube & Finned Heat Sink geometries
 * Connected strictly to the real C++17 REST backend (/api/thermal3d/*)
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.Thermal3dView = (function() {
  'use strict';

  let containerEl = null;
  let statusLine = null;
  let resizeObserver = null;

  // Three.js scene state
  let renderer = null;
  let scene = null;
  let camera = null;
  let controls = null;
  let instancedCells = null;
  let ghostWireframeGroup = null;
  let axisTriadGroup = null;
  let cellEdgeSegments = null;
  let hoverOutlineMesh = null;
  let sliceHelperMesh = null;
  let clippingPlane = null;
  let thermalMaterialUniforms = null;
  let gridHelper = null;
  let raycaster = null;
  let mouse = null;
  let rAFId = null;
  let isSoftwareGl = false;

  // Camera smooth preset transition state
  let camAnimActive = false;
  let camTargetPos = null;
  let camTargetLookAt = null;
  let sceneCenter = null;
  let sceneRadius = 1.0;

  // Chart.js comparison state
  let compareChart = null;

  // Simulation & backend state (strictly authoritative)
  let screenMode = 'selection'; // 'selection' | 'workspace'
  let selectedGeometry = 'CUBE';
  let selectedResolution = 10;
  let selectedSolver = 'PROFILE_CHOLESKY';
  let showEdges = false;
  let showGrid = true;
  let cellScaleRatio = 0.95;
  let sliceX = 100; // 10..100 (%)
  let sliceY = 100; // 10..100 (%)
  let sliceZ = 100; // 10..100 (%)
  let voxelScale = 0.95; // 0.70..1.00 (from 70..100%, default 95%)
  let sliceAxis = 'off';
  let sliceFraction = 1.0;

  // Viewport notification & structural discretized mesh state
  let noticeDismissTimer = null;
  let structuralNoticeDismissed = false;
  let ghostInstancedCells = null;
  let ghostTransforms = null;

  let backendOnline = false;
  let backendChecking = false;
  let solveAbortController = null;
  let compareAbortController = null;
  let healthTimer = null;
  let isSolving = false;
  let isComparing = false;

  let contractHealth = null;
  let contractModels = null;
  let contractConfig = null;
  let discoveredResolutions = [10, 20, 30];
  let discoveredSolvers = ['PROFILE_CHOLESKY', 'PCG', 'SOR'];
  let lastRequestPayload = null;
  let lastRawResponse = null;
  let lastSolveResult = null;
  let lastSolvedKey = null; // `${geometry}|${resolution}|${solver}` for active temperature field
  let supportedOptionalParams = {}; // Advertised optional params from /api/thermal3d/config
  let currentParsedField = null; // Holds validated coordinates + temperatures ONLY when SUCCESS/CONVERGED
  let instanceTransforms = null; // Normalized cell transforms + [0,1] engineering coords for fast slice/scale updates
  let comparisonResults = {}; // solverId -> result summary
  let lastRaycastTime = 0;

  // Pre-allocated math helpers for zero GC pressure in Three.js
  const _mat4 = (typeof THREE !== 'undefined') ? new THREE.Matrix4() : null;
  const _pos = (typeof THREE !== 'undefined') ? new THREE.Vector3() : null;
  const _quat = (typeof THREE !== 'undefined') ? new THREE.Quaternion() : null;
  const _scale = (typeof THREE !== 'undefined') ? new THREE.Vector3() : null;
  const _color = (typeof THREE !== 'undefined') ? new THREE.Color() : null;
  const _vecA = (typeof THREE !== 'undefined') ? new THREE.Vector3() : null;

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function formatSci(val, digits = 3) {
    if (val === null || val === undefined || typeof val !== 'number' || !isFinite(val)) return '—';
    if (val === 0) return '0';
    const abs = Math.abs(val);
    if (abs < 1e-3 || abs >= 1e5) return val.toExponential(digits);
    return val.toFixed(Math.min(4, digits));
  }

  function formatMs(val) {
    if (val === null || val === undefined || typeof val !== 'number' || !isFinite(val)) return '—';
    return `${val.toFixed(2)} ms`;
  }

  function formatTemp(val) {
    if (val === null || val === undefined || typeof val !== 'number' || !isFinite(val)) return '—';
    return `${val.toFixed(2)} °C`;
  }

  // Deep-lookup helper for snake_case or nested fields without inventing anything
  function pickField(obj, paths) {
    if (!obj || typeof obj !== 'object') return undefined;
    for (let i = 0; i < paths.length; i++) {
      const parts = paths[i].split('.');
      let cur = obj;
      let ok = true;
      for (let j = 0; j < parts.length; j++) {
        if (cur && typeof cur === 'object' && parts[j] in cur) {
          cur = cur[parts[j]];
        } else {
          ok = false;
          break;
        }
      }
      if (ok && cur !== undefined && cur !== null) return cur;
    }
    return undefined;
  }

  // Scientific 6-stop engineering thermal contour colormap:
  // Deep Blue -> Cyan -> Green -> Yellow -> Orange -> Red
  const THERMAL_STOPS = [
    [0.00, 22 / 255, 56 / 255, 147 / 255],   // Deep Blue (#163893)
    [0.20, 28 / 255, 150 / 255, 197 / 255],  // Cyan (#1C96C5)
    [0.42, 43 / 255, 174 / 255, 102 / 255],  // Green (#2BAE66)
    [0.64, 232 / 255, 197 / 255, 49 / 255],  // Yellow (#E8C531)
    [0.82, 235 / 255, 114 / 255, 36 / 255],  // Orange (#EB7224)
    [1.00, 201 / 255, 42 / 255, 42 / 255]    // Red (#C92A2A)
  ];

  function thermalNormToRgb(normT) {
    const t = Math.max(0, Math.min(1, normT));
    for (let i = 0; i < THERMAL_STOPS.length - 1; i++) {
      const a = THERMAL_STOPS[i];
      const b = THERMAL_STOPS[i + 1];
      if (t <= b[0]) {
        const p = (t - a[0]) / Math.max(1e-6, b[0] - a[0]);
        return [
          a[1] + (b[1] - a[1]) * p,
          a[2] + (b[2] - a[2]) * p,
          a[3] + (b[3] - a[3]) * p
        ];
      }
    }
    const last = THERMAL_STOPS[THERMAL_STOPS.length - 1];
    return [last[1], last[2], last[3]];
  }

  function renderCubeCardSvg() {
    return `
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 210">
        <defs>
          <linearGradient id="cubeTopGrad" x1="0%" y1="100%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#2BAE66" />
            <stop offset="45%" stop-color="#1C96C5" />
            <stop offset="100%" stop-color="#163893" />
          </linearGradient>
          <linearGradient id="cubeLeftGrad" x1="0%" y1="100%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#C92A2A" />
            <stop offset="32%" stop-color="#EB7224" />
            <stop offset="68%" stop-color="#E8C531" />
            <stop offset="100%" stop-color="#2BAE66" />
          </linearGradient>
          <linearGradient id="cubeRightGrad" x1="0%" y1="100%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#E8C531" />
            <stop offset="42%" stop-color="#2BAE66" />
            <stop offset="76%" stop-color="#1C96C5" />
            <stop offset="100%" stop-color="#163893" />
          </linearGradient>
        </defs>
        <g transform="translate(160, 108)">
          <!-- Subtle Isometric Axis Triad -->
          <g transform="translate(-122, 68)">
            <line x1="0" y1="0" x2="22" y2="-11" stroke="#141414" stroke-width="1.3" />
            <line x1="0" y1="0" x2="-22" y2="-11" stroke="#141414" stroke-width="1.3" />
            <line x1="0" y1="0" x2="0" y2="-24" stroke="#1F3FB0" stroke-width="1.5" />
            <text x="26" y="-10" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="8" fill="#141414">X</text>
            <text x="-31" y="-10" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="8" fill="#141414">Y</text>
            <text x="-3" y="-29" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="8" fill="#1F3FB0">Z</text>
          </g>

          <!-- Top Face -->
          <polygon points="0,-78 68,-44 0,-10 -68,-44" fill="url(#cubeTopGrad)" stroke="#141414" stroke-width="1.8" stroke-linejoin="round" />
          <!-- Left Front Face -->
          <polygon points="-68,-44 0,-10 0,66 -68,32" fill="url(#cubeLeftGrad)" stroke="#141414" stroke-width="1.8" stroke-linejoin="round" />
          <!-- Right Side Face -->
          <polygon points="0,-10 68,-44 68,32 0,66" fill="url(#cubeRightGrad)" stroke="#141414" stroke-width="1.8" stroke-linejoin="round" />

          <!-- Subtle Finite-Volume Cell Grid Boundaries -->
          <g stroke="#141414" stroke-width="0.75" opacity="0.45">
            <!-- Top Grid (4x4 divisions) -->
            <line x1="-51" y1="-35.5" x2="17" y2="-69.5" />
            <line x1="-34" y1="-27" x2="34" y2="-61" />
            <line x1="-17" y1="-18.5" x2="51" y2="-52.5" />
            <line x1="-51" y1="-52.5" x2="17" y2="-18.5" />
            <line x1="-34" y1="-61" x2="34" y2="-27" />
            <line x1="-17" y1="-69.5" x2="51" y2="-35.5" />
            <!-- Left Face Vertical + Horizontal Divisions -->
            <line x1="-51" y1="-35.5" x2="-51" y2="40.5" />
            <line x1="-34" y1="-27" x2="-34" y2="49" />
            <line x1="-17" y1="-18.5" x2="-17" y2="57.5" />
            <line x1="-68" y1="-25" x2="0" y2="9" />
            <line x1="-68" y1="-6" x2="0" y2="28" />
            <line x1="-68" y1="13" x2="0" y2="47" />
            <!-- Right Face Vertical + Horizontal Divisions -->
            <line x1="17" y1="-18.5" x2="17" y2="57.5" />
            <line x1="34" y1="-27" x2="34" y2="49" />
            <line x1="51" y1="-35.5" x2="51" y2="40.5" />
            <line x1="0" y1="9" x2="68" y2="-25" />
            <line x1="0" y1="28" x2="68" y2="-6" />
            <line x1="0" y1="47" x2="68" y2="13" />
          </g>

          <text x="0" y="-86" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="8" text-anchor="middle" fill="#1F3FB0">3D SOLID CUBE &middot; FVM THERMAL CONTOUR</text>
        </g>
      </svg>
    `;
  }

  function renderFinnedSinkCardSvg() {
    // True-proportion isometric preview of 100x100x20mm baseplate + 4 vertical fins (80mm H, 10mm T at 0-10, 30-40, 60-70, 90-100)
    // Isometric projection helper: x in [0..100], y in [0..100], z in [0..100]
    function iso(x, y, z) {
      const cx = (x - 50) * 0.72 - (y - 50) * 0.64;
      const cy = 54 + (x - 50) * 0.34 + (y - 50) * 0.32 - z * 0.92;
      return `${cx.toFixed(1)},${cy.toFixed(1)}`;
    }
    function poly(pts) { return pts.map(([x, y, z]) => iso(x, y, z)).join(' '); }

    const finsX = [[0, 10], [30, 40], [60, 70], [90, 100]];
    const finsMarkup = finsX.slice().reverse().map(([x0, x1]) => `
      <g>
        <!-- Outer Left Edge (Facing Front-Left) -->
        <polygon points="${poly([[x0, 100, 20], [x1, 100, 20], [x1, 100, 100], [x0, 100, 100]])}" fill="url(#finVertGrad)" stroke="#141414" stroke-width="1.3" stroke-linejoin="round" />
        <!-- Inner Broad Side Wall (x = x0, Facing -X toward viewer) -->
        <polygon points="${poly([[x0, 0, 20], [x0, 100, 20], [x0, 100, 100], [x0, 0, 100]])}" fill="url(#finSideGrad)" stroke="#141414" stroke-width="1.2" stroke-linejoin="round" />
        <!-- Outer Broad Side Wall (x = x1, Facing +X) -->
        <polygon points="${poly([[x1, 0, 20], [x1, 100, 20], [x1, 100, 100], [x1, 0, 100]])}" fill="url(#finVertGrad)" stroke="#141414" stroke-width="1.3" stroke-linejoin="round" />
        <!-- Top Fin Tip Face (z = 100mm, Cooled) -->
        <polygon points="${poly([[x0, 0, 100], [x1, 0, 100], [x1, 100, 100], [x0, 100, 100]])}" fill="#163893" stroke="#141414" stroke-width="1.3" stroke-linejoin="round" />
      </g>
    `).join('');

    return `
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 210">
        <defs>
          <linearGradient id="baseHotGrad" x1="0%" y1="0%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#C92A2A" />
            <stop offset="50%" stop-color="#EB7224" />
            <stop offset="100%" stop-color="#E8C531" />
          </linearGradient>
          <linearGradient id="baseTopGrad" x1="0%" y1="100%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#EB7224" />
            <stop offset="50%" stop-color="#E8C531" />
            <stop offset="100%" stop-color="#2BAE66" />
          </linearGradient>
          <linearGradient id="finVertGrad" x1="0%" y1="100%" x2="0%" y2="0%">
            <stop offset="0%" stop-color="#EB7224" />
            <stop offset="28%" stop-color="#E8C531" />
            <stop offset="56%" stop-color="#2BAE66" />
            <stop offset="80%" stop-color="#1C96C5" />
            <stop offset="100%" stop-color="#163893" />
          </linearGradient>
          <linearGradient id="finSideGrad" x1="0%" y1="100%" x2="0%" y2="0%">
            <stop offset="0%" stop-color="#C92A2A" />
            <stop offset="24%" stop-color="#EB7224" />
            <stop offset="48%" stop-color="#E8C531" />
            <stop offset="72%" stop-color="#2BAE66" />
            <stop offset="90%" stop-color="#1C96C5" />
            <stop offset="100%" stop-color="#163893" />
          </linearGradient>
        </defs>
        <g transform="translate(160, 108)">
          <!-- Isometric Axes Indicator -->
          <g transform="translate(-124, 68)">
            <line x1="0" y1="0" x2="22" y2="-11" stroke="#141414" stroke-width="1.3" />
            <line x1="0" y1="0" x2="-22" y2="-11" stroke="#141414" stroke-width="1.3" />
            <line x1="0" y1="0" x2="0" y2="-24" stroke="#1F3FB0" stroke-width="1.5" />
            <text x="26" y="-10" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="8" fill="#141414">X</text>
            <text x="-31" y="-10" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="8" fill="#141414">Y</text>
            <text x="-3" y="-29" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="8" fill="#1F3FB0">Z</text>
          </g>

          <!-- Baseplate 100x100x20mm (z = 0..20) -->
          <polygon points="${poly([[0, 0, 20], [100, 0, 20], [100, 100, 20], [0, 100, 20]])}" fill="url(#baseTopGrad)" stroke="#141414" stroke-width="1.5" stroke-linejoin="round" />
          <polygon points="${poly([[0, 100, 0], [100, 100, 0], [100, 100, 20], [0, 100, 20]])}" fill="url(#baseHotGrad)" stroke="#141414" stroke-width="1.5" stroke-linejoin="round" />
          <polygon points="${poly([[100, 0, 0], [100, 100, 0], [100, 100, 20], [100, 0, 20]])}" fill="#EB7224" stroke="#141414" stroke-width="1.5" stroke-linejoin="round" />

          <!-- Four Vertical Fins (80mm H, 10mm T at 0-10, 30-40, 60-70, 90-100mm) -->
          ${finsMarkup}

          <text x="0" y="-88" font-family="'Inter Tight', sans-serif" font-weight="700" font-size="7.8" text-anchor="middle" fill="#1F3FB0">100&times;100&times;20 MM BASE &middot; 4 FINS (80 MM H &times; 10 MM T)</text>
        </g>
      </svg>
    `;
  }

  function renderSelectionHTML() {
    const resList = discoveredResolutions && discoveredResolutions.length ? discoveredResolutions.map(n => `N=${n}`).join(', ') : 'N=10, N=20, N=30';
    const solvList = discoveredSolvers && discoveredSolvers.length ? discoveredSolvers.join(' · ') : 'PROFILE_CHOLESKY · PCG · SOR';

    return `
      <div class="t3d-select-shell">
        <div class="t3d-select-inner">
          <div class="t3d-select-header">
            <div class="detail-mask">
              <span class="lbl detail-subtitle">TASK C / 3D THERMAL FEA MODEL SELECTION</span>
            </div>
            <div class="detail-mask">
              <h1 class="t3d-select-title">Select 3D Simulation Model</h1>
            </div>
            <div class="detail-mask">
              <p class="t3d-select-sub">
                Choose a three-dimensional finite-volume thermal geometry to launch its dedicated simulation laboratory powered by the C++17 engine.
              </p>
            </div>
          </div>

          <div class="t3d-select-status-bar ${backendOnline ? '' : 'is-offline'}" id="t3d-select-status-bar">
            <div style="display: flex; flex-direction: column; gap: 3px;">
              <span class="lbl" id="t3d-select-status-title" style="color: ${backendOnline ? '#2E7D32' : 'var(--vermilion)'};">
                ${backendChecking ? 'CHECKING C++17 BACKEND...' : (backendOnline ? 'C++17 BACKEND ONLINE' : 'BACKEND OFFLINE — STRUCTURAL CAD REFERENCE MODE')}
              </span>
              <span id="t3d-select-status-body" style="font-family: var(--font-serif); font-size: 13.5px; color: var(--ink);">
                ${backendOnline
                  ? `Connected to ${escapeHtml(API.getThermal3dBaseUrl() || 'local origin')}. Live temperature solves and solver comparisons are enabled.`
                  : 'Both 3D model workspaces remain open for structural CAD inspection. Simulation execution is disabled until the local C++17 engine connects.'}
              </span>
            </div>
            <button class="box" id="btn-t3d-select-retry" type="button" style="height: 26px;">RETRY CONNECTION</button>
          </div>

          <div class="t3d-select-grid">
            <!-- CARD 1: SOLID CUBE (Model B) -->
            <article class="t3d-model-card" id="card-model-cube">
              <div class="t3d-model-card-top">
                <span class="t3d-model-badge">MODEL B — SOLID CUBE</span>
                <span class="t3d-model-method">THREE-DIMENSIONAL FINITE VOLUME METHOD</span>
              </div>
              <div class="t3d-model-art" aria-hidden="true">
                ${renderCubeCardSvg()}
              </div>
              <div class="t3d-model-card-body">
                <h2 class="t3d-model-title">Solid Cube</h2>
                <p class="t3d-model-desc">3D solid cube thermal analysis.</p>
                <div class="t3d-model-meta">
                  MESH RESOLUTIONS: ${escapeHtml(resList)}<br />
                  SOLVERS: ${escapeHtml(solvList)}
                </div>
                <div class="t3d-model-cta">
                  <button class="box cobalt" id="btn-select-cube" type="button" style="background: var(--cobalt); color: #FFFFFF;">
                    OPEN CUBE SIMULATION
                  </button>
                </div>
              </div>
            </article>

            <!-- CARD 2: FINNED HEAT SINK (Model C) -->
            <article class="t3d-model-card" id="card-model-finned-sink">
              <div class="t3d-model-card-top">
                <span class="t3d-model-badge">MODEL C — FINNED HEAT SINK</span>
                <span class="t3d-model-method">THREE-DIMENSIONAL FINITE VOLUME METHOD</span>
              </div>
              <div class="t3d-model-art" aria-hidden="true">
                ${renderFinnedSinkCardSvg()}
              </div>
              <div class="t3d-model-card-body">
                <h2 class="t3d-model-title">Finned Heat Sink</h2>
                <p class="t3d-model-desc">Thermal analysis of a baseplate with four vertical fins.</p>
                <div class="t3d-model-meta">
                  MESH RESOLUTIONS: ${escapeHtml(resList)}<br />
                  SOLVERS: ${escapeHtml(solvList)}
                </div>
                <div class="t3d-model-cta">
                  <button class="box cobalt" id="btn-select-sink" type="button" style="background: var(--cobalt); color: #FFFFFF;">
                    OPEN FINNED HEAT SINK SIMULATION
                  </button>
                </div>
              </div>
            </article>
          </div>
        </div>
      </div>
    `;
  }

  function renderHTML() {
    const isCube = (selectedGeometry === 'CUBE');
    const modelCode = isCube ? 'MODEL B' : 'MODEL C';
    const modelTitle = isCube ? 'Solid Cube' : 'Finned Heat Sink';
    const modelBlurb = isCube
      ? 'Three-dimensional cell-centred Finite Volume thermal analysis of a solid cube with Dirichlet and insulated boundary conditions.'
      : 'Three-dimensional cell-centred Finite Volume thermal analysis of a heated baseplate with four vertical convection fins.';

    return `
      <!-- Left Column: Editorial Dossier, Simulation Controls, Results & Comparison -->
      <div class="detail-left-col">
        <div class="detail-text-stack">
          <div class="detail-mask">
            <span class="lbl detail-subtitle">TASK C / 3D SOLVER &middot; ${modelCode}</span>
          </div>
          <div class="detail-mask">
            <h1 class="detail-title">3D Thermal FEA &mdash; ${modelTitle}</h1>
          </div>
          <div class="detail-mask">
            <span class="lbl detail-meta">${escapeHtml(selectedGeometry)} / 3D FINITE-VOLUME THERMAL / C++17</span>
          </div>
          <div class="detail-mask">
            <p class="detail-blurb">${modelBlurb}</p>
          </div>
          <div class="detail-mask">
            <p class="detail-credit">
              Engineering Workbench assessment module. Candidate ${CONFIG.candidateId}.
            </p>
          </div>
        </div>

        <!-- Secondary Tab Switcher inside Left Dossier -->
        <div class="t3d-tabs" role="tablist" aria-label="3D Thermal FEA Workspace Panels">
          <button class="t3d-tab-btn is-active" data-tab="simulate" role="tab" aria-selected="true">SIMULATE</button>
          <button class="t3d-tab-btn" data-tab="results" role="tab" aria-selected="false">RESULTS</button>
          <button class="t3d-tab-btn" data-tab="compare" role="tab" aria-selected="false">COMPARE</button>
          <button class="t3d-tab-btn" data-tab="contract" role="tab" aria-selected="false">CONTRACT</button>
        </div>

        <!-- Error Banner Mask -->
        <div class="error-banner-mask">
          <div class="error-banner" id="t3d-error-banner">
            <span class="error-banner-title">SOLVER DIAGNOSTIC</span>
            <span class="error-banner-text"></span>
            <span class="error-banner-hint"></span>
          </div>
        </div>

        <!-- TAB 1: SIMULATE -->
        <div class="t3d-tab-panel is-active" id="t3d-panel-simulate">
          <div class="detail-controls-panel" style="gap: 12px;">
            <div class="controls-title">SIMULATION CONFIGURATION</div>

            <!-- Dedicated Active Geometry Model (switch via CHANGE MODEL) -->
            <div class="control-group">
              <label class="control-label">Active Geometry Model</label>
              <div class="t3d-active-model-bar">
                <span style="font-family: var(--font-mono); font-size: 11px; font-weight: 700; color: var(--ink);">
                  ${modelCode} &middot; ${escapeHtml(selectedGeometry)}
                </span>
                <button class="box" id="btn-t3d-panel-change-model" type="button" style="height: 22px; font-size: 9px;">
                  CHANGE MODEL
                </button>
              </div>
            </div>

            <!-- Resolution selector -->
            <div class="control-group">
              <label class="control-label">Mesh Resolution (Cells per Axis)</label>
              <div class="t3d-seg-group" id="t3d-resolution-group">
                <button class="t3d-seg-btn is-active" data-res="10">N=10</button>
                <button class="t3d-seg-btn" data-res="20">N=20</button>
                <button class="t3d-seg-btn" data-res="30">N=30</button>
              </div>
            </div>

            <!-- Linear Solver selector -->
            <div class="control-group">
              <label class="control-label">Linear System Solver</label>
              <div class="t3d-seg-group" id="t3d-solver-group">
                <button class="t3d-seg-btn is-active" data-solver="PROFILE_CHOLESKY">PROFILE_CHOLESKY</button>
                <button class="t3d-seg-btn" data-solver="PCG">PCG</button>
                <button class="t3d-seg-btn" data-solver="SOR">SOR</button>
              </div>
            </div>

            <!-- Optional Backend-Supported Solver Parameters (only populated if /config advertises them) -->
            <div id="t3d-optional-settings" class="t3d-options-box" style="display: none;"></div>

            <!-- Primary Action Bar -->
            <div style="display: flex; gap: 8px; margin-top: 2px;">
              <button class="box cobalt" id="btn-t3d-run" style="flex: 1.5; height: 32px; background: var(--cobalt); color: #FFFFFF;" disabled>
                RUN SIMULATION
              </button>
              <button class="box" id="btn-t3d-abort" style="flex: 0.7; height: 32px; display: none; border-color: var(--vermilion); color: var(--vermilion);">
                ABORT
              </button>
              <button class="box" id="btn-t3d-reconnect" style="flex: 0.9; height: 32px;">
                RETRY CONNECTION
              </button>
            </div>

            <div id="t3d-status-container"></div>
          </div>

          <!-- Compact Live Summary Ledger -->
          <div class="t3d-ledger" id="t3d-summary-ledger">
            <div class="t3d-ledger-header">
              <span>LATEST RUN SUMMARY</span>
              <span id="t3d-summary-badge" style="font-family: var(--font-mono); font-size: 9.5px; color: var(--muted);">NO RESULT</span>
            </div>
            <div class="t3d-kv-grid" id="t3d-summary-grid">
              <div class="t3d-kv-cell">
                <span class="t3d-kv-label">SOLVER STATUS</span>
                <span class="t3d-kv-val">—</span>
              </div>
              <div class="t3d-kv-cell">
                <span class="t3d-kv-label">ACTIVE CELLS</span>
                <span class="t3d-kv-val">—</span>
              </div>
              <div class="t3d-kv-cell">
                <span class="t3d-kv-label">ITERATIONS</span>
                <span class="t3d-kv-val">—</span>
              </div>
              <div class="t3d-kv-cell">
                <span class="t3d-kv-label">SOLVE TIME</span>
                <span class="t3d-kv-val">—</span>
              </div>
              <div class="t3d-kv-cell">
                <span class="t3d-kv-label">FINAL RESIDUAL</span>
                <span class="t3d-kv-val">—</span>
              </div>
              <div class="t3d-kv-cell">
                <span class="t3d-kv-label">VERIFICATION</span>
                <span class="t3d-kv-val">—</span>
              </div>
            </div>
          </div>
        </div>

        <!-- TAB 2: RESULTS -->
        <div class="t3d-tab-panel" id="t3d-panel-results">
          <div class="t3d-ledger">
            <div class="t3d-ledger-header">
              <span>AUTHORITATIVE SOLVER METRICS</span>
              <span id="t3d-results-status-tag" style="font-family: var(--font-mono); font-size: 9.5px; color: var(--muted);">AWAITING SOLVE</span>
            </div>
            <div id="t3d-full-metrics-body" style="padding: 12px; font-family: var(--font-serif); font-size: 13px; color: var(--muted);">
              Run a simulation against the live C++17 engine to populate mesh, matrix, timing, memory, and verification metrics.
            </div>
          </div>
        </div>

        <!-- TAB 3: COMPARE -->
        <div class="t3d-tab-panel" id="t3d-panel-compare">
          <div class="detail-controls-panel" style="gap: 10px;">
            <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--rule); padding-bottom: 8px;">
              <span class="controls-title" style="border: none; padding: 0;">3-SOLVER BENCHMARK COMPARISON</span>
              <span id="t3d-compare-meta" style="font-family: var(--font-mono); font-size: 9.5px; color: var(--cobalt);">FINNED_SINK / N=10</span>
            </div>
            <p style="font-family: var(--font-serif); font-size: 13px; line-height: 1.4; color: var(--ink);">
              Executes Profile Cholesky, PCG, and SOR sequentially against the live C++ backend using the currently selected geometry and resolution.
            </p>
            <div style="display: flex; gap: 8px;">
              <button class="box cobalt" id="btn-t3d-compare-run" style="flex: 1; height: 30px;" disabled>
                COMPARE ALL 3 SOLVERS
              </button>
              <button class="box" id="btn-t3d-compare-cancel" style="display: none; height: 30px; border-color: var(--vermilion); color: var(--vermilion);">
                STOP
              </button>
            </div>
          </div>

          <div class="t3d-ledger">
            <div class="t3d-ledger-header">
              <span>SOLVE TIME COMPARISON (ms)</span>
            </div>
            <div style="height: 175px; padding: 10px; position: relative;">
              <canvas id="t3d-compare-chart"></canvas>
            </div>
          </div>

          <div class="t3d-ledger">
            <div class="t3d-ledger-header">
              <span>NUMERICAL BENCHMARK LEDGER</span>
            </div>
            <div style="overflow-x: auto;">
              <table class="t3d-table">
                <thead>
                  <tr>
                    <th>SOLVER</th>
                    <th>STATUS</th>
                    <th style="text-align: right;">ITERS</th>
                    <th style="text-align: right;">SOLVE (ms)</th>
                    <th style="text-align: right;">RESIDUAL</th>
                    <th style="text-align: right;">MEMORY</th>
                  </tr>
                </thead>
                <tbody id="t3d-compare-tbody">
                  <tr><td colspan="6" style="color: var(--muted); text-align: center; padding: 14px;">Comparison unavailable — run live benchmark when backend is online.</td></tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>

        <!-- TAB 4: CONTRACT (Secondary Diagnostic Inspector) -->
        <div class="t3d-tab-panel" id="t3d-panel-contract">
          <div class="detail-controls-panel" style="gap: 10px;">
            <div class="controls-title">C++17 REST BACKEND CONNECTION & CONTRACT</div>
            <div class="control-group">
              <label class="control-label">Backend Base URL (Local Proxy or http://127.0.0.1:8080)</label>
              <div style="display: flex; gap: 6px;">
                <input type="text" id="inp-t3d-base-url" class="control-input" style="flex: 1;" placeholder="Same origin (/api/thermal3d/*)" value="${escapeHtml(API.getThermal3dBaseUrl())}" />
                <button class="box cobalt" id="btn-t3d-save-url" style="height: 28px;">APPLY & PROBE</button>
                <button class="box" id="btn-t3d-reset-url" style="height: 28px;">DEFAULT</button>
              </div>
            </div>
            <div style="display: flex; gap: 6px; flex-wrap: wrap;">
              <button class="box" id="btn-t3d-probe-all" style="height: 24px; font-size: 9px;">REFRESH HEALTH / MODELS / CONFIG</button>
            </div>
          </div>

          <div class="t3d-ledger">
            <div class="t3d-ledger-header">
              <span>DISCOVERY ENDPOINTS (/health, /models, /config)</span>
            </div>
            <pre id="t3d-contract-discovery" style="padding: 10px; font-family: var(--font-mono); font-size: 9.5px; line-height: 1.4; max-height: 180px; overflow: auto; color: var(--ink); white-space: pre-wrap;">Probing endpoints...</pre>
          </div>

          <div class="t3d-ledger">
            <div class="t3d-ledger-header">
              <span>LAST POST /api/thermal3d/solve TRACE</span>
            </div>
            <pre id="t3d-contract-solve" style="padding: 10px; font-family: var(--font-mono); font-size: 9.5px; line-height: 1.4; max-height: 210px; overflow: auto; color: var(--ink); white-space: pre-wrap;">No solve request dispatched yet.</pre>
          </div>
        </div>
      </div>

      <!-- Right Column (~60-62%): Dedicated 3D Thermal Viewport -->
      <div class="detail-right-col" id="t3d-viewport-wrap">
        <div class="viz-container" id="t3d-three-container"></div>

        <!-- Top-Left Viewport Identity & Returned Temperature Stats -->
        <div class="t3d-hud-top-left">
          <div class="t3d-hud-card">
            <div id="t3d-vp-model-title" style="font-family: var(--font-sans); font-weight: 700; font-size: 10.5px; letter-spacing: 0.08em; color: var(--ink);">
              FINNED_SINK &middot; N=10 &middot; PROFILE_CHOLESKY
            </div>
            <div id="t3d-vp-stats-line" style="font-family: var(--font-mono); font-variant-numeric: tabular-nums; font-size: 10px; color: var(--muted); margin-top: 3px;">
              STRUCTURAL REFERENCE &mdash; NO LIVE TEMPERATURE DATA
            </div>
          </div>
        </div>

        <!-- Top-Right Camera Presets & Display Toggles -->
        <div class="t3d-hud-top-right">
          <button class="t3d-hud-btn" data-cam="iso">ISO</button>
          <button class="t3d-hud-btn" data-cam="front">FRONT</button>
          <button class="t3d-hud-btn" data-cam="top">TOP</button>
          <button class="t3d-hud-btn" data-cam="reset">RESET</button>
          <button class="t3d-hud-btn" id="btn-t3d-edges">EDGES</button>
          <button class="t3d-hud-btn is-active" id="btn-t3d-grid">GRID</button>
        </div>

        <!-- Single Continuous Vertical Thermal Legend (Blue -> Cyan -> Green -> Yellow -> Orange -> Red) -->
        <div class="t3d-legend is-unavailable" id="t3d-legend">
          <div class="t3d-legend-header">
            <span>TEMP</span>
            <span class="t3d-legend-unit">&deg;C</span>
          </div>
          <div class="t3d-legend-body">
            <div class="t3d-legend-bar" id="t3d-legend-bar"></div>
            <div class="t3d-legend-ticks">
              <div class="t3d-legend-tick">
                <span class="t3d-legend-tick-cap">MAX</span>
                <span id="t3d-leg-max" style="font-weight: 700; color: #C92A2A;">—</span>
              </div>
              <div class="t3d-legend-tick">
                <span class="t3d-legend-tick-cap">75%</span>
                <span id="t3d-leg-75">—</span>
              </div>
              <div class="t3d-legend-tick">
                <span class="t3d-legend-tick-cap" id="t3d-leg-mid-cap">AVG</span>
                <span id="t3d-leg-mid">—</span>
              </div>
              <div class="t3d-legend-tick">
                <span class="t3d-legend-tick-cap">25%</span>
                <span id="t3d-leg-25">—</span>
              </div>
              <div class="t3d-legend-tick">
                <span class="t3d-legend-tick-cap">MIN</span>
                <span id="t3d-leg-min" style="font-weight: 700; color: #163893;">—</span>
              </div>
            </div>
          </div>
          <div class="t3d-legend-unavail-note" id="t3d-legend-unavail-note">NO LIVE FIELD</div>
        </div>

        <!-- Compact Dismissible Viewport Status Notice (Moved completely away from bottom dock) -->
        <div class="t3d-stage-notice" id="t3d-stage-notice">
          <div class="t3d-stage-notice-header">
            <span class="t3d-stage-notice-title" id="t3d-notice-title">STRUCTURAL REFERENCE</span>
            <div style="display: flex; align-items: center; gap: 6px;">
              <button class="box cobalt" id="btn-t3d-notice-action" style="display: none; height: 18px; font-size: 8px; padding: 0 5px;">RETRY</button>
              <button class="t3d-notice-close-btn" id="btn-t3d-notice-close" type="button" title="Dismiss notice" aria-label="Dismiss">&times;</button>
            </div>
          </div>
          <div class="t3d-stage-notice-body" id="t3d-notice-body">
            Showing ${escapeHtml(selectedGeometry)} structural reference. Click RUN SIMULATION to compute real temperatures.
          </div>
        </div>

        <!-- Bottom Dock: Simultaneous 3-Axis Proportion Sliders & Voxel Gap Controls -->
        <div class="t3d-viewport-bottom">
          <div class="t3d-bottom-dock" id="t3d-bottom-dock">
            <div class="t3d-dock-row t3d-dock-row-axes">
              <span class="t3d-dock-section-title">PROPORTIONS</span>
              <div class="t3d-axis-slider" title="X-Axis Cutoff (10–100%)">
                <span class="t3d-axis-tag is-x">X-CUT</span>
                <input type="range" class="t3d-range t3d-slider-input" id="slider-slice-x" min="10" max="100" step="1" value="${sliceX}" aria-label="X-Axis Cutoff" />
                <span class="t3d-dock-val" id="val-slice-x">${sliceX}%</span>
              </div>
              <div class="t3d-axis-slider" title="Y-Axis Cutoff (10–100%)">
                <span class="t3d-axis-tag is-y">Y-CUT</span>
                <input type="range" class="t3d-range t3d-slider-input" id="slider-slice-y" min="10" max="100" step="1" value="${sliceY}" aria-label="Y-Axis Cutoff" />
                <span class="t3d-dock-val" id="val-slice-y">${sliceY}%</span>
              </div>
              <div class="t3d-axis-slider" title="Z-Axis Cutoff (10–100%)">
                <span class="t3d-axis-tag is-z">Z-CUT</span>
                <input type="range" class="t3d-range t3d-slider-input" id="slider-slice-z" min="10" max="100" step="1" value="${sliceZ}" aria-label="Z-Axis Cutoff" />
                <span class="t3d-dock-val" id="val-slice-z">${sliceZ}%</span>
              </div>
            </div>

            <div class="t3d-dock-row t3d-dock-row-tools">
              <div class="t3d-dock-tool-group">
                <span class="t3d-dock-lbl">VOXEL GAP</span>
                <input type="range" class="t3d-range t3d-slider-input" id="slider-voxel-scale" min="70" max="100" step="1" value="${Math.round(voxelScale * 100)}" aria-label="Voxel Gap / Cell Spacing" />
                <span class="t3d-dock-val" id="val-voxel-scale">${Math.round(voxelScale * 100)}%</span>
              </div>
              <button class="t3d-dock-btn" id="btn-t3d-reset-slice" type="button" title="Reset all cuts to 100% and voxel gap to 95%">RESET</button>
            </div>
          </div>

          <div class="t3d-cell-readout" id="t3d-cell-readout">
            ORBIT: DRAG &bull; ZOOM: SCROLL &bull; NO TEMPERATURE FIELD LOADED
          </div>
        </div>
      </div>
    `;
  }

  // =========================================================================
  // THREE.JS VIEWPORT INITIALIZATION & INSTANCED MESH RENDERING
  // =========================================================================

  function detectSoftwareWebGL() {
    try {
      const probeCanvas = document.createElement('canvas');
      const gl = probeCanvas.getContext('webgl2') || probeCanvas.getContext('webgl');
      if (!gl) return true;
      const ext = gl.getExtension('WEBGL_debug_renderer_info');
      const param = ext ? ext.UNMASKED_RENDERER_WEBGL : gl.RENDERER;
      const gpu = String(gl.getParameter(param) || '');
      const loseExt = gl.getExtension('WEBGL_lose_context');
      if (loseExt) loseExt.loseContext();
      return /SwiftShader|llvmpipe|softpipe|Software|Basic Render/i.test(gpu);
    } catch (e) {
      return false;
    }
  }

  function initThree(viewportContainer) {
    if (typeof THREE === 'undefined') {
      viewportContainer.innerHTML = `
        <div style="display:flex; flex-direction:column; align-items:center; justify-content:center; height:100%; padding:24px; text-align:center;">
          <div style="color:var(--vermilion); font-weight:700; font-family:var(--font-sans); margin-bottom:8px;">
            THREE.JS LIBRARY UNAVAILABLE
          </div>
          <div style="font-size:11px; font-family:var(--font-mono); color:var(--muted); max-width:360px;">
            Could not find window.THREE. Ensure vendor/three.min.js is loaded.
          </div>
        </div>
      `;
      return false;
    }

    isSoftwareGl = detectSoftwareWebGL();
    const width = viewportContainer.clientWidth || 640;
    const height = viewportContainer.clientHeight || 520;

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0xF4F2ED);

    camera = new THREE.PerspectiveCamera(42, width / height, 0.005, 100);
    sceneCenter = new THREE.Vector3(0, 0.4, 0);
    sceneRadius = 1.1;
    camera.position.set(1.65, 1.35, 1.85);
    camera.lookAt(sceneCenter);

    renderer = new THREE.WebGLRenderer({
      antialias: !isSoftwareGl,
      alpha: false,
      powerPreference: 'high-performance'
    });
    renderer.localClippingEnabled = true;
    renderer.setSize(width, height);
    renderer.setPixelRatio(isSoftwareGl ? 1 : Math.min(window.devicePixelRatio, 2));
    viewportContainer.appendChild(renderer.domElement);

    renderer.domElement.addEventListener('webglcontextlost', (e) => {
      e.preventDefault();
      if (rAFId) cancelAnimationFrame(rAFId);
      rAFId = null;
    });

    if (typeof THREE.OrbitControls !== 'undefined') {
      controls = new THREE.OrbitControls(camera, renderer.domElement);
      controls.enableDamping = !isSoftwareGl;
      controls.dampingFactor = 0.08;
      controls.target.copy(sceneCenter);
      controls.update();
      controls.addEventListener('start', () => {
        camAnimActive = false; // Never fight manual user orbiting
      });
    }

    // Three-point studio lighting calibrated for paper ivory surface & non-glare thermal reading
    const hemiLight = new THREE.HemisphereLight(0xF6F8FC, 0xD2D4D9, 0.78);
    scene.add(hemiLight);

    const keyLight = new THREE.DirectionalLight(0xFFFDF8, 0.72);
    keyLight.position.set(4.2, 6.4, 5.2);
    scene.add(keyLight);

    const rimLight = new THREE.DirectionalLight(0x1F3FB0, 0.22);
    rimLight.position.set(-4.0, 2.2, -4.0);
    scene.add(rimLight);

    // Subtle calibration floor grid
    gridHelper = new THREE.GridHelper(2.4, 24, 0x1F3FB0, 0xD5D0C5);
    gridHelper.position.y = -0.002;
    gridHelper.visible = showGrid;
    scene.add(gridHelper);

    // Engineering Axis Triad on workbench floor (X right, Y depth, Z vertical up from z=0)
    axisTriadGroup = buildAxisTriad();
    scene.add(axisTriadGroup);

    // Clipping plane & slice-plane indicator frame for cross-section inspection
    clippingPlane = new THREE.Plane(new THREE.Vector3(-1, 0, 0), 10);
    sliceHelperMesh = buildSliceIndicator();
    sliceHelperMesh.visible = false;
    scene.add(sliceHelperMesh);

    // Hovered voxel cell outline highlight box
    const hlGeo = new THREE.EdgesGeometry(new THREE.BoxGeometry(1.04, 1.04, 1.04));
    const hlMat = new THREE.LineBasicMaterial({ color: 0x141414 });
    hoverOutlineMesh = new THREE.LineSegments(hlGeo, hlMat);
    hoverOutlineMesh.visible = false;
    hoverOutlineMesh.renderOrder = 10;
    scene.add(hoverOutlineMesh);

    raycaster = new THREE.Raycaster();
    mouse = new THREE.Vector2();

    // Neutral light-grey structural CAD specimen shown when backend is offline or pre-solve
    ghostWireframeGroup = new THREE.Group();
    scene.add(ghostWireframeGroup);
    rebuildGhostWireframe();
    updateSliceVisuals();

    let lastTime = performance.now();
    function animate(now) {
      rAFId = requestAnimationFrame(animate);
      const dt = Math.min(0.05, Math.max(0.001, (now - lastTime) / 1000.0));
      lastTime = now;

      // Smooth exponential camera preset transition
      if (camAnimActive && camTargetPos && camTargetLookAt) {
        const ease = 1 - Math.exp(-8.5 * dt);
        camera.position.lerp(camTargetPos, ease);
        if (controls) {
          controls.target.lerp(camTargetLookAt, ease);
        } else {
          camera.lookAt(camTargetLookAt);
        }
        if (camera.position.distanceTo(camTargetPos) < 0.005) {
          camera.position.copy(camTargetPos);
          if (controls) controls.target.copy(camTargetLookAt);
          camAnimActive = false;
        }
      }

      if (controls) controls.update();
      if (renderer && scene && camera) {
        renderer.render(scene, camera);
      }
    }
    rAFId = requestAnimationFrame(animate);

    return true;
  }

  function makeAxisLabelSprite(letter, hexColor) {
    const c = document.createElement('canvas');
    c.width = 48;
    c.height = 48;
    const ctx = c.getContext('2d');
    if (ctx) {
      ctx.font = 'bold 26px "Inter Tight", sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillStyle = hexColor;
      ctx.fillText(letter, 24, 24);
    }
    const tex = new THREE.CanvasTexture(c);
    const mat = new THREE.SpriteMaterial({ map: tex, transparent: true, depthTest: false });
    const spr = new THREE.Sprite(mat);
    spr.scale.set(0.11, 0.11, 1);
    return spr;
  }

  function buildAxisTriad() {
    const grp = new THREE.Group();
    grp.position.set(-0.88, 0.004, 0.88);
    const len = 0.24;

    const xMat = new THREE.LineBasicMaterial({ color: 0xC92A2A });
    const yMat = new THREE.LineBasicMaterial({ color: 0x2BAE66 });
    const zMat = new THREE.LineBasicMaterial({ color: 0x1F3FB0 });

    const xGeo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(len, 0, 0)]);
    const yGeo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, 0, -len)]);
    const zGeo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, len, 0)]);

    grp.add(new THREE.Line(xGeo, xMat));
    grp.add(new THREE.Line(yGeo, yMat));
    grp.add(new THREE.Line(zGeo, zMat));

    const sx = makeAxisLabelSprite('X', '#C92A2A');
    sx.position.set(len + 0.06, 0.02, 0);
    const sy = makeAxisLabelSprite('Y', '#2BAE66');
    sy.position.set(0, 0.02, -len - 0.06);
    const sz = makeAxisLabelSprite('Z', '#1F3FB0');
    sz.position.set(0, len + 0.06, 0);
    grp.add(sx, sy, sz);

    return grp;
  }

  function buildSliceIndicator() {
    const planeGeo = new THREE.EdgesGeometry(new THREE.PlaneGeometry(1.34, 1.34));
    const planeMat = new THREE.LineBasicMaterial({ color: 0x1F3FB0, transparent: true, opacity: 0.78 });
    return new THREE.LineSegments(planeGeo, planeMat);
  }

  function getActiveClippingPlanes() {
    const planes = [];
    if (sliceX < 99.5) {
      planes.push(new THREE.Plane(new THREE.Vector3(-1, 0, 0), (sliceX / 100.0 - 0.5) * 1.20));
    }
    if (sliceY < 99.5) {
      planes.push(new THREE.Plane(new THREE.Vector3(0, 0, 1), (sliceY / 100.0 - 0.5) * 1.20));
    }
    if (sliceZ < 99.5) {
      planes.push(new THREE.Plane(new THREE.Vector3(0, -1, 0), (sliceZ / 100.0) * 1.20));
    }
    return planes;
  }

  function addStructuralBox(group, w, h, d, cx, cy, cz, segX = 1, segY = 1, segZ = 1) {
    const geo = new THREE.BoxGeometry(w, h, d);
    // Neutral light-grey engineering structural reference material (never flat ivory, zero fabricated temperature colors)
    const solidMat = new THREE.MeshStandardMaterial({
      color: 0xD4D7DD,
      roughness: 0.66,
      metalness: 0.06,
      side: THREE.DoubleSide,
      clippingPlanes: getActiveClippingPlanes()
    });
    const mesh = new THREE.Mesh(geo, solidMat);
    mesh.position.set(cx, cy, cz);
    mesh.userData.isCadSolid = true;
    group.add(mesh);

    const edges = new THREE.EdgesGeometry(geo);
    const edgeMat = new THREE.LineBasicMaterial({
      color: 0x1E222A,
      transparent: true,
      opacity: 0.78,
      clippingPlanes: getActiveClippingPlanes()
    });
    const line = new THREE.LineSegments(edges, edgeMat);
    line.position.set(cx, cy, cz);
    group.add(line);

    // Subtle structural CAD surface subdivisions indicating FVM domain discretization
    const pts = [];
    const eps = 0.0015;
    const x0 = cx - w * 0.5, x1 = cx + w * 0.5;
    const y0 = cy - h * 0.5, y1 = cy + h * 0.5;
    const z0 = cz - d * 0.5, z1 = cz + d * 0.5;

    for (let i = 1; i < segX; i++) {
      const tx = x0 + (w * i) / segX;
      pts.push(tx, y1 + eps, z0, tx, y1 + eps, z1);
      pts.push(tx, y0, z1 + eps, tx, y1, z1 + eps);
      pts.push(tx, y0, z0 - eps, tx, y1, z0 - eps);
    }
    for (let j = 1; j < segY; j++) {
      const ty = y0 + (h * j) / segY;
      pts.push(x0, ty, z1 + eps, x1, ty, z1 + eps);
      pts.push(x1 + eps, ty, z0, x1 + eps, ty, z1);
      pts.push(x0 - eps, ty, z0, x0 - eps, ty, z1);
    }
    for (let k = 1; k < segZ; k++) {
      const tz = z0 + (d * k) / segZ;
      pts.push(x0, y1 + eps, tz, x1, y1 + eps, tz);
      pts.push(x1 + eps, y0, tz, x1 + eps, y1, tz);
      pts.push(x0 - eps, y0, tz, x0 - eps, y1, tz);
    }

    if (pts.length > 0) {
      const gridGeo = new THREE.BufferGeometry();
      gridGeo.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
      const gridMat = new THREE.LineBasicMaterial({
        color: 0x5A606B,
        transparent: true,
        opacity: 0.25,
        clippingPlanes: getActiveClippingPlanes()
      });
      group.add(new THREE.LineSegments(gridGeo, gridMat));
    }
  }

  function clearGroup(group) {
    while (group.children.length > 0) {
      const child = group.children[0];
      group.remove(child);
      if (child.geometry) child.geometry.dispose();
      if (child.material) {
        if (child.material.map) child.material.map.dispose();
        child.material.dispose();
      }
    }
  }

  function createStructuralCellMaterial() {
    return new THREE.MeshStandardMaterial({
      color: 0xC8CBD0,
      roughness: 0.68,
      metalness: 0.08,
      side: THREE.DoubleSide
    });
  }

  function applyGhostSliceAndScale() {
    if (!ghostInstancedCells || !ghostTransforms) return;
    const { count, px, py, pz, sx, sy, sz, nx, ny, nz } = ghostTransforms;
    const cutX = sliceX / 100.0 + 1e-4;
    const cutY = sliceY / 100.0 + 1e-4;
    const cutZ = sliceZ / 100.0 + 1e-4;
    const scale = voxelScale;

    for (let i = 0; i < count; i++) {
      const visible = (nx[i] <= cutX) && (ny[i] <= cutY) && (nz[i] <= cutZ);

      if (!visible) {
        _pos.set(px[i], py[i], pz[i]);
        _quat.identity();
        _scale.set(0, 0, 0);
      } else {
        _pos.set(px[i], py[i], pz[i]);
        _quat.identity();
        _scale.set(sx[i] * scale, sy[i] * scale, sz[i] * scale);
      }
      _mat4.compose(_pos, _quat, _scale);
      ghostInstancedCells.setMatrixAt(i, _mat4);
    }
    ghostInstancedCells.instanceMatrix.needsUpdate = true;
    if (typeof ghostInstancedCells.computeBoundingSphere === 'function') {
      ghostInstancedCells.computeBoundingSphere();
    }
    if (hoverOutlineMesh) hoverOutlineMesh.visible = false;
  }

  function rebuildGhostWireframe() {
    if (!ghostWireframeGroup || typeof THREE === 'undefined') return;
    clearGroup(ghostWireframeGroup);
    if (ghostInstancedCells) {
      if (ghostInstancedCells.geometry) ghostInstancedCells.geometry.dispose();
      if (ghostInstancedCells.material) ghostInstancedCells.material.dispose();
      ghostInstancedCells = null;
    }
    ghostTransforms = null;

    const N = Math.max(4, Math.min(20, selectedResolution || 10));
    const cellEntries = []; // { ex, ey, ez, dx, dy, dz }

    if (selectedGeometry === 'CUBE') {
      const step = 100.0 / N;
      for (let k = 0; k < N; k++) {
        const ez = (k + 0.5) * step;
        for (let j = 0; j < N; j++) {
          const ey = (j + 0.5) * step;
          for (let i = 0; i < N; i++) {
            const ex = (i + 0.5) * step;
            cellEntries.push({ ex, ey, ez, dx: step, dy: step, dz: step });
          }
        }
      }
      sceneCenter.set(0, 0.60, 0);
      sceneRadius = 1.04;
    } else {
      // Model C: Exact Four-Finned Heat Sink Geometry (Baseplate + 4 Fins)
      const step = 100.0 / N;
      const baseplateZ = 20.0;
      const finRanges = [
        [0, 10],
        [30, 40],
        [60, 70],
        [90, 100]
      ];

      for (let k = 0; k < N; k++) {
        const ez = (k + 0.5) * step;
        const inBase = (ez <= baseplateZ);
        for (let j = 0; j < N; j++) {
          const ey = (j + 0.5) * step;
          for (let i = 0; i < N; i++) {
            const ex = (i + 0.5) * step;
            if (inBase) {
              cellEntries.push({ ex, ey, ez, dx: step, dy: step, dz: step });
            } else {
              const inFin = finRanges.some(([x0, x1]) => ex >= (x0 - 1e-4) && ex <= (x1 + 1e-4));
              if (inFin) {
                cellEntries.push({ ex, ey, ez, dx: step, dy: step, dz: step });
              }
            }
          }
        }
      }
      sceneCenter.set(0, 0.54, 0);
      sceneRadius = 1.08;
    }

    const count = cellEntries.length;
    const normFactor = 0.012; // 100mm = 1.20 world units
    const pxArr = new Float32Array(count);
    const pyArr = new Float32Array(count);
    const pzArr = new Float32Array(count);
    const sxArr = new Float32Array(count);
    const syArr = new Float32Array(count);
    const szArr = new Float32Array(count);
    const nxArr = new Float32Array(count);
    const nyArr = new Float32Array(count);
    const nzArr = new Float32Array(count);

    for (let idx = 0; idx < count; idx++) {
      const c = cellEntries[idx];
      pxArr[idx] = (c.ex - 50.0) * normFactor;
      pyArr[idx] = c.ez * normFactor;
      pzArr[idx] = -(c.ey - 50.0) * normFactor;

      sxArr[idx] = c.dx * normFactor;
      syArr[idx] = c.dz * normFactor;
      szArr[idx] = c.dy * normFactor;

      nxArr[idx] = c.ex / 100.0;
      nyArr[idx] = c.ey / 100.0;
      nzArr[idx] = c.ez / 100.0;
    }

    ghostTransforms = {
      count,
      px: pxArr, py: pyArr, pz: pzArr,
      sx: sxArr, sy: syArr, sz: szArr,
      nx: nxArr, ny: nyArr, nz: nzArr,
      xs: cellEntries.map(c => c.ex),
      ys: cellEntries.map(c => c.ey),
      zs: cellEntries.map(c => c.ez)
    };

    const unitGeo = new THREE.BoxGeometry(1, 1, 1);
    const ghostMat = createStructuralCellMaterial();
    ghostInstancedCells = new THREE.InstancedMesh(unitGeo, ghostMat, count);
    ghostInstancedCells.userData.isGhostCells = true;

    ghostWireframeGroup.add(ghostInstancedCells);
    ghostWireframeGroup.visible = !currentParsedField;

    applyGhostSliceAndScale();
  }

  function clearTemperatureFieldMesh() {
    if (instancedCells && scene) {
      scene.remove(instancedCells);
      if (instancedCells.geometry) instancedCells.geometry.dispose();
      if (instancedCells.material) instancedCells.material.dispose();
      instancedCells = null;
    }
    if (cellEdgeSegments && scene) {
      scene.remove(cellEdgeSegments);
      if (cellEdgeSegments.geometry) cellEdgeSegments.geometry.dispose();
      if (cellEdgeSegments.material) cellEdgeSegments.material.dispose();
      cellEdgeSegments = null;
    }
    if (hoverOutlineMesh) {
      hoverOutlineMesh.visible = false;
    }
    thermalMaterialUniforms = null;
    instanceTransforms = null;
    currentParsedField = null;
    lastSolvedKey = null;
    if (ghostWireframeGroup) {
      ghostWireframeGroup.visible = true;
      applyGhostSliceAndScale();
    }
    updateLegendDisplay(null);
    const readout = containerEl?.querySelector('#t3d-cell-readout');
    if (readout) {
      readout.textContent = 'ORBIT: DRAG · ZOOM: SCROLL · NO TEMPERATURE FIELD LOADED';
    }
  }

  // Strictly validates and extracts backend coordinates + temperatures without inventing data
  function extractAuthoritativeField(data) {
    if (!data || typeof data !== 'object') {
      return { ok: false, reason: 'INVALID_RESPONSE', detail: 'Empty or non-object JSON payload.' };
    }

    const rawStatus = pickField(data, ['status', 'metrics.status', 'solver_status', 'metrics.solver_status', 'convergence_status', 'state']);
    if (!rawStatus || typeof rawStatus !== 'string') {
      return { ok: false, reason: 'INVALID_RESPONSE', detail: 'Missing authoritative solver status string in response.' };
    }

    const statusUpper = rawStatus.trim().toUpperCase();
    const isUsable = (statusUpper === 'SUCCESS' || statusUpper === 'CONVERGED');

    if (!isUsable) {
      return {
        ok: false,
        reason: statusUpper,
        detail: pickField(data, ['message', 'error_message', 'reason']) || `Solver terminated with status ${statusUpper}.`
      };
    }

    // Attempt extraction of authoritative cell coordinates & temperatures
    let xs = null, ys = null, zs = null, temps = null;
    let dxArr = null, dyArr = null, dzArr = null;
    let uniformStep = null;

    const cellsArray = pickField(data, ['cells', 'field.cells', 'solution.cells', 'results.cells', 'temperature_field']);
    if (Array.isArray(cellsArray) && cellsArray.length > 0) {
      const len = cellsArray.length;
      xs = new Float32Array(len);
      ys = new Float32Array(len);
      zs = new Float32Array(len);
      temps = new Float32Array(len);

      let hasSizes = false;
      dxArr = new Float32Array(len);
      dyArr = new Float32Array(len);
      dzArr = new Float32Array(len);

      for (let i = 0; i < len; i++) {
        const c = cellsArray[i];
        if (!c || typeof c !== 'object') {
          return { ok: false, reason: 'INVALID_RESPONSE', detail: `Malformed cell entry at index ${i}.` };
        }
        const x = Number(c.x ?? c.cx ?? c.pos_x);
        const y = Number(c.y ?? c.cy ?? c.pos_y);
        const z = Number(c.z ?? c.cz ?? c.pos_z);
        const t = Number(c.temperature ?? c.temp ?? c.t ?? c.T);
        if (!isFinite(x) || !isFinite(y) || !isFinite(z) || !isFinite(t)) {
          return { ok: false, reason: 'INVALID_RESPONSE', detail: `Non-finite coordinate or temperature at cell ${i}.` };
        }
        xs[i] = x;
        ys[i] = y;
        zs[i] = z;
        temps[i] = t;

        if (c.dx !== undefined && c.dy !== undefined && c.dz !== undefined) {
          hasSizes = true;
          dxArr[i] = Number(c.dx);
          dyArr[i] = Number(c.dy);
          dzArr[i] = Number(c.dz);
        }
      }
      if (!hasSizes) {
        dxArr = dyArr = dzArr = null;
      }
    } else {
      // Parallel array structures
      const coordsFlat = pickField(data, ['thermal_field.coordinates', 'coordinates', 'positions', 'field.coordinates']);
      const xArr = pickField(data, ['x', 'xs', 'cell_x', 'field.x']);
      const yArr = pickField(data, ['y', 'ys', 'cell_y', 'field.y']);
      const zArr = pickField(data, ['z', 'zs', 'cell_z', 'field.z']);
      const tArr = pickField(data, ['thermal_field.temperatures', 'temperatures', 'temperature', 'T', 'field.temperatures', 'solution.temperatures']);

      if (Array.isArray(tArr) && tArr.length > 0) {
        const len = tArr.length;
        temps = new Float32Array(len);
        xs = new Float32Array(len);
        ys = new Float32Array(len);
        zs = new Float32Array(len);

        if (Array.isArray(xArr) && Array.isArray(yArr) && Array.isArray(zArr) &&
            xArr.length === len && yArr.length === len && zArr.length === len) {
          for (let i = 0; i < len; i++) {
            xs[i] = Number(xArr[i]);
            ys[i] = Number(yArr[i]);
            zs[i] = Number(zArr[i]);
            temps[i] = Number(tArr[i]);
            if (!isFinite(xs[i]) || !isFinite(ys[i]) || !isFinite(zs[i]) || !isFinite(temps[i])) {
              return { ok: false, reason: 'INVALID_RESPONSE', detail: `Non-finite numeric value at cell ${i}.` };
            }
          }
        } else if (Array.isArray(coordsFlat) && coordsFlat.length === len * 3) {
          for (let i = 0; i < len; i++) {
            xs[i] = Number(coordsFlat[i * 3]);
            ys[i] = Number(coordsFlat[i * 3 + 1]);
            zs[i] = Number(coordsFlat[i * 3 + 2]);
            temps[i] = Number(tArr[i]);
            if (!isFinite(xs[i]) || !isFinite(ys[i]) || !isFinite(zs[i]) || !isFinite(temps[i])) {
              return { ok: false, reason: 'INVALID_RESPONSE', detail: `Non-finite coordinate/temperature value at index ${i}.` };
            }
          }
        } else if (Array.isArray(coordsFlat) && coordsFlat.length === len && Array.isArray(coordsFlat[0])) {
          for (let i = 0; i < len; i++) {
            xs[i] = Number(coordsFlat[i][0]);
            ys[i] = Number(coordsFlat[i][1]);
            zs[i] = Number(coordsFlat[i][2]);
            temps[i] = Number(tArr[i]);
            if (!isFinite(xs[i]) || !isFinite(ys[i]) || !isFinite(zs[i]) || !isFinite(temps[i])) {
              return { ok: false, reason: 'INVALID_RESPONSE', detail: `Non-finite nested coordinate at index ${i}.` };
            }
          }
        } else {
          return { ok: false, reason: 'INVALID_RESPONSE', detail: 'Coordinates array length does not match temperatures array.' };
        }
      }
    }

    if (!temps || temps.length === 0) {
      return { ok: false, reason: 'INVALID_RESPONSE', detail: 'Solver reported ' + statusUpper + ' without usable cell coordinates or temperatures.' };
    }

    // Optional cell size scalar / spacing from backend
    const cellSizeObj = pickField(data, ['cell_size', 'spacing', 'dx', 'h']);
    if (typeof cellSizeObj === 'number' && isFinite(cellSizeObj) && cellSizeObj > 0) {
      uniformStep = [cellSizeObj, cellSizeObj, cellSizeObj];
    } else if (cellSizeObj && typeof cellSizeObj === 'object') {
      const sx = Number(cellSizeObj.x ?? cellSizeObj.dx);
      const sy = Number(cellSizeObj.y ?? cellSizeObj.dy);
      const sz = Number(cellSizeObj.z ?? cellSizeObj.dz);
      if (isFinite(sx) && isFinite(sy) && isFinite(sz) && sx > 0 && sy > 0 && sz > 0) {
        uniformStep = [sx, sy, sz];
      }
    }

    // Compute bounds & temperature range for visualization color normalization
    let minX = Infinity, maxX = -Infinity;
    let minY = Infinity, maxY = -Infinity;
    let minZ = Infinity, maxZ = -Infinity;
    let fieldMinT = Infinity, fieldMaxT = -Infinity;

    for (let i = 0; i < temps.length; i++) {
      if (xs[i] < minX) minX = xs[i];
      if (xs[i] > maxX) maxX = xs[i];
      if (ys[i] < minY) minY = ys[i];
      if (ys[i] > maxY) maxY = ys[i];
      if (zs[i] < minZ) minZ = zs[i];
      if (zs[i] > maxZ) maxZ = zs[i];
      if (temps[i] < fieldMinT) fieldMinT = temps[i];
      if (temps[i] > fieldMaxT) fieldMaxT = temps[i];
    }

    // Backend-supplied min / max / avg stats (only displayed in HUD when returned by backend)
    const backendMinT = pickField(data, ['thermal_field.min_temp_degC', 'min_temperature', 'min_temp', 'temperature_min', 'stats.min_temperature', 'temperature_stats.min']);
    const backendMaxT = pickField(data, ['thermal_field.max_temp_degC', 'max_temperature', 'max_temp', 'temperature_max', 'stats.max_temperature', 'temperature_stats.max']);
    const backendAvgT = pickField(data, ['thermal_field.avg_temp_degC', 'avg_temperature', 'mean_temperature', 'avg_temp', 'stats.avg_temperature', 'temperature_stats.avg']);

    return {
      ok: true,
      status: statusUpper,
      count: temps.length,
      xs, ys, zs, temps,
      dxArr, dyArr, dzArr,
      uniformStep,
      bounds: { minX, maxX, minY, maxY, minZ, maxZ },
      colorMinT: typeof backendMinT === 'number' && isFinite(backendMinT) ? backendMinT : fieldMinT,
      colorMaxT: typeof backendMaxT === 'number' && isFinite(backendMaxT) ? backendMaxT : fieldMaxT,
      backendStats: {
        min: typeof backendMinT === 'number' && isFinite(backendMinT) ? backendMinT : null,
        max: typeof backendMaxT === 'number' && isFinite(backendMaxT) ? backendMaxT : null,
        avg: typeof backendAvgT === 'number' && isFinite(backendAvgT) ? backendAvgT : null
      }
    };
  }

  // Estimate cell pitch from sorted unique coordinates when explicit dx/dy/dz isn't provided
  function estimateAxisStep(arr, spanFallback) {
    const vals = [];
    const seen = new Set();
    const limit = Math.min(arr.length, 400);
    for (let i = 0; i < limit; i++) {
      const rounded = Math.round(arr[i] * 1e6) / 1e6;
      if (!seen.has(rounded)) {
        seen.add(rounded);
        vals.push(rounded);
      }
    }
    if (vals.length < 2) return spanFallback || 0.05;
    vals.sort((a, b) => a - b);
    let minDiff = Infinity;
    for (let i = 1; i < vals.length; i++) {
      const diff = vals[i] - vals[i - 1];
      if (diff > 1e-7 && diff < minDiff) minDiff = diff;
    }
    return isFinite(minDiff) ? minDiff : (spanFallback || 0.05);
  }

  function detectUpAxis(field) {
    if (selectedGeometry !== 'FINNED_SINK') return 'z';
    const { count, ys, zs, bounds } = field;
    const ySpan = Math.max(1e-9, bounds.maxY - bounds.minY);
    const zSpan = Math.max(1e-9, bounds.maxZ - bounds.minZ);
    let lowZ = 0, highZ = 0, lowY = 0, highY = 0;
    for (let i = 0; i < count; i++) {
      const nz = (zs[i] - bounds.minZ) / zSpan;
      const ny = (ys[i] - bounds.minY) / ySpan;
      if (nz < 0.18) lowZ++;
      if (nz > 0.82) highZ++;
      if (ny < 0.18) lowY++;
      if (ny > 0.82) highY++;
    }
    // Baseplate has ~2.5x more active cells than the 4 fins layer; fins are at the top!
    if (highY < lowY * 0.65 && highZ >= lowZ * 0.85) return 'y';
    return 'z';
  }

  function createFvmHeatmapMaterial() {
    const uniforms = {
      uEdgeStrength: { value: showEdges ? 0.56 : 0.20 },
      uEdgeWidth: { value: showEdges ? 0.072 : 0.042 }
    };
    const mat = new THREE.MeshStandardMaterial({
      color: 0xffffff,
      roughness: 0.62,
      metalness: 0.03
    });
    mat.onBeforeCompile = function(shader) {
      shader.uniforms.uEdgeStrength = uniforms.uEdgeStrength;
      shader.uniforms.uEdgeWidth = uniforms.uEdgeWidth;
      shader.vertexShader = 'varying vec2 vFvmCellUv;\n' + shader.vertexShader.replace(
        '#include <uv_vertex>',
        '#include <uv_vertex>\n  vFvmCellUv = uv;'
      );
      shader.fragmentShader = 'varying vec2 vFvmCellUv;\nuniform float uEdgeStrength;\nuniform float uEdgeWidth;\n' + shader.fragmentShader.replace(
        '#include <color_fragment>',
        '#include <color_fragment>\n  float fvmEdge = min(min(vFvmCellUv.x, 1.0 - vFvmCellUv.x), min(vFvmCellUv.y, 1.0 - vFvmCellUv.y));\n  float fvmBorder = 1.0 - smoothstep(0.0, uEdgeWidth, fvmEdge);\n  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.08, 0.09, 0.11), fvmBorder * uEdgeStrength);'
      );
    };
    mat.customProgramCacheKey = () => 'uew-fvm-cell-border-v1';
    thermalMaterialUniforms = uniforms;
    return mat;
  }

  function applyFieldSliceAndScale() {
    if (!instancedCells || !instanceTransforms) return;
    const { count, px, py, pz, sx, sy, sz, nx, ny, nz } = instanceTransforms;
    const cutX = sliceX / 100.0 + 1e-4;
    const cutY = sliceY / 100.0 + 1e-4;
    const cutZ = sliceZ / 100.0 + 1e-4;
    const scale = voxelScale;

    for (let i = 0; i < count; i++) {
      const visible = (nx[i] <= cutX) && (ny[i] <= cutY) && (nz[i] <= cutZ);

      if (!visible) {
        _pos.set(px[i], py[i], pz[i]);
        _quat.identity();
        _scale.set(0, 0, 0);
      } else {
        _pos.set(px[i], py[i], pz[i]);
        _quat.identity();
        _scale.set(sx[i] * scale, sy[i] * scale, sz[i] * scale);
      }
      _mat4.compose(_pos, _quat, _scale);
      instancedCells.setMatrixAt(i, _mat4);
    }
    instancedCells.instanceMatrix.needsUpdate = true;
    if (typeof instancedCells.computeBoundingSphere === 'function') {
      instancedCells.computeBoundingSphere();
    }
    if (hoverOutlineMesh) hoverOutlineMesh.visible = false;
  }

  function syncSliceControlsUI() {
    if (!containerEl) return;
    const slX = containerEl.querySelector('#slider-slice-x');
    const valX = containerEl.querySelector('#val-slice-x');
    const slY = containerEl.querySelector('#slider-slice-y');
    const valY = containerEl.querySelector('#val-slice-y');
    const slZ = containerEl.querySelector('#slider-slice-z');
    const valZ = containerEl.querySelector('#val-slice-z');
    const slScale = containerEl.querySelector('#slider-voxel-scale');
    const valScale = containerEl.querySelector('#val-voxel-scale');

    if (slX) slX.value = sliceX;
    if (valX) valX.textContent = `${sliceX}%`;
    if (slY) slY.value = sliceY;
    if (valY) valY.textContent = `${sliceY}%`;
    if (slZ) slZ.value = sliceZ;
    if (valZ) valZ.textContent = `${sliceZ}%`;
    if (slScale) slScale.value = Math.round(voxelScale * 100);
    if (valScale) valScale.textContent = `${Math.round(voxelScale * 100)}%`;
  }

  function updateSliceVisuals() {
    const planes = getActiveClippingPlanes();

    if (ghostWireframeGroup) {
      ghostWireframeGroup.traverse(obj => {
        if (obj.material) {
          obj.material.clippingPlanes = planes;
        }
      });
    }

    if (sliceHelperMesh) {
      const cuts = [];
      if (sliceX < 99.5) cuts.push('x');
      if (sliceY < 99.5) cuts.push('y');
      if (sliceZ < 99.5) cuts.push('z');

      if (cuts.length === 1) {
        sliceHelperMesh.visible = true;
        sliceHelperMesh.rotation.set(0, 0, 0);
        if (cuts[0] === 'x') {
          sliceHelperMesh.rotation.y = Math.PI * 0.5;
          sliceHelperMesh.position.set((sliceX / 100.0 - 0.5) * 1.20, 0.60, 0);
        } else if (cuts[0] === 'y') {
          sliceHelperMesh.position.set(0, 0.60, -(sliceY / 100.0 - 0.5) * 1.20);
        } else if (cuts[0] === 'z') {
          sliceHelperMesh.rotation.x = Math.PI * 0.5;
          sliceHelperMesh.position.set(0, (sliceZ / 100.0) * 1.20, 0);
        }
      } else {
        sliceHelperMesh.visible = false;
      }
    }

    if (instancedCells && instanceTransforms) {
      applyFieldSliceAndScale();
    } else if (ghostInstancedCells && ghostTransforms) {
      applyGhostSliceAndScale();
    }
  }

  function renderTemperatureField(field) {
    if (!scene || typeof THREE === 'undefined') return;
    clearTemperatureFieldMesh();

    currentParsedField = field;
    if (ghostWireframeGroup) ghostWireframeGroup.visible = false;

    const { count, xs, ys, zs, temps, dxArr, dyArr, dzArr, uniformStep, bounds, colorMinT, colorMaxT } = field;
    const upAxis = detectUpAxis(field);

    const engYs = (upAxis === 'z') ? ys : zs;
    const engZs = (upAxis === 'z') ? zs : ys;
    const minEngY = (upAxis === 'z') ? bounds.minY : bounds.minZ;
    const maxEngY = (upAxis === 'z') ? bounds.maxY : bounds.maxZ;
    const minEngZ = (upAxis === 'z') ? bounds.minZ : bounds.minY;
    const maxEngZ = (upAxis === 'z') ? bounds.maxZ : bounds.maxY;

    const spanX = Math.max(1e-6, bounds.maxX - bounds.minX);
    const spanY = Math.max(1e-6, maxEngY - minEngY);
    const spanZ = Math.max(1e-6, maxEngZ - minEngZ);
    const stepX = uniformStep ? uniformStep[0] : estimateAxisStep(xs, spanX / selectedResolution);
    const stepY = uniformStep ? (upAxis === 'z' ? uniformStep[1] : uniformStep[2]) : estimateAxisStep(engYs, spanY / selectedResolution);
    const stepZ = uniformStep ? (upAxis === 'z' ? uniformStep[2] : uniformStep[1]) : estimateAxisStep(engZs, spanZ / selectedResolution);

    const fullSpanX = spanX + stepX;
    const fullSpanY = spanY + stepY;
    const fullSpanZ = spanZ + stepZ;
    const maxFullSpan = Math.max(fullSpanX, fullSpanY, fullSpanZ);
    const normFactor = 1.20 / maxFullSpan; // 100mm normalized cleanly onto 1.20m reference cube

    const cxMid = (bounds.minX + bounds.maxX) * 0.5;
    const cyMid = (minEngY + maxEngY) * 0.5;
    const zBottomOffset = stepZ * 0.5 - minEngZ;

    const unitGeo = new THREE.BoxGeometry(1, 1, 1);
    const material = createFvmHeatmapMaterial();

    instancedCells = new THREE.InstancedMesh(unitGeo, material, count);
    const tempRange = Math.max(1e-9, colorMaxT - colorMinT);

    const pxArr = new Float32Array(count);
    const pyArr = new Float32Array(count);
    const pzArr = new Float32Array(count);
    const sxArr = new Float32Array(count);
    const syArr = new Float32Array(count);
    const szArr = new Float32Array(count);
    const nxArr = new Float32Array(count);
    const nyArr = new Float32Array(count);
    const nzArr = new Float32Array(count);

    for (let i = 0; i < count; i++) {
      const ex = xs[i];
      const ey = engYs[i];
      const ez = engZs[i];

      pxArr[i] = (ex - cxMid) * normFactor;
      pyArr[i] = (ez + zBottomOffset) * normFactor; // Engineering Z (height) -> World Y (up from 0)
      pzArr[i] = -(ey - cyMid) * normFactor;        // Engineering Y (depth) -> -World Z (right-handed triad)

      sxArr[i] = (dxArr ? dxArr[i] : stepX) * normFactor;
      syArr[i] = (dzArr ? dzArr[i] : stepZ) * normFactor;
      szArr[i] = (dyArr ? dyArr[i] : stepY) * normFactor;

      nxArr[i] = (ex - bounds.minX) / spanX;
      nyArr[i] = (ey - minEngY) / spanY;
      nzArr[i] = (ez - minEngZ) / spanZ;

      const normT = (temps[i] - colorMinT) / tempRange;
      const [r, g, b] = thermalNormToRgb(normT);
      _color.setRGB(r, g, b);
      instancedCells.setColorAt(i, _color);
    }

    instanceTransforms = {
      count,
      px: pxArr, py: pyArr, pz: pzArr,
      sx: sxArr, sy: syArr, sz: szArr,
      nx: nxArr, ny: nyArr, nz: nzArr
    };

    if (instancedCells.instanceColor) instancedCells.instanceColor.needsUpdate = true;
    applyFieldSliceAndScale();
    scene.add(instancedCells);

    // Update bounding center & radius for camera presets
    const worldHeight = fullSpanZ * normFactor;
    sceneCenter.set(0, worldHeight * 0.5, 0);
    sceneRadius = Math.max(0.88, Math.sqrt(fullSpanX * fullSpanX + fullSpanY * fullSpanY + fullSpanZ * fullSpanZ) * normFactor * 0.62);

    updateSliceVisuals();
    updateLegendDisplay(field.backendStats);
    applyCameraPreset('iso');
  }

  function applyCameraPreset(preset) {
    if (!camera || !sceneCenter) return;

    const r = sceneRadius;
    let targetPos;

    if (preset === 'front') {
      targetPos = new THREE.Vector3(sceneCenter.x, sceneCenter.y + 0.08 * r, sceneCenter.z + 2.35 * r);
    } else if (preset === 'top') {
      targetPos = new THREE.Vector3(sceneCenter.x, sceneCenter.y + 2.45 * r, sceneCenter.z + 0.08 * r);
    } else {
      // 'iso' or 'reset'
      targetPos = new THREE.Vector3(sceneCenter.x + 1.55 * r, sceneCenter.y + 1.15 * r, sceneCenter.z + 1.75 * r);
    }

    camTargetPos = targetPos;
    camTargetLookAt = sceneCenter.clone();
    camAnimActive = true;
  }

  // =========================================================================
  // UI UPDATE & BACKEND DIAGNOSTICS
  // =========================================================================

  function syncActionButtons() {
    const runBtn = containerEl?.querySelector('#btn-t3d-run');
    const cmpBtn = containerEl?.querySelector('#btn-t3d-compare-run');
    if (runBtn) runBtn.disabled = (!backendOnline || isSolving);
    if (cmpBtn) cmpBtn.disabled = (!backendOnline || isComparing);
  }

  function updateSelectionStatusBar() {
    const bar = containerEl?.querySelector('#t3d-select-status-bar');
    if (!bar) return;

    bar.classList.toggle('is-offline', !backendOnline);
    const titleEl = bar.querySelector('#t3d-select-status-title');
    const bodyEl = bar.querySelector('#t3d-select-status-body');

    if (titleEl) {
      titleEl.style.color = backendChecking ? 'var(--cobalt)' : (backendOnline ? '#2E7D32' : 'var(--vermilion)');
      titleEl.textContent = backendChecking
        ? 'CHECKING C++17 BACKEND...'
        : (backendOnline ? 'C++17 BACKEND ONLINE' : 'BACKEND OFFLINE — STRUCTURAL CAD REFERENCE MODE');
    }
    if (bodyEl) {
      bodyEl.textContent = backendOnline
        ? `Connected to ${API.getThermal3dBaseUrl() || 'local origin'}. Live temperature solves and solver comparisons are enabled.`
        : 'Both 3D model workspaces remain open for structural CAD inspection. Simulation execution is disabled until the local C++17 engine connects.';
    }
  }

  function updateTopBackendChip() {
    const slotRight = document.querySelector('#detail-top-right');
    const slotLeftExtra = document.querySelector('#detail-top-left-extra');

    if (slotLeftExtra) {
      if (screenMode === 'workspace') {
        slotLeftExtra.innerHTML = `<button class="box cobalt" id="btn-t3d-top-change-model" type="button">CHANGE MODEL</button>`;
        slotLeftExtra.querySelector('#btn-t3d-top-change-model')?.addEventListener('click', () => {
          returnToModelSelection();
        });
      } else {
        slotLeftExtra.innerHTML = '';
      }
    }

    if (!slotRight) return;

    const cls = backendOnline ? 'online' : 'offline';
    const text = backendChecking
      ? 'CHECKING C++17 SOLVER...'
      : (backendOnline ? 'C++17 BACKEND ONLINE' : 'BACKEND OFFLINE');

    const contractBtnHtml = (screenMode === 'workspace')
      ? '<button class="box" id="btn-t3d-top-contract" type="button" style="height: 24px; font-size: 9.5px;">API CONTRACT</button>'
      : '';

    slotRight.innerHTML = `
      ${contractBtnHtml}
      <div class="status-chip ${cls}" style="font-size: 9.5px; padding: 3px 9px;">
        <span class="chip-dot"></span>
        <span>${text}</span>
      </div>
    `;

    slotRight.querySelector('#btn-t3d-top-contract')?.addEventListener('click', () => {
      switchTab('contract');
    });

    if (screenMode === 'selection') {
      updateSelectionStatusBar();
    } else {
      syncActionButtons();
    }
  }

  function dismissStageNotice() {
    if (noticeDismissTimer) {
      clearTimeout(noticeDismissTimer);
      noticeDismissTimer = null;
    }
    const box = containerEl?.querySelector('#t3d-stage-notice');
    if (box) {
      box.classList.add('is-dismissing');
      setTimeout(() => {
        if (box) {
          box.style.display = 'none';
          box.classList.remove('is-dismissing');
        }
      }, 200);
    }
    structuralNoticeDismissed = true;
  }

  function setStageNotice(mode, title, body, showRetryBtn = false) {
    const box = containerEl?.querySelector('#t3d-stage-notice');
    if (!box) return;

    if (noticeDismissTimer) {
      clearTimeout(noticeDismissTimer);
      noticeDismissTimer = null;
    }

    if (!mode) {
      box.style.display = 'none';
      return;
    }

    // Do not pop back up if user already dismissed the informational notice
    if (mode === 'info' && structuralNoticeDismissed) {
      return;
    }

    box.style.display = 'flex';
    box.classList.remove('is-dismissing');
    box.className = 't3d-stage-notice ' + (mode === 'offline' ? 'is-offline' : (mode === 'error' ? 'is-error' : ''));

    const titleEl = box.querySelector('#t3d-notice-title');
    const bodyEl = box.querySelector('#t3d-notice-body');
    const btnEl = box.querySelector('#btn-t3d-notice-action');

    if (titleEl) titleEl.textContent = title;
    if (bodyEl) bodyEl.textContent = body;
    if (btnEl) btnEl.style.display = showRetryBtn ? 'inline-flex' : 'none';

    // Automatically dismiss info notice after 5 seconds if user is not actively interacting with it
    if (mode === 'info') {
      noticeDismissTimer = setTimeout(() => {
        dismissStageNotice();
      }, 5000);
    }
  }

  function updateLegendDisplay(stats) {
    const legendEl = containerEl?.querySelector('#t3d-legend');
    const maxEl = containerEl?.querySelector('#t3d-leg-max');
    const t75El = containerEl?.querySelector('#t3d-leg-75');
    const midCapEl = containerEl?.querySelector('#t3d-leg-mid-cap');
    const midEl = containerEl?.querySelector('#t3d-leg-mid');
    const t25El = containerEl?.querySelector('#t3d-leg-25');
    const minEl = containerEl?.querySelector('#t3d-leg-min');
    const statsLine = containerEl?.querySelector('#t3d-vp-stats-line');

    if (!currentParsedField) {
      if (legendEl) legendEl.classList.add('is-unavailable');
      if (maxEl) maxEl.textContent = '—';
      if (t75El) t75El.textContent = '—';
      if (midCapEl) midCapEl.textContent = 'AVG';
      if (midEl) midEl.textContent = '—';
      if (t25El) t25El.textContent = '—';
      if (minEl) minEl.textContent = '—';
      if (statsLine) {
        statsLine.textContent = 'STRUCTURAL REFERENCE — NO LIVE TEMPERATURE DATA';
      }
      return;
    }

    if (legendEl) legendEl.classList.remove('is-unavailable');

    const s = stats || currentParsedField.backendStats || {};
    const hasMin = (s.min !== null && s.min !== undefined);
    const hasMax = (s.max !== null && s.max !== undefined);
    const hasAvg = (s.avg !== null && s.avg !== undefined);

    const minVal = hasMin ? s.min : currentParsedField.colorMinT;
    const maxVal = hasMax ? s.max : currentParsedField.colorMaxT;
    const hasFieldBounds = isFinite(minVal) && isFinite(maxVal);

    const minStr = hasMin ? `${s.min.toFixed(1)}` : (hasFieldBounds ? `${minVal.toFixed(1)}` : '—');
    const maxStr = hasMax ? `${s.max.toFixed(1)}` : (hasFieldBounds ? `${maxVal.toFixed(1)}` : '—');
    const t75Str = hasFieldBounds ? `${(minVal + (maxVal - minVal) * 0.75).toFixed(1)}` : '—';
    const t25Str = hasFieldBounds ? `${(minVal + (maxVal - minVal) * 0.25).toFixed(1)}` : '—';
    const avgStr = hasAvg ? `${s.avg.toFixed(2)} °C` : null;
    const midVal = hasAvg ? `${s.avg.toFixed(1)}` : (hasFieldBounds ? `${((minVal + maxVal) * 0.5).toFixed(1)}` : '—');

    if (maxEl) maxEl.textContent = maxStr;
    if (t75El) t75El.textContent = t75Str;
    if (midCapEl) midCapEl.textContent = hasAvg ? 'AVG' : 'MID';
    if (midEl) midEl.textContent = midVal;
    if (t25El) t25El.textContent = t25Str;
    if (minEl) minEl.textContent = minStr;

    if (statsLine) {
      const parts = [];
      if (hasMin) parts.push(`MIN ${s.min.toFixed(2)} °C`);
      if (avgStr) parts.push(`AVG ${avgStr}`);
      if (hasMax) parts.push(`MAX ${s.max.toFixed(2)} °C`);
      statsLine.textContent = parts.length > 0
        ? parts.join(' · ')
        : `${currentParsedField.count.toLocaleString()} ACTIVE CELLS · ${minVal.toFixed(1)}–${maxVal.toFixed(1)} °C`;
    }
  }

  function updateViewportHeader() {
    const titleEl = containerEl?.querySelector('#t3d-vp-model-title');
    if (titleEl) {
      titleEl.textContent = `${selectedGeometry} · N=${selectedResolution} · ${selectedSolver}`;
    }
    const cmpMeta = containerEl?.querySelector('#t3d-compare-meta');
    if (cmpMeta) {
      cmpMeta.textContent = `${selectedGeometry} / N=${selectedResolution}`;
    }
  }

  function extractMetricsFromResponse(data) {
    const status = pickField(data, ['status', 'solver_status', 'convergence_status', 'state', 'metrics.status']);
    const activeCells = pickField(data, ['mesh.active_cells_M', 'active_cells', 'cell_count', 'num_cells', 'mesh.active_cells']);
    const nnz = pickField(data, ['mesh.matrix_nnz', 'matrix_nnz', 'nnz', 'nonzeros', 'matrix.nnz', 'matrix_nonzeros']);
    const iterations = pickField(data, ['metrics.iterations', 'iterations', 'iters', 'iter_count', 'solver.iterations']);
    const finalRes = pickField(data, ['metrics.final_residual', 'final_residual', 'residual', 'abs_residual', 'residuals.final']);
    const relRes = pickField(data, ['metrics.relative_residual', 'relative_residual', 'rel_residual', 'residuals.relative']);

    const tAssembly = pickField(data, ['timings_ms.assembly_time_ms', 'assembly_time_ms', 'assembly_ms', 'timings.assembly_ms', 'timings.assembly']);
    const tConversion = pickField(data, ['timings_ms.conversion_time_ms', 'conversion_time_ms', 'conversion_ms', 'timings.conversion_ms', 'timings.conversion']);
    const tOrdering = pickField(data, ['timings_ms.setup_time_ms', 'ordering_time_ms', 'ordering_ms', 'timings.ordering_ms', 'timings.ordering']);
    const tFactor = pickField(data, ['timings_ms.setup_time_ms', 'factorization_time_ms', 'factorization_ms', 'timings.factorization_ms', 'timings.factorization']);
    const tSolve = pickField(data, ['timings_ms.solve_time_ms', 'solve_time_ms', 'solve_ms', 'timings.solve_ms', 'timings.solve', 'elapsed_ms']);

    const memBytes = pickField(data, ['memory_bytes', 'mem_bytes', 'memory.total_bytes', 'memory.bytes']);
    const memMb = pickField(data, ['memory_mb', 'mem_mb', 'memory.total_mb', 'memory_used_mb']);
    const memStr = pickField(data, ['memory_info', 'memory_summary', 'memory.description']);

    let memoryDisplay = null;
    if (typeof memStr === 'string' && memStr.trim()) {
      memoryDisplay = memStr.trim();
    } else if (typeof memMb === 'number' && isFinite(memMb)) {
      memoryDisplay = `${memMb.toFixed(2)} MB`;
    } else if (typeof memBytes === 'number' && isFinite(memBytes)) {
      memoryDisplay = memBytes >= 1048576 ? `${(memBytes / 1048576).toFixed(2)} MB` : `${(memBytes / 1024).toFixed(1)} KB`;
    }

    const cubeErr = pickField(data, ['metrics.max_analytical_error_degC', 'analytical_error', 'l2_error', 'max_error', 'cube_analytical_error', 'verification.analytical_error']);
    const energyBal = pickField(data, ['metrics.energy_balance_rel_error', 'energy_balance', 'heat_balance_error', 'energy_balance_error', 'finned_sink_energy_balance', 'verification.energy_balance']);

    const minT = pickField(data, ['thermal_field.min_temp_degC', 'min_temperature', 'min_temp', 'temperature_min', 'stats.min_temperature']);
    const maxT = pickField(data, ['thermal_field.max_temp_degC', 'max_temperature', 'max_temp', 'temperature_max', 'stats.max_temperature']);
    const avgT = pickField(data, ['thermal_field.avg_temp_degC', 'avg_temperature', 'mean_temperature', 'avg_temp', 'stats.avg_temperature']);

    return {
      status, activeCells, nnz, iterations, finalRes, relRes,
      tAssembly, tConversion, tOrdering, tFactor, tSolve,
      memoryDisplay, cubeErr, energyBal, minT, maxT, avgT
    };
  }

  function clearResultsPanels(reasonText = 'UNAVAILABLE') {
    const summaryBadge = containerEl?.querySelector('#t3d-summary-badge');
    const summaryGrid = containerEl?.querySelector('#t3d-summary-grid');
    if (summaryBadge) {
      summaryBadge.textContent = reasonText;
      summaryBadge.style.color = 'var(--muted)';
    }
    if (summaryGrid) {
      summaryGrid.innerHTML = `
        <div class="t3d-kv-cell"><span class="t3d-kv-label">SOLVER STATUS</span><span class="t3d-kv-val">—</span></div>
        <div class="t3d-kv-cell"><span class="t3d-kv-label">ACTIVE CELLS</span><span class="t3d-kv-val">—</span></div>
        <div class="t3d-kv-cell"><span class="t3d-kv-label">ITERATIONS</span><span class="t3d-kv-val">—</span></div>
        <div class="t3d-kv-cell"><span class="t3d-kv-label">SOLVE TIME</span><span class="t3d-kv-val">—</span></div>
        <div class="t3d-kv-cell"><span class="t3d-kv-label">FINAL RESIDUAL</span><span class="t3d-kv-val">—</span></div>
        <div class="t3d-kv-cell"><span class="t3d-kv-label">VERIFICATION</span><span class="t3d-kv-val">—</span></div>
      `;
    }

    const resTag = containerEl?.querySelector('#t3d-results-status-tag');
    const resBody = containerEl?.querySelector('#t3d-full-metrics-body');
    if (resTag) {
      resTag.textContent = reasonText;
      resTag.style.color = 'var(--muted)';
    }
    if (resBody) {
      resBody.style.padding = '12px';
      resBody.innerHTML = backendOnline
        ? 'Run a live simulation against the C++17 engine to populate numerical solver results.'
        : 'Numerical solver results unavailable while the C++17 backend is offline.';
    }
  }

  function renderResultsPanels(data) {
    const m = extractMetricsFromResponse(data);
    const statusUpper = m.status ? String(m.status).toUpperCase() : 'UNKNOWN';
    const isOk = (statusUpper === 'SUCCESS' || statusUpper === 'CONVERGED');
    const statusCls = isOk ? 'is-ok' : 'is-vermilion';

    // Update Summary Ledger on Simulate tab (only show returned fields)
    const summaryBadge = containerEl?.querySelector('#t3d-summary-badge');
    const summaryGrid = containerEl?.querySelector('#t3d-summary-grid');
    if (summaryBadge) {
      summaryBadge.textContent = statusUpper;
      summaryBadge.style.color = isOk ? '#2E7D32' : 'var(--vermilion)';
    }

    if (summaryGrid) {
      const summaryItems = [['SOLVER STATUS', statusUpper, statusCls]];
      if (m.activeCells !== undefined) summaryItems.push(['ACTIVE CELLS', Number(m.activeCells).toLocaleString(), '']);
      if (m.iterations !== undefined) summaryItems.push(['ITERATIONS', Number(m.iterations).toLocaleString(), '']);
      if (m.tSolve !== undefined) summaryItems.push(['SOLVE TIME', formatMs(m.tSolve), 'is-cobalt']);
      if (m.finalRes !== undefined) summaryItems.push(['FINAL RESIDUAL', formatSci(m.finalRes, 3), '']);
      if (m.cubeErr !== undefined) {
        summaryItems.push(['ANALYTICAL ERROR', typeof m.cubeErr === 'number' ? formatSci(m.cubeErr, 3) : String(m.cubeErr), '']);
      } else if (m.energyBal !== undefined) {
        summaryItems.push(['ENERGY BALANCE', typeof m.energyBal === 'number' ? formatSci(m.energyBal, 3) : String(m.energyBal), '']);
      }

      summaryGrid.innerHTML = summaryItems.map(([lbl, val, cls]) => `
        <div class="t3d-kv-cell">
          <span class="t3d-kv-label">${escapeHtml(lbl)}</span>
          <span class="t3d-kv-val ${cls}">${escapeHtml(String(val))}</span>
        </div>
      `).join('');
    }

    // Update Full Results Tab: STRICTLY render only fields present in the backend response
    const resTag = containerEl?.querySelector('#t3d-results-status-tag');
    const resBody = containerEl?.querySelector('#t3d-full-metrics-body');
    if (resTag) {
      resTag.textContent = statusUpper;
      resTag.style.color = isOk ? '#2E7D32' : 'var(--vermilion)';
    }

    if (resBody) {
      const rows = [['Solver Status', statusUpper, statusCls]];
      if (m.activeCells !== undefined) rows.push(['Active Cells', Number(m.activeCells).toLocaleString(), '']);
      if (m.nnz !== undefined) rows.push(['Matrix Non-Zeros (NNZ)', Number(m.nnz).toLocaleString(), '']);
      if (m.iterations !== undefined) rows.push(['Iterations', Number(m.iterations).toLocaleString(), '']);
      if (m.finalRes !== undefined) rows.push(['Final Residual', formatSci(m.finalRes, 4), '']);
      if (m.relRes !== undefined) rows.push(['Relative Residual', formatSci(m.relRes, 4), '']);
      if (m.tAssembly !== undefined) rows.push(['Assembly Time', formatMs(m.tAssembly), '']);
      if (m.tConversion !== undefined) rows.push(['Conversion Time', formatMs(m.tConversion), '']);
      if (m.tOrdering !== undefined) rows.push(['Ordering Time', formatMs(m.tOrdering), '']);
      if (m.tFactor !== undefined) rows.push(['Factorization Time', formatMs(m.tFactor), '']);
      if (m.tSolve !== undefined) rows.push(['Solve Time', formatMs(m.tSolve), 'is-cobalt']);
      if (m.memoryDisplay !== null) rows.push(['Memory Usage', m.memoryDisplay, '']);
      if (m.minT !== undefined) rows.push(['Min Temperature', formatTemp(m.minT), 'is-cobalt']);
      if (m.maxT !== undefined) rows.push(['Max Temperature', formatTemp(m.maxT), 'is-vermilion']);
      if (m.avgT !== undefined) rows.push(['Average Temperature', formatTemp(m.avgT), '']);
      if (m.cubeErr !== undefined) {
        rows.push(['Cube Analytical Error', typeof m.cubeErr === 'number' ? formatSci(m.cubeErr, 4) : String(m.cubeErr), '']);
      }
      if (m.energyBal !== undefined) {
        rows.push(['Heat-Sink Energy Balance', typeof m.energyBal === 'number' ? formatSci(m.energyBal, 4) : String(m.energyBal), '']);
      }

      resBody.style.padding = '0';
      resBody.innerHTML = `
        <div class="t3d-kv-grid">
          ${rows.map(([lbl, val, cls]) => `
            <div class="t3d-kv-cell">
              <span class="t3d-kv-label">${escapeHtml(lbl)}</span>
              <span class="t3d-kv-val ${cls}">${escapeHtml(String(val))}</span>
            </div>
          `).join('')}
        </div>
      `;
    }
  }

  // =========================================================================
  // BACKEND DISCOVERY & SOLVE EXECUTION
  // =========================================================================

  function formatOfflineDiagnostic(errMsg) {
    const base = API.getThermal3dBaseUrl() || window.location.origin;
    const isHttpsMixed = (window.location.protocol === 'https:') && /^http:\/\/(127\.0\.0\.1|localhost)/i.test(base);
    if (isHttpsMixed) {
      return `Unable to reach ${base}/api/thermal3d/health (${errMsg}). Because this frontend is loaded over HTTPS while the local C++ engine listens on HTTP, browser mixed-content or loopback policies may block the request. Verify local CORS / proxy rules or update the endpoint under API CONTRACT.`;
    }
    return `Unable to reach ${base}/api/thermal3d/health (${errMsg}). The local C++17 engine is unreachable; RUN SIMULATION is disabled and no fabricated temperature field will be rendered.`;
  }

  async function probeBackend() {
    backendChecking = true;
    updateTopBackendChip();

    const discoveryPre = containerEl?.querySelector('#t3d-contract-discovery');
    const trace = {};

    try {
      const healthRes = await API.thermal3dHealth();
      contractHealth = healthRes.data;
      trace.health = healthRes.data;
      backendOnline = true;

      // Also fetch models & config
      try {
        const modelsRes = await API.thermal3dModels();
        contractModels = modelsRes.data;
        trace.models = modelsRes.data;
        applyDiscoveredModels(modelsRes.data);
      } catch (e) {
        trace.models_error = e.message;
      }

      try {
        const cfgRes = await API.thermal3dConfig();
        contractConfig = cfgRes.data;
        trace.config = cfgRes.data;
        applyDiscoveredConfig(cfgRes.data);
      } catch (e) {
        trace.config_error = e.message;
      }

      backendChecking = false;
      updateTopBackendChip();
      UI.hideErrorBanner(containerEl);

      if (screenMode === 'workspace' && !currentParsedField && !isSolving) {
        statusLine?.setDone('C++17 BACKEND ONLINE');
        setStageNotice(
          'info',
          'STRUCTURAL REFERENCE — NO LIVE TEMPERATURE DATA',
          `Connected to C++17 engine (${API.getThermal3dBaseUrl() || 'local origin'}). Select resolution and solver, then click RUN SIMULATION to solve and plot real nodal temperatures.`,
          false
        );
      }
    } catch (err) {
      backendOnline = false;
      backendChecking = false;
      updateTopBackendChip();
      trace.health_error = err.message;

      if (screenMode === 'workspace') {
        if (!isSolving) {
          statusLine?.setError('BACKEND OFFLINE');
        }
        if (!currentParsedField) {
          clearResultsPanels('OFFLINE');
          setStageNotice(
            'offline',
            'BACKEND OFFLINE · STRUCTURAL REFERENCE — NO LIVE TEMPERATURE DATA',
            formatOfflineDiagnostic(err.message),
            true
          );
        }
      }
    }

    if (discoveryPre) {
      discoveryPre.textContent = JSON.stringify(trace, null, 2);
    }
  }

  function applyDiscoveredModels(modelsData) {
    // Support array of strings or { models: [...] } without overriding user's selected dedicated workspace model
    const list = Array.isArray(modelsData) ? modelsData : (modelsData && Array.isArray(modelsData.models) ? modelsData.models : null);
    if (!list || list.length === 0) return;

    if (screenMode === 'workspace') {
      updateViewportHeader();
      if (!currentParsedField) rebuildGhostWireframe();
    }
  }

  function renderOptionalSolverSettings() {
    const box = containerEl?.querySelector('#t3d-optional-settings');
    if (!box) return;

    const opts = [];
    const optMap = supportedOptionalParams || {};
    if (optMap.tolerance !== undefined) {
      opts.push(['tolerance', 'Tolerance', String(optMap.tolerance)]);
    }
    if (optMap.max_iterations !== undefined && selectedSolver !== 'PROFILE_CHOLESKY') {
      opts.push(['max_iterations', 'Max Iterations', String(optMap.max_iterations)]);
    }
    if (optMap.omega !== undefined && selectedSolver === 'SOR') {
      opts.push(['omega', 'SOR Relaxation (ω)', String(optMap.omega)]);
    } else if (optMap.relaxation_factor !== undefined && selectedSolver === 'SOR') {
      opts.push(['relaxation_factor', 'SOR Relaxation (ω)', String(optMap.relaxation_factor)]);
    }

    if (opts.length === 0) {
      box.style.display = 'none';
      box.innerHTML = '';
      return;
    }

    box.style.display = 'flex';
    box.innerHTML = `
      <div class="control-label">SUPPORTED SOLVER PARAMETERS</div>
      <div class="t3d-options-grid">
        ${opts.map(([key, label, val]) => `
          <div class="control-group" style="gap: 4px;">
            <label class="control-label" style="font-size: 9px; color: var(--muted);" for="opt-${key}">${escapeHtml(label)}</label>
            <input type="number" step="any" class="control-input t3d-opt-input" id="opt-${key}" data-opt-key="${escapeHtml(key)}" value="${escapeHtml(val)}" />
          </div>
        `).join('')}
      </div>
    `;
  }

  function applyConfigControlsToDom() {
    const resGroup = containerEl?.querySelector('#t3d-resolution-group');
    if (resGroup && discoveredResolutions.length > 0) {
      if (!discoveredResolutions.includes(selectedResolution)) selectedResolution = discoveredResolutions[0];
      resGroup.innerHTML = discoveredResolutions.map(n => `
        <button class="t3d-seg-btn ${n === selectedResolution ? 'is-active' : ''}" data-res="${n}">N=${n}</button>
      `).join('');
      resGroup.querySelectorAll('.t3d-seg-btn').forEach(btn => {
        btn.addEventListener('click', () => selectResolution(parseInt(btn.dataset.res, 10)));
      });
    }

    const solvGroup = containerEl?.querySelector('#t3d-solver-group');
    if (solvGroup && discoveredSolvers.length > 0) {
      if (!discoveredSolvers.includes(selectedSolver)) selectedSolver = discoveredSolvers[0];
      solvGroup.innerHTML = discoveredSolvers.map(s => `
        <button class="t3d-seg-btn ${s === selectedSolver ? 'is-active' : ''}" data-solver="${escapeHtml(s)}">${escapeHtml(s)}</button>
      `).join('');
      solvGroup.querySelectorAll('.t3d-seg-btn').forEach(btn => {
        btn.addEventListener('click', () => selectSolver(btn.dataset.solver));
      });
    }

    renderOptionalSolverSettings();
    updateViewportHeader();
  }

  function applyDiscoveredConfig(cfg) {
    if (!cfg || typeof cfg !== 'object') return;
    if (Array.isArray(cfg.resolutions) && cfg.resolutions.length > 0) {
      const resList = cfg.resolutions.map(Number).filter(n => isFinite(n) && n > 0);
      if (resList.length > 0) discoveredResolutions = resList;
    }

    if (Array.isArray(cfg.solvers) && cfg.solvers.length > 0) {
      const solvers = cfg.solvers.map(s => typeof s === 'string' ? s : (s.id || s.name)).filter(Boolean);
      if (solvers.length > 0) discoveredSolvers = solvers;
    }

    // Only expose optional solver settings if explicitly supported by /api/thermal3d/config
    const discoveredOpts = {};
    const optSource = cfg.solver_options || cfg.defaults || cfg;
    if (typeof optSource.tolerance === 'number' && isFinite(optSource.tolerance)) {
      discoveredOpts.tolerance = optSource.tolerance;
    }
    if (typeof optSource.max_iterations === 'number' && isFinite(optSource.max_iterations)) {
      discoveredOpts.max_iterations = optSource.max_iterations;
    }
    if (typeof optSource.omega === 'number' && isFinite(optSource.omega)) {
      discoveredOpts.omega = optSource.omega;
    } else if (typeof optSource.relaxation_factor === 'number' && isFinite(optSource.relaxation_factor)) {
      discoveredOpts.relaxation_factor = optSource.relaxation_factor;
    }
    if (Array.isArray(cfg.solvers)) {
      for (const s of cfg.solvers) {
        if (typeof s === 'object' && s) {
          if (typeof s.default_tolerance === 'number' && isFinite(s.default_tolerance)) {
            discoveredOpts.tolerance = s.default_tolerance;
          }
          if (typeof s.default_max_iterations === 'number' && isFinite(s.default_max_iterations)) {
            discoveredOpts.max_iterations = s.default_max_iterations;
          }
          if (typeof s.default_omega === 'number' && isFinite(s.default_omega)) {
            discoveredOpts.omega = s.default_omega;
          }
        }
      }
    }
    if (discoveredOpts.tolerance === undefined) {
      discoveredOpts.tolerance = 1e-8;
    }
    supportedOptionalParams = discoveredOpts;

    if (screenMode === 'workspace') {
      applyConfigControlsToDom();
    }
  }

  async function runSimulation() {
    if (isSolving || !backendOnline) return;

    if (solveAbortController) solveAbortController.abort();
    solveAbortController = new AbortController();

    isSolving = true;
    UI.hideErrorBanner(containerEl);
    statusLine?.setRunning(`SOLVING ${selectedGeometry} N=${selectedResolution}`);

    const abortBtn = containerEl?.querySelector('#btn-t3d-abort');
    syncActionButtons();
    if (abortBtn) abortBtn.style.display = 'inline-flex';

    setStageNotice(
      'info',
      `EXECUTING ${selectedSolver} SOLVER ON C++17 BACKEND...`,
      `Dispatching finite-volume solve for ${selectedGeometry} (N=${selectedResolution}).`,
      false
    );

    const solverOptions = {
      tolerance: 1e-8,
      max_iterations: (selectedSolver === 'SOR') ? 10000 : 5000
    };
    if (selectedSolver === 'SOR') {
      solverOptions.omega = 1.0;
    }

    // Include supported optional solver settings from inputs
    containerEl?.querySelectorAll('.t3d-opt-input').forEach(inp => {
      const val = parseFloat(inp.value);
      if (inp.dataset.optKey && isFinite(val)) {
        if (inp.dataset.optKey === 'tolerance') solverOptions.tolerance = val;
        else if (inp.dataset.optKey === 'max_iterations') solverOptions.max_iterations = Math.round(val);
        else if (inp.dataset.optKey === 'omega' || inp.dataset.optKey === 'relaxation_factor') solverOptions.omega = val;
      }
    });

    const payload = {
      model: selectedGeometry,
      resolution: selectedResolution,
      solver: selectedSolver,
      solver_options: solverOptions
    };

    lastRequestPayload = payload;
    const contractPre = containerEl?.querySelector('#t3d-contract-solve');

    try {
      const response = await API.thermal3dSolve(payload, solveAbortController.signal);
      backendOnline = true;
      updateTopBackendChip();

      const data = response.data;
      lastRawResponse = data;
      lastSolveResult = data;

      if (contractPre) {
        // Summarize huge cell arrays in trace preview so pre block stays responsive
        const traceCopy = Object.assign({}, data);
        if (Array.isArray(traceCopy.cells) && traceCopy.cells.length > 8) {
          traceCopy.cells = [...traceCopy.cells.slice(0, 4), `... (${traceCopy.cells.length} total cells)`];
        }
        if (Array.isArray(traceCopy.temperatures) && traceCopy.temperatures.length > 12) {
          traceCopy.temperatures = [...traceCopy.temperatures.slice(0, 6), `... (${traceCopy.temperatures.length} total)`];
        }
        contractPre.textContent = JSON.stringify({ request: payload, response: traceCopy }, null, 2);
      }

      renderResultsPanels(data);

      const extracted = extractAuthoritativeField(data);
      if (extracted.ok) {
        lastSolvedKey = `${selectedGeometry}|${selectedResolution}|${selectedSolver}`;
        renderTemperatureField(extracted);
        setStageNotice(null);
        statusLine?.setDone(`${extracted.status} · ${extracted.count.toLocaleString()} CELLS`);
        UI.showToast(`Solved ${selectedGeometry} (N=${selectedResolution}) with ${selectedSolver}`);
      } else {
        // MAX_ITERATIONS_REACHED, DIVERGED_RESIDUAL, DIVERGED_NUMERICAL, or INVALID_RESPONSE -> DO NOT plot!
        clearTemperatureFieldMesh();
        rebuildGhostWireframe();
        statusLine?.setError(extracted.reason);
        setStageNotice(
          'error',
          `SOLVER DIAGNOSTIC: ${extracted.reason} — NO TEMPERATURE FIELD PLOTTED`,
          `${extracted.detail} Only SUCCESS and CONVERGED solutions with valid cell coordinates render as thermal visualizations.`,
          false
        );
        UI.showErrorBanner(
          containerEl,
          `${extracted.reason}: ${extracted.detail}`,
          'Inspect returned diagnostics in RESULTS or run Profile Cholesky.'
        );
      }
    } catch (err) {
      if (err.name === 'AbortError') {
        statusLine?.setIdle('SOLVE ABORTED');
        setStageNotice('info', 'STRUCTURAL REFERENCE — SOLVE ABORTED', 'Simulation request was cancelled before completion.', false);
      } else {
        clearTemperatureFieldMesh();
        rebuildGhostWireframe();
        if (err.code === 'OFFLINE') {
          backendOnline = false;
          updateTopBackendChip();
          statusLine?.setError('BACKEND OFFLINE');
          setStageNotice(
            'offline',
            'BACKEND OFFLINE · STRUCTURAL REFERENCE — NO LIVE TEMPERATURE DATA',
            formatOfflineDiagnostic(err.message),
            true
          );
          UI.showErrorBanner(containerEl, err.message, 'Ensure the C++17 REST engine is listening on 127.0.0.1:8080.');
        } else {
          statusLine?.setError(err.code || 'SOLVE ERROR');
          setStageNotice(
            'error',
            err.code === 'INVALID_RESPONSE' ? 'INVALID BACKEND RESPONSE' : 'BACKEND SOLVE ERROR',
            err.message,
            false
          );
          UI.showErrorBanner(containerEl, err.message, 'Check API CONTRACT tab for raw payload details.');
        }

        if (contractPre) {
          contractPre.textContent = JSON.stringify({
            request: payload,
            error: err.message,
            code: err.code || 'ERROR',
            raw: err.rawText ? err.rawText.slice(0, 800) : undefined
          }, null, 2);
        }
      }
    } finally {
      isSolving = false;
      syncActionButtons();
      if (abortBtn) abortBtn.style.display = 'none';
    }
  }

  // =========================================================================
  // SOLVER COMPARISON (REAL BACKEND RUNS FOR CHOLESKY, PCG, SOR)
  // =========================================================================

  function initCompareChart() {
    if (typeof Chart === 'undefined') return;
    const canvas = containerEl?.querySelector('#t3d-compare-chart');
    if (!canvas) return;

    compareChart = new Chart(canvas, {
      type: 'bar',
      data: {
        labels: ['PROFILE_CHOLESKY', 'PCG', 'SOR'],
        datasets: [
          {
            label: 'Solve Time (ms)',
            data: [0, 0, 0],
            backgroundColor: ['#1F3FB0', '#141414', '#E0452B'],
            borderWidth: 1,
            borderColor: '#141414'
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        plugins: {
          legend: { display: false },
          tooltip: {
            callbacks: {
              label: (ctx) => `Solve time: ${ctx.parsed.y.toFixed(2)} ms`
            }
          }
        },
        scales: {
          x: {
            ticks: { font: { family: "'Inter Tight', sans-serif", size: 9.5, weight: '600' }, color: '#141414' },
            grid: { display: false }
          },
          y: {
            beginAtZero: true,
            title: { display: true, text: 'ms', font: { family: "'Inter Tight', sans-serif", size: 9.5 }, color: '#6D6A63' },
            ticks: { font: { family: "monospace", size: 9 }, color: '#6D6A63' },
            grid: { color: 'rgba(20, 20, 20, 0.08)' }
          }
        }
      }
    });
  }

  function resetComparisonDisplay() {
    comparisonResults = {};
    const tbody = containerEl?.querySelector('#t3d-compare-tbody');
    if (tbody) {
      tbody.innerHTML = `<tr><td colspan="6" style="color: var(--muted); text-align: center; padding: 14px;">Comparison unavailable — run live benchmark for ${escapeHtml(selectedGeometry)} (N=${selectedResolution}).</td></tr>`;
    }
    if (compareChart) {
      compareChart.data.datasets[0].data = [0, 0, 0];
      compareChart.update('none');
    }
  }

  async function runSolverComparison() {
    if (isComparing || !backendOnline) return;
    isComparing = true;

    if (compareAbortController) compareAbortController.abort();
    compareAbortController = new AbortController();

    const stopBtn = containerEl?.querySelector('#btn-t3d-compare-cancel');
    const tbody = containerEl?.querySelector('#t3d-compare-tbody');
    syncActionButtons();
    if (stopBtn) stopBtn.style.display = 'inline-flex';

    const solvers = ['PROFILE_CHOLESKY', 'PCG', 'SOR'];
    comparisonResults = {};
    if (tbody) {
      tbody.innerHTML = solvers.map(s => `
        <tr data-row-solver="${s}">
          <td>${s}</td>
          <td style="color:var(--cobalt);">QUEUED</td>
          <td style="text-align:right;">—</td>
          <td style="text-align:right;">—</td>
          <td style="text-align:right;">—</td>
          <td style="text-align:right;">—</td>
        </tr>
      `).join('');
    }

    for (const solver of solvers) {
      if (compareAbortController.signal.aborted) break;
      const row = tbody?.querySelector(`tr[data-row-solver="${solver}"]`);
      if (row) row.children[1].textContent = 'RUNNING...';

      try {
        const compareOptions = {
          tolerance: 1e-8,
          max_iterations: (solver === 'SOR') ? 10000 : 5000
        };
        if (solver === 'SOR') {
          compareOptions.omega = 1.0;
        }
        const res = await API.thermal3dSolve({
          model: selectedGeometry,
          resolution: selectedResolution,
          solver: solver,
          solver_options: compareOptions
        }, compareAbortController.signal);

        backendOnline = true;
        updateTopBackendChip();

        const m = extractMetricsFromResponse(res.data);
        const stUpper = m.status ? String(m.status).toUpperCase() : 'UNKNOWN';
        const isOk = (stUpper === 'SUCCESS' || stUpper === 'CONVERGED');

        comparisonResults[solver] = { status: stUpper, metrics: m, ok: isOk };

        if (row) {
          row.children[1].textContent = stUpper;
          row.children[1].style.color = isOk ? '#2E7D32' : 'var(--vermilion)';
          row.children[2].textContent = m.iterations !== undefined ? String(m.iterations) : '—';
          row.children[3].textContent = m.tSolve !== undefined ? m.tSolve.toFixed(2) : '—';
          row.children[4].textContent = formatSci(m.finalRes, 2);
          row.children[5].textContent = m.memoryDisplay || '—';
        }
      } catch (err) {
        if (err.name === 'AbortError') break;
        if (err.code === 'OFFLINE') {
          backendOnline = false;
          updateTopBackendChip();
        }
        comparisonResults[solver] = { status: err.code || 'ERROR', ok: false };
        if (row) {
          row.children[1].textContent = err.code || 'FAILED';
          row.children[1].style.color = 'var(--vermilion)';
        }
      }
    }

    if (compareChart) {
      compareChart.data.datasets[0].data = solvers.map(s => {
        const r = comparisonResults[s];
        return (r && r.metrics && typeof r.metrics.tSolve === 'number') ? r.metrics.tSolve : 0;
      });
      compareChart.update('none');
    }

    isComparing = false;
    syncActionButtons();
    if (stopBtn) stopBtn.style.display = 'none';
  }

  // =========================================================================
  // INTERACTION BINDINGS & RAYCAST CELL PROBE
  // =========================================================================

  function showNeutralReferenceNotice() {
    if (structuralNoticeDismissed) return;
    if (backendOnline) {
      setStageNotice(
        'info',
        'STRUCTURAL REFERENCE',
        `Showing ${selectedGeometry} structural reference (N=${selectedResolution}). Click RUN SIMULATION to compute real temperatures.`,
        false
      );
    } else {
      setStageNotice(
        'offline',
        'BACKEND OFFLINE',
        `Showing ${selectedGeometry} structural reference while C++17 backend is unreachable.`,
        true
      );
    }
  }

  function selectResolution(res) {
    if (selectedResolution === res) return;
    selectedResolution = res;
    containerEl?.querySelectorAll('#t3d-resolution-group .t3d-seg-btn').forEach(btn => {
      btn.classList.toggle('is-active', parseInt(btn.dataset.res, 10) === res);
    });
    // Never mix results across resolutions
    clearTemperatureFieldMesh();
    rebuildGhostWireframe();
    clearResultsPanels(backendOnline ? 'AWAITING SOLVE' : 'OFFLINE');
    resetComparisonDisplay();
    showNeutralReferenceNotice();
    updateViewportHeader();
  }

  function selectSolver(solver) {
    selectedSolver = solver;
    containerEl?.querySelectorAll('#t3d-solver-group .t3d-seg-btn').forEach(btn => {
      btn.classList.toggle('is-active', btn.dataset.solver === solver);
    });
    renderOptionalSolverSettings();
    updateViewportHeader();
  }

  function switchTab(tabId) {
    containerEl?.querySelectorAll('.t3d-tab-btn').forEach(btn => {
      const act = btn.dataset.tab === tabId;
      btn.classList.toggle('is-active', act);
      btn.setAttribute('aria-selected', act ? 'true' : 'false');
    });
    containerEl?.querySelectorAll('.t3d-tab-panel').forEach(panel => {
      panel.classList.toggle('is-active', panel.id === 't3d-panel-' + tabId);
    });
    if (tabId === 'compare' && compareChart) {
      compareChart.resize();
    }
  }

  function handlePointerMove(e) {
    if (!instancedCells || !currentParsedField || !raycaster || !camera || !renderer) return;
    const now = performance.now();
    if (now - lastRaycastTime < 55) return;
    lastRaycastTime = now;

    const rect = renderer.domElement.getBoundingClientRect();
    mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

    raycaster.setFromCamera(mouse, camera);
    const hits = raycaster.intersectObject(instancedCells);
    const readout = containerEl?.querySelector('#t3d-cell-readout');

    if (hits.length > 0 && hits[0].instanceId !== undefined) {
      let pickedHit = null;
      const cutX = sliceX / 100.0 + 1e-4;
      const cutY = sliceY / 100.0 + 1e-4;
      const cutZ = sliceZ / 100.0 + 1e-4;
      for (let h = 0; h < hits.length; h++) {
        const idx = hits[h].instanceId;
        if (idx === undefined || !instanceTransforms) continue;
        if (instanceTransforms.nx[idx] > cutX) continue;
        if (instanceTransforms.ny[idx] > cutY) continue;
        if (instanceTransforms.nz[idx] > cutZ) continue;
        pickedHit = idx;
        break;
      }

      if (pickedHit !== null) {
        const f = currentParsedField;
        if (readout && pickedHit >= 0 && pickedHit < f.count) {
          readout.textContent = `CELL #${pickedHit} · (${f.xs[pickedHit].toFixed(4)}, ${f.ys[pickedHit].toFixed(4)}, ${f.zs[pickedHit].toFixed(4)}) · T = ${f.temps[pickedHit].toFixed(2)} °C`;
        }
        if (hoverOutlineMesh && instanceTransforms) {
          hoverOutlineMesh.position.set(
            instanceTransforms.px[pickedHit],
            instanceTransforms.py[pickedHit],
            instanceTransforms.pz[pickedHit]
          );
          hoverOutlineMesh.scale.set(
            instanceTransforms.sx[pickedHit] * voxelScale,
            instanceTransforms.sy[pickedHit] * voxelScale,
            instanceTransforms.sz[pickedHit] * voxelScale
          );
          hoverOutlineMesh.visible = true;
        }
        return;
      }
    }

    if (hoverOutlineMesh) hoverOutlineMesh.visible = false;
    if (readout) {
      readout.textContent = 'ORBIT: DRAG · ZOOM: SCROLL · HOVER CELL TO INSPECT';
    }
  }

  function teardownWorkspaceInstances() {
    if (solveAbortController) {
      solveAbortController.abort();
      solveAbortController = null;
    }
    if (compareAbortController) {
      compareAbortController.abort();
      compareAbortController = null;
    }
    if (resizeObserver) {
      resizeObserver.disconnect();
      resizeObserver = null;
    }
    if (rAFId) {
      cancelAnimationFrame(rAFId);
      rAFId = null;
    }
    if (compareChart) {
      compareChart.destroy();
      compareChart = null;
    }
    clearTemperatureFieldMesh();
    if (ghostWireframeGroup) clearGroup(ghostWireframeGroup);
    if (axisTriadGroup) clearGroup(axisTriadGroup);
    if (hoverOutlineMesh) {
      hoverOutlineMesh.geometry?.dispose();
      hoverOutlineMesh.material?.dispose();
      hoverOutlineMesh = null;
    }
    if (sliceHelperMesh) {
      sliceHelperMesh.geometry?.dispose();
      sliceHelperMesh.material?.dispose();
      sliceHelperMesh = null;
    }
    clippingPlane = null;
    if (controls) {
      controls.dispose();
      controls = null;
    }
    if (renderer) {
      renderer.dispose();
      if (renderer.domElement && renderer.domElement.parentNode) {
        renderer.domElement.parentNode.removeChild(renderer.domElement);
      }
      renderer = null;
    }
    scene = null;
    camera = null;
    raycaster = null;
    mouse = null;
    statusLine = null;
    isSolving = false;
    isComparing = false;
  }

  function bindSelectionEvents() {
    containerEl?.querySelector('#btn-select-cube')?.addEventListener('click', () => {
      enterModelWorkspace('CUBE');
    });
    containerEl?.querySelector('#btn-select-sink')?.addEventListener('click', () => {
      enterModelWorkspace('FINNED_SINK');
    });
    containerEl?.querySelector('#btn-t3d-select-retry')?.addEventListener('click', probeBackend);
  }

  function returnToModelSelection() {
    if (!containerEl) return;
    teardownWorkspaceInstances();
    screenMode = 'selection';

    const dialog = document.querySelector('#detail-dialog');
    if (dialog) dialog.classList.add('is-selection');

    containerEl.innerHTML = renderSelectionHTML();
    bindSelectionEvents();
    updateTopBackendChip();
  }

  function enterModelWorkspace(modelId) {
    if (!containerEl) return;
    teardownWorkspaceInstances();

    screenMode = 'workspace';
    selectedGeometry = (modelId === 'CUBE') ? 'CUBE' : 'FINNED_SINK';
    sliceX = 100;
    sliceY = 100;
    sliceZ = 100;
    voxelScale = 0.95;
    sliceAxis = 'off';
    sliceFraction = 1.0;
    cellScaleRatio = 0.95;
    lastSolveResult = null;
    lastRawResponse = null;
    lastRequestPayload = null;

    const dialog = document.querySelector('#detail-dialog');
    if (dialog) dialog.classList.remove('is-selection');

    containerEl.innerHTML = renderHTML();

    const edgesBtn = containerEl.querySelector('#btn-t3d-edges');
    const gridBtn = containerEl.querySelector('#btn-t3d-grid');
    if (edgesBtn) edgesBtn.classList.toggle('is-active', showEdges);
    if (gridBtn) gridBtn.classList.toggle('is-active', showGrid);

    const statusContainer = containerEl.querySelector('#t3d-status-container');
    statusLine = UI.createStatusLine(statusContainer);

    const vp = containerEl.querySelector('#t3d-three-container');
    initThree(vp);
    initCompareChart();
    bindEvents();
    syncSliceControlsUI();
    applyConfigControlsToDom();
    updateTopBackendChip();
    updateViewportHeader();
    clearResultsPanels(backendOnline ? 'AWAITING SOLVE' : 'OFFLINE');
    resetComparisonDisplay();
    showNeutralReferenceNotice();

    // Update contract discovery trace if already fetched
    const discoveryPre = containerEl.querySelector('#t3d-contract-discovery');
    if (discoveryPre && (contractHealth || contractModels || contractConfig)) {
      discoveryPre.textContent = JSON.stringify({
        health: contractHealth,
        models: contractModels,
        config: contractConfig
      }, null, 2);
    }

    if (backendOnline) {
      statusLine?.setDone('C++17 BACKEND ONLINE');
    } else if (!backendChecking) {
      statusLine?.setError('BACKEND OFFLINE');
    }

    // Play Ink Reveal overlay across the right 3D viewport stage
    const vpWrap = containerEl.querySelector('.detail-right-col');
    if (vpWrap && window.UI && UI.createInkReveal) {
      try { UI.createInkReveal(vpWrap); } catch (e) {}
    }

    if (typeof ResizeObserver !== 'undefined' && vp) {
      resizeObserver = new ResizeObserver(() => handleResize());
      resizeObserver.observe(vp);
    }
  }

  function bindEvents() {
    // Tabs
    containerEl.querySelectorAll('.t3d-tab-btn').forEach(btn => {
      btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });

    // Change Model button inside Configuration panel
    containerEl.querySelector('#btn-t3d-panel-change-model')?.addEventListener('click', returnToModelSelection);

    // Controls
    containerEl.querySelectorAll('#t3d-resolution-group .t3d-seg-btn').forEach(btn => {
      btn.addEventListener('click', () => selectResolution(parseInt(btn.dataset.res, 10)));
    });
    containerEl.querySelectorAll('#t3d-solver-group .t3d-seg-btn').forEach(btn => {
      btn.addEventListener('click', () => selectSolver(btn.dataset.solver));
    });

    containerEl.querySelector('#btn-t3d-run')?.addEventListener('click', runSimulation);
    containerEl.querySelector('#btn-t3d-abort')?.addEventListener('click', () => {
      if (solveAbortController) solveAbortController.abort();
    });
    containerEl.querySelector('#btn-t3d-reconnect')?.addEventListener('click', probeBackend);
    containerEl.querySelector('#btn-t3d-notice-action')?.addEventListener('click', probeBackend);

    // Camera presets
    containerEl.querySelectorAll('.t3d-hud-btn[data-cam]').forEach(btn => {
      btn.addEventListener('click', () => applyCameraPreset(btn.dataset.cam));
    });

    // Edge & Grid toggles
    containerEl.querySelector('#btn-t3d-edges')?.addEventListener('click', (e) => {
      showEdges = !showEdges;
      e.currentTarget.classList.toggle('is-active', showEdges);
      if (thermalMaterialUniforms) {
        thermalMaterialUniforms.uEdgeStrength.value = showEdges ? 0.56 : 0.20;
        thermalMaterialUniforms.uEdgeWidth.value = showEdges ? 0.072 : 0.042;
      }
    });

    containerEl.querySelector('#btn-t3d-grid')?.addEventListener('click', (e) => {
      showGrid = !showGrid;
      e.currentTarget.classList.toggle('is-active', showGrid);
      if (gridHelper) gridHelper.visible = showGrid;
      if (axisTriadGroup) axisTriadGroup.visible = showGrid;
    });

    // Simultaneous 3-Axis Proportion Sliders (X-CUT, Y-CUT, Z-CUT) & Voxel Gap Controls
    const slX = containerEl.querySelector('#slider-slice-x');
    const valX = containerEl.querySelector('#val-slice-x');
    const slY = containerEl.querySelector('#slider-slice-y');
    const valY = containerEl.querySelector('#val-slice-y');
    const slZ = containerEl.querySelector('#slider-slice-z');
    const valZ = containerEl.querySelector('#val-slice-z');
    const slScale = containerEl.querySelector('#slider-voxel-scale');
    const valScale = containerEl.querySelector('#val-voxel-scale');
    const resetSliceBtn = containerEl.querySelector('#btn-t3d-reset-slice');

    if (slX && valX) {
      slX.addEventListener('input', (e) => {
        sliceX = Math.max(10, Math.min(100, parseInt(e.target.value, 10) || 100));
        valX.textContent = `${sliceX}%`;
        updateSliceVisuals();
      });
    }

    if (slY && valY) {
      slY.addEventListener('input', (e) => {
        sliceY = Math.max(10, Math.min(100, parseInt(e.target.value, 10) || 100));
        valY.textContent = `${sliceY}%`;
        updateSliceVisuals();
      });
    }

    if (slZ && valZ) {
      slZ.addEventListener('input', (e) => {
        sliceZ = Math.max(10, Math.min(100, parseInt(e.target.value, 10) || 100));
        valZ.textContent = `${sliceZ}%`;
        updateSliceVisuals();
      });
    }

    if (slScale && valScale) {
      slScale.addEventListener('input', (e) => {
        const pct = Math.max(70, Math.min(100, parseInt(e.target.value, 10) || 95));
        voxelScale = pct / 100.0;
        cellScaleRatio = voxelScale;
        valScale.textContent = `${pct}%`;
        updateSliceVisuals();
      });
    }

    if (resetSliceBtn) {
      resetSliceBtn.addEventListener('click', () => {
        sliceX = 100;
        sliceY = 100;
        sliceZ = 100;
        voxelScale = 0.95;
        cellScaleRatio = 0.95;
        syncSliceControlsUI();
        updateSliceVisuals();
      });
    }

    // Solver Comparison
    containerEl.querySelector('#btn-t3d-compare-run')?.addEventListener('click', runSolverComparison);
    containerEl.querySelector('#btn-t3d-compare-cancel')?.addEventListener('click', () => {
      if (compareAbortController) compareAbortController.abort();
    });

    // Contract Inspector URL controls
    containerEl.querySelector('#btn-t3d-save-url')?.addEventListener('click', () => {
      const inp = containerEl.querySelector('#inp-t3d-base-url');
      if (inp) {
        API.setThermal3dBaseUrl(inp.value);
        probeBackend();
      }
    });
    containerEl.querySelector('#btn-t3d-reset-url')?.addEventListener('click', () => {
      const def = API.resetThermal3dBaseUrl();
      const inp = containerEl.querySelector('#inp-t3d-base-url');
      if (inp) inp.value = def;
      probeBackend();
    });
    containerEl.querySelector('#btn-t3d-probe-all')?.addEventListener('click', probeBackend);

    if (renderer && renderer.domElement) {
      renderer.domElement.addEventListener('pointermove', handlePointerMove);
    }
  }

  function handleResize() {
    if (screenMode !== 'workspace') return;
    if (compareChart) compareChart.resize();
    if (!renderer || !camera) return;
    const vp = containerEl?.querySelector('#t3d-three-container');
    if (!vp) return;
    const w = vp.clientWidth;
    const h = vp.clientHeight;
    if (w > 0 && h > 0) {
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);
    }
  }

  return {
    resize: handleResize,

    mount: function(parentContainer) {
      containerEl = parentContainer;
      screenMode = 'selection';

      const dialog = document.querySelector('#detail-dialog');
      if (dialog) dialog.classList.add('is-selection');

      containerEl.innerHTML = renderSelectionHTML();
      bindSelectionEvents();
      updateTopBackendChip();

      // Immediately probe real C++ backend (/health, /models, /config)
      probeBackend();

      // Periodic reconnection check when offline
      healthTimer = setInterval(() => {
        if (!backendOnline && !isSolving && !backendChecking) {
          probeBackend();
        }
      }, 6500);
    },

    unmount: function() {
      if (healthTimer) {
        clearInterval(healthTimer);
        healthTimer = null;
      }
      teardownWorkspaceInstances();
      containerEl = null;
      screenMode = 'selection';
    }
  };
})();
