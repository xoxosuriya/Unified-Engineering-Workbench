// ANSYS View: 3D Point Cloud of Nodal Temperatures with Colormap and Raycasting Inspection
const AnsysView = {
  scene: null,
  camera: null,
  renderer: null,
  controls: null,
  pointCloud: null,
  container: null,
  raycaster: null,
  mouse: null,
  nodeData: null,
  tooltip: null,

  init() {
    this.container = document.getElementById('ansys-canvas-container');
    this.tooltip = document.getElementById('ansys-node-tooltip');
    if (!this.container) return;

    const width = this.container.clientWidth || 800;
    const height = this.container.clientHeight || 560;

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(45, width / height, 0.001, 100);
    this.camera.position.set(0.18, 0.16, 0.18);

    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.renderer.setSize(width, height);
    this.renderer.setPixelRatio(window.devicePixelRatio);
    this.container.appendChild(this.renderer.domElement);

    this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.05;

    this.raycaster = new THREE.Raycaster();
    this.raycaster.params.Points.threshold = 0.003; // 3mm hit threshold
    this.mouse = new THREE.Vector2();

    // Subtle grid helper
    const grid = new THREE.GridHelper(0.3, 30, 0x3b82f6, 0x1e293b);
    grid.position.y = -0.001;
    this.scene.add(grid);

    window.addEventListener('resize', () => this.onResize());

    // Mouse move for inspection
    this.container.addEventListener('mousemove', (e) => this.onMouseMove(e));

    const animate = () => {
      requestAnimationFrame(animate);
      this.controls.update();
      this.renderer.render(this.scene, this.camera);
    };
    animate();

    this.bindEvents();
  },

  onResize() {
    if (!this.container || !this.renderer || !this.camera) return;
    const width = this.container.clientWidth;
    const height = this.container.clientHeight;
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height);
  },

  bindEvents() {
    // Dropzone
    const dropzone = document.getElementById('ansys-dropzone');
    const fileInput = document.getElementById('ansys-file-input');

    if (dropzone && fileInput) {
      dropzone.addEventListener('click', () => fileInput.click());
      dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.classList.add('dragover');
      });
      dropzone.addEventListener('dragleave', () => dropzone.classList.remove('dragover'));
      dropzone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');
        if (e.dataTransfer.files.length > 0) {
          this.loadFile(e.dataTransfer.files[0]);
        }
      });

      fileInput.addEventListener('change', (e) => {
        if (e.target.files.length > 0) {
          this.loadFile(e.target.files[0]);
        }
      });
    }

    const loadSampleBtn = document.getElementById('load-sample-ansys-btn');
    if (loadSampleBtn) loadSampleBtn.addEventListener('click', () => this.loadSample());

    const fitBtn = document.getElementById('ansys-fit-btn');
    if (fitBtn) fitBtn.addEventListener('click', () => this.fitToView());

    const pointSizeInput = document.getElementById('ansys-point-size');
    if (pointSizeInput) {
      pointSizeInput.addEventListener('input', (e) => {
        if (this.pointCloud) {
          this.pointCloud.material.size = parseFloat(e.target.value);
        }
      });
    }
  },

  loadSample() {
    App.showToast("Loading Synthetic ANSYS Nodal Results...", "info");
    fetch('/api/ansys/parse', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: 'filepath:data/ansys_thermal_synthetic.csv'
    })
    .then(r => r.json())
    .then(data => {
      if (data.success) {
        this.renderPointCloud(data);
        App.showToast(`Loaded ${data.node_count} nodes (Synthetic ANSYS Dataset)`, "success");
      } else {
        App.showToast(data.error_message, "error");
      }
    })
    .catch(err => App.showToast("Failed to load CSV: " + err, "error"));
  },

  loadFile(file) {
    if (file.size > 50 * 1024 * 1024) {
      App.showToast("File size exceeds 50 MB limit", "error");
      return;
    }
    App.showToast(`Validating ${file.name}...`, "info");
    const reader = new FileReader();
    reader.onload = (e) => {
      const content = e.target.result;
      fetch('/api/ansys/parse', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: content
      })
      .then(r => r.json())
      .then(data => {
        if (data.success) {
          this.renderPointCloud(data);
          App.showToast(`Successfully parsed ${data.node_count} nodes!`, "success");
        } else {
          App.showToast(`CSV Validation Error: ${data.error_message}`, "error");
          document.getElementById('ansys-error-banner').style.display = 'block';
          document.getElementById('ansys-error-msg').textContent = data.error_message;
        }
      })
      .catch(err => App.showToast("CSV parse failed: " + err, "error"));
    };
    reader.readAsText(file);
  },

  // Thermal colormap helper: maps 0..1 to Blue -> Cyan -> Green -> Yellow -> Red
  getThermalColor(val) {
    val = Math.max(0, Math.min(1, val));
    let r = 0, g = 0, b = 0;
    if (val < 0.25) {
      r = 0;
      g = val / 0.25;
      b = 1;
    } else if (val < 0.5) {
      r = 0;
      g = 1;
      b = 1 - (val - 0.25) / 0.25;
    } else if (val < 0.75) {
      r = (val - 0.5) / 0.25;
      g = 1;
      b = 0;
    } else {
      r = 1;
      g = 1 - (val - 0.75) / 0.25;
      b = 0;
    }
    return [r, g, b];
  },

  renderPointCloud(data) {
    document.getElementById('ansys-error-banner').style.display = 'none';
    this.nodeData = data;

    if (this.pointCloud) this.scene.remove(this.pointCloud);

    const count = data.node_count;
    const geometry = new THREE.BufferGeometry();
    const positions = new Float32Array(data.positions);
    const colors = new Float32Array(count * 3);

    const minT = data.min_temp_C;
    const maxT = data.max_temp_C;
    const range = (maxT > minT) ? (maxT - minT) : 1.0;

    for (let i = 0; i < count; i++) {
      const t = data.temperatures[i];
      const norm = (t - minT) / range;
      const rgb = this.getThermalColor(norm);
      colors[i * 3 + 0] = rgb[0];
      colors[i * 3 + 1] = rgb[1];
      colors[i * 3 + 2] = rgb[2];
    }

    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));

    const material = new THREE.PointsMaterial({
      size: 0.0035,
      vertexColors: true,
      sizeAttenuation: true
    });

    this.pointCloud = new THREE.Points(geometry, material);
    this.scene.add(this.pointCloud);

    // Update legend and metrics
    document.getElementById('ansys-legend-min').textContent = `${minT.toFixed(1)} °C`;
    document.getElementById('ansys-legend-max').textContent = `${maxT.toFixed(1)} °C`;

    document.getElementById('ansys-metric-nodes').textContent = Number(count).toLocaleString();
    document.getElementById('ansys-metric-min-temp').textContent = `${minT.toFixed(2)} °C`;
    document.getElementById('ansys-metric-max-temp').textContent = `${maxT.toFixed(2)} °C`;
    document.getElementById('ansys-metric-avg-temp').textContent = `${data.avg_temp_C.toFixed(2)} °C`;

    this.fitToView();
  },

  onMouseMove(e) {
    if (!this.pointCloud || !this.nodeData) return;

    const rect = this.container.getBoundingClientRect();
    this.mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    this.mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

    this.raycaster.setFromCamera(this.mouse, this.camera);
    const intersects = this.raycaster.intersectObject(this.pointCloud);

    if (intersects.length > 0) {
      const idx = intersects[0].index;
      const nodeId = this.nodeData.node_ids[idx];
      const x = this.nodeData.positions[idx * 3 + 0];
      const y = this.nodeData.positions[idx * 3 + 1];
      const z = this.nodeData.positions[idx * 3 + 2];
      const temp = this.nodeData.temperatures[idx];

      this.tooltip.style.display = 'block';
      this.tooltip.style.left = (e.clientX - rect.left + 15) + 'px';
      this.tooltip.style.top = (e.clientY - rect.top + 15) + 'px';
      this.tooltip.innerHTML = `
        <div style="font-weight:700; color:var(--accent-cyan); margin-bottom:4px;">Node #${nodeId}</div>
        <div>Coords: (${(x*1e3).toFixed(1)}, ${(y*1e3).toFixed(1)}, ${(z*1e3).toFixed(1)}) mm</div>
        <div style="font-weight:600; color:#34d399; margin-top:2px;">Temp: ${temp.toFixed(2)} °C</div>
      `;
    } else {
      this.tooltip.style.display = 'none';
    }
  },

  fitToView() {
    if (!this.pointCloud) return;
    const box = new THREE.Box3().setFromObject(this.pointCloud);
    const center = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());

    const maxDim = Math.max(size.x, size.y, size.z);
    const fov = this.camera.fov * (Math.PI / 180);
    let cameraZ = Math.abs(maxDim / 2 / Math.tan(fov / 2)) * 1.6;

    this.camera.position.set(center.x + cameraZ * 0.7, center.y + cameraZ * 0.6, center.z + cameraZ * 0.7);
    this.camera.lookAt(center);
    this.controls.target.copy(center);
    this.controls.update();
  }
};
