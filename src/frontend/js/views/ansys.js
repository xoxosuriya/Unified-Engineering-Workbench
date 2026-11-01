/**
 * Unified Engineering Workbench - ANSYS Thermal Point Cloud View (Module 2 / Task B)
 * Nodal temperature point cloud with thermal gradient legend and Raycaster node inspection
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.AnsysView = (function() {
  'use strict';

  let containerEl = null;
  let renderer = null;
  let scene = null;
  let camera = null;
  let controls = null;
  let pointsMesh = null;
  let rAFId = null;
  let resizeObserver = null;

  let currentData = null;
  let pointSize = 4.0;
  let pinnedNode = null;
  let hoveredNode = null;
  let lastRaycastTime = 0;

  let raycaster = null;
  let mouse = null;
  let statusLine = null;
  let nodeTooltipEl = null;

  function renderHTML() {
    return `
      <!-- Left Column: Controls & Diagnostics -->
      <div class="detail-left-col">
        <div class="detail-text-stack">
          <div class="detail-mask">
            <span class="lbl detail-subtitle">TASK B / THERMAL DATA</span>
          </div>
          <div class="detail-mask">
            <h1 class="detail-title">ANSYS Thermal Data</h1>
          </div>
          <div class="detail-mask">
            <span class="lbl detail-meta">NODAL TEMPERATURES / C++17</span>
          </div>
          <div class="detail-mask">
            <p class="detail-blurb">
              Load nodal temperatures as a coloured point cloud with thermal legend and real-time node inspection.
            </p>
          </div>
          <div class="detail-mask">
            <p class="detail-credit">
              Engineering Workbench assessment module. Candidate ${CONFIG.candidateId}.
            </p>
          </div>
        </div>

        <div class="detail-controls-panel">
          <div class="controls-title">DATA INGESTION & NODAL TELEMETRY</div>

          <!-- Error Banner Mask -->
          <div class="error-banner-mask">
            <div class="error-banner" id="ansys-error-banner">
              <span class="error-banner-title">INGESTION ERROR</span>
              <span class="error-banner-text"></span>
              <span class="error-banner-hint"></span>
            </div>
          </div>

          <!-- Drag and Drop / CSV File Picker Zone -->
          <div class="control-group">
            <label class="control-label">ANSYS CSV Export</label>
            <div id="ansys-dropzone" style="border: 2px dashed var(--rule); padding: 18px 12px; text-align: center; background: #FFFFFF; cursor: pointer;">
              <input type="file" id="ansys-file-input" accept=".csv" style="display: none;" />
              <div style="font-family: var(--font-sans); font-size: 11px; font-weight: 600; color: var(--cobalt);">
                DROP .CSV FILE HERE OR CLICK TO BROWSE
              </div>
              <div style="font-size: 9.5px; color: var(--muted); margin-top: 4px;">
                Expected format: node_id, x_m, y_m, z_m, temperature_C
              </div>
            </div>
          </div>

          <div style="display: flex; gap: 8px;">
            <button class="box cobalt" id="btn-load-ansys-sample" style="flex: 1; height: 32px;">
              LOAD BUNDLED SAMPLE (2,000 NODES)
            </button>
          </div>

          <!-- Point Size Control Slider -->
          <div class="control-group" style="margin-top: 4px;">
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <label class="control-label">Point Particle Size</label>
              <span id="point-size-val" style="font-family: var(--font-mono); font-size: 10px;">4 px</span>
            </div>
            <input type="range" id="slider-point-size" min="1" max="10" step="0.5" value="4" style="cursor: pointer;" />
          </div>

          <!-- Stats Card -->
          <div id="ansys-stats-card" style="display: none; border: 1px solid var(--rule); padding: 12px; background: #FFFFFF; flex-direction: column; gap: 8px;">
            <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--rule); padding-bottom: 4px;">
              <span style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--ink); letter-spacing: 0.08em;">
                THERMAL FIELD STATS
              </span>
              <span id="pill-synthetic" style="display: none; background: #FFF0ED; color: var(--vermilion); border: 1px solid var(--vermilion); font-size: 9px; font-weight: 700; padding: 1px 6px; border-radius: 2px;">
                SYNTHETIC
              </span>
            </div>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 6px; font-family: var(--font-mono); font-size: 10.5px;">
              <div>Nodes: <strong id="stat-node-count">-</strong></div>
              <div>Min Temp: <strong id="stat-min-temp" style="color: #1F3FB0;">-</strong></div>
              <div>Avg Temp: <strong id="stat-avg-temp">-</strong></div>
              <div>Max Temp: <strong id="stat-max-temp" style="color: #E0452B;">-</strong></div>
            </div>
            <div style="font-family: var(--font-mono); font-size: 10px; color: var(--muted); border-top: 1px dashed var(--rule); padding-top: 4px;" id="stat-bounds">
              Bounds: -
            </div>
          </div>

          <!-- Pinned Node Inspector Card -->
          <div id="ansys-pinned-node-card" style="display: none; border: 1px solid var(--cobalt); padding: 10px; background: #F0F4FF; flex-direction: column; gap: 4px;">
            <div style="display: flex; justify-content: space-between; align-items: center;">
              <span style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--cobalt);">
                PINNED NODE INSPECTION
              </span>
              <button id="btn-unpin-node" class="box" style="height: 18px; padding: 0 4px; font-size: 9px;">UNPIN</button>
            </div>
            <div style="font-family: var(--font-mono); font-size: 10.5px;" id="pinned-node-details">
              -
            </div>
          </div>

          <!-- Status Line Container -->
          <div id="ansys-status-container"></div>
        </div>
      </div>

      <!-- Right Column: 3D Point Cloud Viewport -->
      <div class="detail-right-col" id="ansys-viewport-col">
        <div class="viz-container" id="ansys-three-container"></div>
        
        <!-- Viewport Toolbar overlay -->
        <div style="position: absolute; top: 14px; right: 14px; z-index: 20; display: flex; gap: 6px;">
          <button class="box cobalt" id="btn-ansys-fit-view" style="background: rgba(237,235,230,0.9); height: 26px; font-size: 9.5px;">
            FIT TO VIEW
          </button>
        </div>

        <!-- Vertical Thermal Color Legend -->
        <div id="thermal-legend" style="position: absolute; left: 14px; top: 20px; bottom: 20px; z-index: 20; width: 44px; display: flex; flex-direction: column; align-items: center; justify-content: space-between; pointer-events: none; background: rgba(237,235,230,0.85); padding: 8px 4px; border: 1px solid var(--rule);">
          <span id="legend-max-temp" style="font-family: var(--font-mono); font-size: 9px; font-weight: 700; color: #E0452B;">-°C</span>
          <div style="flex: 1; width: 14px; margin: 6px 0; border: 1px solid var(--rule); background: linear-gradient(to bottom, #E0452B, #FFB300, #4CAF50, #00BCD4, #1F3FB0);"></div>
          <span id="legend-min-temp" style="font-family: var(--font-mono); font-size: 9px; font-weight: 700; color: #1F3FB0;">-°C</span>
        </div>

        <!-- Floating Node Hover Tooltip Card -->
        <div id="node-hover-tooltip" style="position: absolute; display: none; z-index: 30; pointer-events: none; background: rgba(20,20,20,0.9); color: #FFFFFF; padding: 6px 10px; font-family: var(--font-mono); font-size: 10px; border-radius: 2px; box-shadow: 0 4px 12px rgba(0,0,0,0.25);">
          <!-- Content populated dynamically -->
        </div>

        <div style="position: absolute; bottom: 12px; right: 14px; z-index: 20; font-family: var(--font-mono); font-size: 10px; color: var(--muted); background: rgba(237,235,230,0.85); padding: 2px 6px; border: 1px solid var(--rule);">
          CLICK POINT TO PIN &bull; ORBIT DRAG TO ROTATE
        </div>
      </div>
    `;
  }

  // Thermal Color Map Helper: blue -> cyan -> green -> yellow -> red
  function tempToColor(normT) {
    const t = Math.max(0, Math.min(1, normT));
    let r, g, b;

    if (t < 0.25) {
      // Blue (0, 0, 1) to Cyan (0, 1, 1)
      const p = t / 0.25;
      r = 0.12 * (1 - p);
      g = 0.25 + 0.75 * p;
      b = 0.7 + 0.3 * p;
    } else if (t < 0.5) {
      // Cyan (0, 1, 1) to Green (0, 1, 0)
      const p = (t - 0.25) / 0.25;
      r = 0;
      g = 1.0;
      b = 1.0 - p;
    } else if (t < 0.75) {
      // Green (0, 1, 0) to Yellow (1, 1, 0)
      const p = (t - 0.5) / 0.25;
      r = p;
      g = 1.0;
      b = 0;
    } else {
      // Yellow (1, 1, 0) to Red (0.88, 0.27, 0.17)
      const p = (t - 0.75) / 0.25;
      r = 1.0 - 0.12 * p;
      g = 1.0 - 0.73 * p;
      b = 0.17 * p;
    }

    return [r, g, b];
  }

  function initThree(viewportContainer) {
    if (typeof THREE === 'undefined') {
      viewportContainer.innerHTML = `
        <div style="display:flex; flex-direction:column; align-items:center; justify-content:center; height:100%; padding:24px; text-align:center;">
          <div style="color:var(--vermilion); font-weight:700; font-family:var(--font-sans); margin-bottom:8px;">
            THREE.JS LIBRARY UNAVAILABLE
          </div>
          <div style="font-size:11px; font-family:var(--font-mono); color:var(--muted); max-width:360px;">
            Could not find window.THREE. Ensure vendor/three.min.js is present.
          </div>
        </div>
      `;
      return false;
    }

    const width = viewportContainer.clientWidth || 500;
    const height = viewportContainer.clientHeight || 500;

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0xF4F2ED);

    camera = new THREE.PerspectiveCamera(45, width / height, 0.001, 100);
    camera.position.set(0.35, 0.25, 0.35);

    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    viewportContainer.appendChild(renderer.domElement);

    if (typeof THREE.OrbitControls !== 'undefined') {
      controls = new THREE.OrbitControls(camera, renderer.domElement);
      controls.enableDamping = true;
      controls.dampingFactor = 0.08;
      controls.target.set(0.125, 0, 0);
    }

    // Ambient light & Subtle axis helper
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.9);
    scene.add(ambientLight);

    const grid = new THREE.GridHelper(0.4, 20, 0x1F3FB0, 0xDDDDDD);
    grid.position.y = -0.05;
    scene.add(grid);

    raycaster = new THREE.Raycaster();
    mouse = new THREE.Vector2();

    function animate() {
      rAFId = requestAnimationFrame(animate);
      if (controls) controls.update();
      if (renderer && scene && camera) {
        renderer.render(scene, camera);
      }
    }
    animate();

    return true;
  }

  function displayPointCloud(data) {
    if (!scene || typeof THREE === 'undefined') return;
    currentData = data;

    if (pointsMesh) {
      scene.remove(pointsMesh);
      if (pointsMesh.geometry) pointsMesh.geometry.dispose();
      if (pointsMesh.material) pointsMesh.material.dispose();
      pointsMesh = null;
    }

    const nodeCount = data.node_count;
    const positions = new Float32Array(data.positions);
    const colors = new Float32Array(nodeCount * 3);

    const minT = data.min_temp_C;
    const maxT = data.max_temp_C;
    const rangeT = maxT - minT || 1.0;

    for (let i = 0; i < nodeCount; i++) {
      const temp = data.temperatures[i];
      const norm = (temp - minT) / rangeT;
      const rgb = tempToColor(norm);
      colors[i * 3 + 0] = rgb[0];
      colors[i * 3 + 1] = rgb[1];
      colors[i * 3 + 2] = rgb[2];
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    geometry.computeBoundingBox();

    const material = new THREE.PointsMaterial({
      size: pointSize,
      vertexColors: true,
      sizeAttenuation: true
    });

    pointsMesh = new THREE.Points(geometry, material);
    scene.add(pointsMesh);

    // Adjust Raycaster threshold relative to geometry bounding box
    const box = geometry.boundingBox;
    const size = new THREE.Vector3();
    box.getSize(size);
    const maxDim = Math.max(size.x, size.y, size.z);
    if (raycaster) {
      raycaster.params.Points.threshold = Math.max(0.003, maxDim * 0.02);
    }

    fitToView();
    updateStats(data);
  }

  function fitToView() {
    if (!pointsMesh || !camera || !controls) return;
    
    pointsMesh.geometry.computeBoundingBox();
    const box = pointsMesh.geometry.boundingBox;
    const center = new THREE.Vector3();
    box.getCenter(center);
    const size = new THREE.Vector3();
    box.getSize(size);

    controls.target.copy(center);
    const maxDim = Math.max(size.x, size.y, size.z);
    camera.position.set(center.x + maxDim * 1.5, center.y + maxDim * 1.2, center.z + maxDim * 1.8);
    camera.lookAt(center);
    controls.update();
  }

  function updateStats(data) {
    const statsCard = containerEl.querySelector('#ansys-stats-card');
    if (!statsCard) return;

    statsCard.style.display = 'flex';

    const pillSynthetic = containerEl.querySelector('#pill-synthetic');
    if (pillSynthetic) {
      pillSynthetic.style.display = data.is_synthetic ? 'inline-block' : 'none';
    }

    containerEl.querySelector('#stat-node-count').textContent = data.node_count.toLocaleString();
    containerEl.querySelector('#stat-min-temp').textContent = `${data.min_temp_C.toFixed(1)} °C`;
    containerEl.querySelector('#stat-avg-temp').textContent = `${data.avg_temp_C.toFixed(1)} °C`;
    containerEl.querySelector('#stat-max-temp').textContent = `${data.max_temp_C.toFixed(1)} °C`;

    containerEl.querySelector('#legend-min-temp').textContent = `${data.min_temp_C.toFixed(0)}°C`;
    containerEl.querySelector('#legend-max-temp').textContent = `${data.max_temp_C.toFixed(0)}°C`;

    if (data.bounds) {
      const min = data.bounds.min.map(v => v.toFixed(3));
      const max = data.bounds.max.map(v => v.toFixed(3));
      containerEl.querySelector('#stat-bounds').textContent = 
        `Bounds (m): [${min.join(', ')}] to [${max.join(', ')}]`;
    }
  }

  // Throttled Raycasting (1 query per 50 ms)
  function handlePointerMove(e) {
    if (!pointsMesh || !camera || !raycaster || !nodeTooltipEl) return;

    const now = performance.now();
    if (now - lastRaycastTime < 50) return;
    lastRaycastTime = now;

    const rect = renderer.domElement.getBoundingClientRect();
    mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

    raycaster.setFromCamera(mouse, camera);
    const intersects = raycaster.intersectObject(pointsMesh);

    if (intersects.length > 0) {
      const idx = intersects[0].index;
      hoveredNode = getNodeData(idx);

      if (!pinnedNode) {
        showTooltip(e.clientX - rect.left, e.clientY - rect.top, hoveredNode);
      }
    } else {
      hoveredNode = null;
      if (!pinnedNode) {
        hideTooltip();
      }
    }
  }

  function handlePointerClick() {
    if (hoveredNode) {
      pinnedNode = hoveredNode;
      showPinnedNode(pinnedNode);
    }
  }

  function getNodeData(index) {
    if (!currentData) return null;
    return {
      nodeId: currentData.node_ids ? currentData.node_ids[index] : index + 1,
      x: currentData.positions[index * 3 + 0],
      y: currentData.positions[index * 3 + 1],
      z: currentData.positions[index * 3 + 2],
      temp: currentData.temperatures[index]
    };
  }

  function showTooltip(x, y, node) {
    if (!nodeTooltipEl || !node) return;
    nodeTooltipEl.style.display = 'block';
    nodeTooltipEl.style.left = `${x + 14}px`;
    nodeTooltipEl.style.top = `${y + 14}px`;
    nodeTooltipEl.innerHTML = `
      <div><strong>NODE #${node.nodeId}</strong></div>
      <div>Temp: <span style="color:#FFA726;">${node.temp.toFixed(1)} °C</span></div>
      <div style="color:#AAA;">x: ${node.x.toFixed(4)} m</div>
      <div style="color:#AAA;">y: ${node.y.toFixed(4)} m</div>
      <div style="color:#AAA;">z: ${node.z.toFixed(4)} m</div>
    `;
  }

  function hideTooltip() {
    if (nodeTooltipEl) nodeTooltipEl.style.display = 'none';
  }

  function showPinnedNode(node) {
    const card = containerEl.querySelector('#ansys-pinned-node-card');
    const details = containerEl.querySelector('#pinned-node-details');
    if (!card || !details) return;

    card.style.display = 'flex';
    details.innerHTML = `
      <div><strong>Node ID:</strong> #${node.nodeId}</div>
      <div><strong>Temperature:</strong> ${node.temp.toFixed(2)} °C</div>
      <div><strong>Position:</strong> (${node.x.toFixed(4)}, ${node.y.toFixed(4)}, ${node.z.toFixed(4)}) m</div>
    `;
  }

  async function loadSample() {
    UI.hideErrorBanner(containerEl);
    statusLine.setRunning('LOADING ANSYS SAMPLE CLOUD');

    try {
      const data = await API.getAnsysSample();
      displayPointCloud(data);
      statusLine.setDone(`LOADED ${data.node_count.toLocaleString()} NODAL POINTS`);
      UI.showToast('ANSYS thermal data cloud rendered');
    } catch (err) {
      statusLine.setError('FAILED TO LOAD SAMPLE');
      UI.showErrorBanner(containerEl, err.message, 'Verify server connection or mock mode.');
    }
  }

  async function handleFile(file) {
    UI.hideErrorBanner(containerEl);

    if (!file.name.toLowerCase().endsWith('.csv')) {
      UI.showErrorBanner(containerEl, 'Invalid file format. Please select an ANSYS exported .csv file.', 'CSV columns: node_id,x_m,y_m,z_m,temperature_C');
      statusLine.setError('INVALID FORMAT');
      return;
    }

    statusLine.setRunning('STREAMING & PARSING CSV');

    const reader = new FileReader();
    reader.onload = async (e) => {
      const csvText = e.target.result;
      try {
        const data = await API.parseAnsys(csvText);
        displayPointCloud(data);
        statusLine.setDone(`INGESTED ${data.node_count.toLocaleString()} NODES`);
        UI.showToast(`Imported ${file.name} successfully`);
      } catch (err) {
        statusLine.setError('PARSE ERROR');
        UI.showErrorBanner(containerEl, err.message, 'Check CSV headers: node_id,x_m,y_m,z_m,temperature_C');
      }
    };
    reader.onerror = () => {
      statusLine.setError('READ ERROR');
      UI.showErrorBanner(containerEl, 'Failed to read local file from disk.', 'Check file read permissions.');
    };
    reader.readAsText(file);
  }

  function bindEvents() {
    const sampleBtn = containerEl.querySelector('#btn-load-ansys-sample');
    if (sampleBtn) sampleBtn.addEventListener('click', loadSample);

    const fitBtn = containerEl.querySelector('#btn-ansys-fit-view');
    if (fitBtn) fitBtn.addEventListener('click', fitToView);

    const unpinBtn = containerEl.querySelector('#btn-unpin-node');
    if (unpinBtn) {
      unpinBtn.addEventListener('click', () => {
        pinnedNode = null;
        const card = containerEl.querySelector('#ansys-pinned-node-card');
        if (card) card.style.display = 'none';
        hideTooltip();
      });
    }

    const pointSlider = containerEl.querySelector('#slider-point-size');
    const pointVal = containerEl.querySelector('#point-size-val');
    if (pointSlider && pointVal) {
      pointSlider.addEventListener('input', (e) => {
        pointSize = parseFloat(e.target.value);
        pointVal.textContent = `${pointSize.toFixed(1)} px`;
        if (pointsMesh && pointsMesh.material) {
          pointsMesh.material.size = pointSize;
        }
      });
    }

    // Drag and Drop & File input
    const dropzone = containerEl.querySelector('#ansys-dropzone');
    const fileInput = containerEl.querySelector('#ansys-file-input');

    if (dropzone && fileInput) {
      dropzone.addEventListener('click', () => fileInput.click());

      fileInput.addEventListener('change', (e) => {
        if (e.target.files && e.target.files[0]) {
          handleFile(e.target.files[0]);
        }
      });

      dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.style.borderColor = 'var(--cobalt)';
        dropzone.style.background = '#F0F4FF';
      });

      dropzone.addEventListener('dragleave', (e) => {
        e.preventDefault();
        dropzone.style.borderColor = 'var(--rule)';
        dropzone.style.background = '#FFFFFF';
      });

      dropzone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropzone.style.borderColor = 'var(--rule)';
        dropzone.style.background = '#FFFFFF';
        if (e.dataTransfer.files && e.dataTransfer.files[0]) {
          handleFile(e.dataTransfer.files[0]);
        }
      });
    }

    // Raycast listeners on renderer canvas
    if (renderer && renderer.domElement) {
      renderer.domElement.addEventListener('pointermove', handlePointerMove, { passive: true });
      renderer.domElement.addEventListener('click', handlePointerClick);
    }
  }

  return {
    resize: function() {
      if (!renderer || !camera) return;
      const viewportContainer = containerEl?.querySelector('#ansys-three-container');
      if (!viewportContainer) return;
      const w = viewportContainer.clientWidth;
      const h = viewportContainer.clientHeight;
      if (w > 0 && h > 0) {
        camera.aspect = w / h;
        camera.updateProjectionMatrix();
        renderer.setSize(w, h);
      }
    },

    mount: function(parentContainer) {
      containerEl = parentContainer;
      containerEl.innerHTML = renderHTML();

      nodeTooltipEl = containerEl.querySelector('#node-hover-tooltip');
      const statusContainer = containerEl.querySelector('#ansys-status-container');
      statusLine = UI.createStatusLine(statusContainer);

      const viewportContainer = containerEl.querySelector('#ansys-three-container');
      const threeReady = initThree(viewportContainer);

      bindEvents();

      if (threeReady) {
        loadSample();
      }

      if (typeof ResizeObserver !== 'undefined' && viewportContainer) {
        resizeObserver = new ResizeObserver(() => this.resize());
        resizeObserver.observe(viewportContainer);
      }
    },

    unmount: function() {
      if (resizeObserver) {
        resizeObserver.disconnect();
        resizeObserver = null;
      }

      if (rAFId) cancelAnimationFrame(rAFId);
      rAFId = null;

      if (controls) {
        controls.dispose();
        controls = null;
      }

      if (pointsMesh) {
        if (pointsMesh.geometry) pointsMesh.geometry.dispose();
        if (pointsMesh.material) pointsMesh.material.dispose();
        pointsMesh = null;
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
      currentData = null;
      pinnedNode = null;
      hoveredNode = null;
      containerEl = null;
    }
  };
})();
