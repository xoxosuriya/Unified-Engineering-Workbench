/**
 * Unified Engineering Workbench - Geometry View (Module 1 / Task B)
 * ASCII STL parser validation, diagnostics card, and WebGL Three.js inspector
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.GeometryView = (function() {
  'use strict';

  let containerEl = null;
  let renderer = null;
  let scene = null;
  let camera = null;
  let controls = null;
  let mesh = null;
  let gridHelper = null;
  let rAFId = null;
  let isWireframe = false;

  let statusLine = null;
  let diagnosticsCard = null;

  function renderHTML() {
    return `
      <!-- Left Column: Controls & Diagnostics -->
      <div class="detail-left-col">
        <div class="detail-text-stack">
          <div class="detail-mask">
            <span class="lbl detail-subtitle">TASK B / 3D GEOMETRY</span>
          </div>
          <div class="detail-mask">
            <h1 class="detail-title">STL Geometry</h1>
          </div>
          <div class="detail-mask">
            <span class="lbl detail-meta">ASCII 3D MESH / C++17 VALIDATOR</span>
          </div>
          <div class="detail-mask">
            <p class="detail-blurb">
              Import an ASCII STL, validate topology and facet connectivity in the C++ engine, and inspect in real-time 3D.
            </p>
          </div>
          <div class="detail-mask">
            <p class="detail-credit">
              Engineering Workbench assessment module. Candidate ${CONFIG.candidateId}.
            </p>
          </div>
        </div>

        <div class="detail-controls-panel">
          <div class="controls-title">MESH INGESTION & DIAGNOSTICS</div>

          <!-- Error Banner Mask -->
          <div class="error-banner-mask">
            <div class="error-banner" id="geo-error-banner">
              <span class="error-banner-title">VALIDATION FAILED</span>
              <span class="error-banner-text"></span>
              <span class="error-banner-hint"></span>
            </div>
          </div>

          <!-- Drag and Drop / File Picker Zone -->
          <div class="control-group">
            <label class="control-label">STL File Source</label>
            <div id="stl-dropzone" style="border: 2px dashed var(--rule); padding: 18px 12px; text-align: center; background: #FFFFFF; cursor: pointer;">
              <input type="file" id="stl-file-input" accept=".stl" style="display: none;" />
              <div style="font-family: var(--font-sans); font-size: 11px; font-weight: 600; color: var(--cobalt);">
                DROP .STL FILE HERE OR CLICK TO BROWSE
              </div>
              <div style="font-size: 9.5px; color: var(--muted); margin-top: 4px;">
                ASCII format only. Maximum file size: 50 MB.
              </div>
            </div>
          </div>

          <div style="display: flex; gap: 8px;">
            <button class="box cobalt" id="btn-load-sample" style="flex: 1; height: 32px;">
              LOAD BUNDLED SAMPLE (L-BRACKET)
            </button>
          </div>

          <!-- Diagnostics Card -->
          <div id="geo-diagnostics" style="display: none; border: 1px solid var(--rule); padding: 12px; background: #FFFFFF; flex-direction: column; gap: 8px;">
            <div style="font-family: var(--font-sans); font-size: 10px; font-weight: 700; color: var(--ink); border-bottom: 1px solid var(--rule); padding-bottom: 4px; letter-spacing: 0.08em;">
              PARSER TELEMETRY & BOUNDS
            </div>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 6px; font-family: var(--font-mono); font-size: 10.5px;">
              <div>Solid: <strong id="diag-name">-</strong></div>
              <div>Facets: <strong id="diag-facets">-</strong></div>
              <div>Surface Area: <strong id="diag-area-m2">-</strong></div>
              <div>Area (cm²): <strong id="diag-area-cm2">-</strong></div>
              <div>Size: <strong id="diag-filesize">-</strong></div>
            </div>
            <div style="font-family: var(--font-mono); font-size: 10px; color: var(--muted); border-top: 1px dashed var(--rule); padding-top: 4px;" id="diag-bounds">
              Bounds: -
            </div>
          </div>

          <!-- Status Line Container -->
          <div id="geo-status-container"></div>
        </div>
      </div>

      <!-- Right Column: Three.js 3D Viewport -->
      <div class="detail-right-col" id="geo-viewport-col">
        <div class="viz-container" id="geo-three-container"></div>
        
        <!-- Viewport Toolbar overlay -->
        <div style="position: absolute; top: 14px; right: 14px; z-index: 20; display: flex; gap: 6px;">
          <button class="box" id="btn-wireframe" style="background: rgba(237,235,230,0.9); height: 26px; font-size: 9.5px;">
            WIREFRAME: OFF
          </button>
          <button class="box cobalt" id="btn-fit-view" style="background: rgba(237,235,230,0.9); height: 26px; font-size: 9.5px;">
            FIT TO VIEW
          </button>
        </div>

        <!-- Metric unit indicator -->
        <div style="position: absolute; bottom: 12px; left: 14px; z-index: 20; font-family: var(--font-mono); font-size: 10px; color: var(--muted); background: rgba(237,235,230,0.85); padding: 2px 6px; border: 1px solid var(--rule);">
          GRID: 0.01 m (10 mm) / UNITS: METRIC
        </div>
      </div>
    `;
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
    scene.background = new THREE.Color(0xF4F2ED); // Paper sheet color

    camera = new THREE.PerspectiveCamera(45, width / height, 0.001, 100);
    camera.position.set(0.18, 0.15, 0.22);

    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    viewportContainer.appendChild(renderer.domElement);

    if (typeof THREE.OrbitControls !== 'undefined') {
      controls = new THREE.OrbitControls(camera, renderer.domElement);
      controls.enableDamping = true;
      controls.dampingFactor = 0.08;
      controls.target.set(0.05, 0.04, 0.025);
    }

    // Lighting
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.65);
    scene.add(ambientLight);

    const dirLight1 = new THREE.DirectionalLight(0xffffff, 0.7);
    dirLight1.position.set(1, 2, 1);
    scene.add(dirLight1);

    const dirLight2 = new THREE.DirectionalLight(0x1F3FB0, 0.25);
    dirLight2.position.set(-1, -1, -1);
    scene.add(dirLight2);

    // Ground Grid Helper (0.2m x 0.2m with 20 divisions = 10mm ticks)
    gridHelper = new THREE.GridHelper(0.3, 30, 0x1F3FB0, 0xCCCCCC);
    gridHelper.position.y = -0.001;
    scene.add(gridHelper);

    // Render loop
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

  function displayMesh(data) {
    if (!scene || typeof THREE === 'undefined') return;

    if (mesh) {
      scene.remove(mesh);
      if (mesh.geometry) mesh.geometry.dispose();
      if (mesh.material) mesh.material.dispose();
      mesh = null;
    }

    const vertices = new Float32Array(data.vertices);
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(vertices, 3));
    geometry.computeVertexNormals();

    const material = new THREE.MeshStandardMaterial({
      color: 0x1F3FB0, // Cobalt
      roughness: 0.35,
      metalness: 0.15,
      wireframe: isWireframe,
      flatShading: true,
      side: THREE.DoubleSide
    });

    mesh = new THREE.Mesh(geometry, material);
    scene.add(mesh);

    fitToView();
    updateDiagnostics(data);
  }

  function fitToView() {
    if (!mesh || !camera || !controls) return;
    
    mesh.geometry.computeBoundingBox();
    const box = mesh.geometry.boundingBox;
    const center = new THREE.Vector3();
    box.getCenter(center);
    const size = new THREE.Vector3();
    box.getSize(size);

    controls.target.copy(center);
    const maxDim = Math.max(size.x, size.y, size.z);
    camera.position.set(center.x + maxDim * 1.6, center.y + maxDim * 1.3, center.z + maxDim * 2.0);
    camera.lookAt(center);
    controls.update();
  }

  function updateDiagnostics(data) {
    if (!diagnosticsCard) return;
    diagnosticsCard.style.display = 'flex';

    document.querySelector('#diag-name').textContent = data.solid_name || 'SOLID_01';
    document.querySelector('#diag-facets').textContent = (data.facet_count || 0).toLocaleString();
    
    const areaM2 = data.surface_area || 0;
    document.querySelector('#diag-area-m2').textContent = `${areaM2.toFixed(4)} m²`;
    document.querySelector('#diag-area-cm2').textContent = `${(areaM2 * 10000).toFixed(1)} cm²`;
    
    const sizeBytes = data.file_size_bytes || (data.vertices ? data.vertices.length * 4 : 0);
    document.querySelector('#diag-filesize').textContent = `${(sizeBytes / 1024).toFixed(1)} KB`;

    if (data.bounds) {
      const min = data.bounds.min.map(v => (v * 1000).toFixed(1));
      const max = data.bounds.max.map(v => (v * 1000).toFixed(1));
      document.querySelector('#diag-bounds').textContent = 
        `Bounds (mm): [${min.join(', ')}] to [${max.join(', ')}]`;
    }
  }

  async function loadSample() {
    UI.hideErrorBanner(containerEl);
    statusLine.setRunning('PARSING BUNDLED SAMPLE');

    try {
      const data = await API.getStlSample();
      displayMesh(data);
      statusLine.setDone(`SOLID VALIDATED (${data.facet_count} FACETS)`);
      UI.showToast('Bundled L-bracket loaded successfully');
    } catch (err) {
      statusLine.setError('PARSE ERROR');
      UI.showErrorBanner(containerEl, err.message, 'Verify that the server has sample files available');
    }
  }

  async function handleFile(file) {
    UI.hideErrorBanner(containerEl);

    // Client-side 50 MB check
    const maxSizeBytes = 50 * 1024 * 1024;
    if (file.size > maxSizeBytes) {
      const msg = `File size ${(file.size / (1024 * 1024)).toFixed(1)} MB exceeds the 50 MB limit.`;
      UI.showErrorBanner(containerEl, msg, 'Please provide an ASCII STL under 50 MB.');
      statusLine.setError('FILE TOO LARGE');
      return;
    }

    if (!file.name.toLowerCase().endsWith('.stl')) {
      UI.showErrorBanner(containerEl, 'Invalid file extension. Expected an .stl file.', 'Check that you selected an ASCII STL.');
      statusLine.setError('INVALID FORMAT');
      return;
    }

    statusLine.setRunning('STREAMING & PARSING STL');

    const reader = new FileReader();
    reader.onload = async (e) => {
      const text = e.target.result;
      try {
        const data = await API.parseStl(text);
        displayMesh(data);
        statusLine.setDone(`STL VALIDATED (${data.facet_count} FACETS)`);
        UI.showToast(`Imported ${file.name} successfully`);
      } catch (err) {
        statusLine.setError('PARSE FAILED');
        UI.showErrorBanner(containerEl, err.message, 'Check that the file is an ASCII STL, not binary');
      }
    };
    reader.onerror = () => {
      statusLine.setError('READ ERROR');
      UI.showErrorBanner(containerEl, 'Failed to read file from local disk.', 'Check file read permissions.');
    };
    reader.readAsText(file);
  }

  function bindEvents() {
    const sampleBtn = containerEl.querySelector('#btn-load-sample');
    if (sampleBtn) sampleBtn.addEventListener('click', loadSample);

    const wireframeBtn = containerEl.querySelector('#btn-wireframe');
    if (wireframeBtn) {
      wireframeBtn.addEventListener('click', () => {
        isWireframe = !isWireframe;
        wireframeBtn.textContent = `WIREFRAME: ${isWireframe ? 'ON' : 'OFF'}`;
        if (mesh && mesh.material) {
          mesh.material.wireframe = isWireframe;
        }
      });
    }

    const fitBtn = containerEl.querySelector('#btn-fit-view');
    if (fitBtn) fitBtn.addEventListener('click', fitToView);

    // Drag and Drop & File input
    const dropzone = containerEl.querySelector('#stl-dropzone');
    const fileInput = containerEl.querySelector('#stl-file-input');

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
  }

  return {
    resize: function() {
      if (!renderer || !camera) return;
      const viewportContainer = containerEl?.querySelector('#geo-three-container');
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

      const statusContainer = containerEl.querySelector('#geo-status-container');
      statusLine = UI.createStatusLine(statusContainer);
      diagnosticsCard = containerEl.querySelector('#geo-diagnostics');

      const viewportContainer = containerEl.querySelector('#geo-three-container');
      const threeReady = initThree(viewportContainer);

      bindEvents();

      // Automatically load bundled sample on first mount
      if (threeReady) {
        loadSample();
      }

      // Observe size changes
      if (typeof ResizeObserver !== 'undefined' && viewportContainer) {
        this._ro = new ResizeObserver(() => this.resize());
        this._ro.observe(viewportContainer);
      }
    },

    unmount: function() {
      if (this._ro) {
        this._ro.disconnect();
        this._ro = null;
      }

      if (rAFId) cancelAnimationFrame(rAFId);
      rAFId = null;

      if (controls) {
        controls.dispose();
        controls = null;
      }

      if (mesh) {
        if (mesh.geometry) mesh.geometry.dispose();
        if (mesh.material) mesh.material.dispose();
        mesh = null;
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
      containerEl = null;
    }
  };
})();
