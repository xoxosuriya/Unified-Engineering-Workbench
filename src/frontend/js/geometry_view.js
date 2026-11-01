// Geometry View: 3D ASCII STL Renderer using Three.js
const GeometryView = {
  scene: null,
  camera: null,
  renderer: null,
  controls: null,
  mesh: null,
  wireframeMesh: null,
  isWireframe: false,
  container: null,

  init() {
    this.container = document.getElementById('stl-canvas-container');
    if (!this.container) return;

    const width = this.container.clientWidth || 800;
    const height = this.container.clientHeight || 560;

    // Scene
    this.scene = new THREE.Scene();

    // Camera
    this.camera = new THREE.PerspectiveCamera(45, width / height, 0.001, 100);
    this.camera.position.set(0.18, 0.16, 0.18);

    // Renderer
    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.renderer.setSize(width, height);
    this.renderer.setPixelRatio(window.devicePixelRatio);
    this.renderer.shadowMap.enabled = true;
    this.container.appendChild(this.renderer.domElement);

    // OrbitControls
    this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.05;

    // Lighting
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.55);
    this.scene.add(ambientLight);

    const dirLight1 = new THREE.DirectionalLight(0xffffff, 0.85);
    dirLight1.position.set(1, 2, 1);
    this.scene.add(dirLight1);

    const dirLight2 = new THREE.DirectionalLight(0x06b6d4, 0.4);
    dirLight2.position.set(-1, -1, -1);
    this.scene.add(dirLight2);

    // Grid helper
    const gridHelper = new THREE.GridHelper(0.3, 30, 0x06b6d4, 0x1e293b);
    gridHelper.position.y = -0.001;
    this.scene.add(gridHelper);

    // Resize handling
    window.addEventListener('resize', () => this.onResize());

    // Animation loop
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
    const dropzone = document.getElementById('stl-dropzone');
    const fileInput = document.getElementById('stl-file-input');

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

    // Load sample button
    const loadSampleBtn = document.getElementById('load-sample-stl-btn');
    if (loadSampleBtn) {
      loadSampleBtn.addEventListener('click', () => this.loadSample());
    }

    // Controls
    const fitBtn = document.getElementById('stl-fit-btn');
    if (fitBtn) fitBtn.addEventListener('click', () => this.fitToView());

    const wireframeBtn = document.getElementById('stl-wireframe-btn');
    if (wireframeBtn) {
      wireframeBtn.addEventListener('click', () => {
        this.isWireframe = !this.isWireframe;
        wireframeBtn.classList.toggle('active', this.isWireframe);
        if (this.mesh) this.mesh.material.wireframe = this.isWireframe;
      });
    }
  },

  loadSample() {
    App.showToast("Loading Sample Mechanical Bracket STL...", "info");
    fetch('/api/stl/parse', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: 'filepath:data/sample_bracket.stl'
    })
    .then(r => r.json())
    .then(data => {
      if (data.success) {
        this.renderMesh(data);
        App.showToast(`Loaded ${data.facet_count} facets (${data.solid_name})`, "success");
      } else {
        App.showToast(data.error_message, "error");
      }
    })
    .catch(err => App.showToast("Failed to load STL: " + err, "error"));
  },

  loadFile(file) {
    if (file.size > 50 * 1024 * 1024) {
      App.showToast("File size exceeds 50 MB limit", "error");
      return;
    }
    App.showToast(`Uploading and validating ${file.name}...`, "info");
    const reader = new FileReader();
    reader.onload = (e) => {
      const content = e.target.result;
      fetch('/api/stl/parse', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: content
      })
      .then(r => r.json())
      .then(data => {
        if (data.success) {
          this.renderMesh(data);
          App.showToast(`Successfully parsed ${data.facet_count} facets!`, "success");
        } else {
          App.showToast(`STL Validation Error: ${data.error_message}`, "error");
          document.getElementById('stl-error-banner').style.display = 'block';
          document.getElementById('stl-error-msg').textContent = data.error_message;
        }
      })
      .catch(err => App.showToast("Parse failed: " + err, "error"));
    };
    reader.readAsText(file);
  },

  renderMesh(data) {
    document.getElementById('stl-error-banner').style.display = 'none';

    if (this.mesh) this.scene.remove(this.mesh);

    const geometry = new THREE.BufferGeometry();
    const vertices = new Float32Array(data.vertices);
    const normals = new Float32Array(data.normals);

    geometry.setAttribute('position', new THREE.BufferAttribute(vertices, 3));
    geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3));

    const material = new THREE.MeshStandardMaterial({
      color: 0x06b6d4,
      metalness: 0.25,
      roughness: 0.45,
      wireframe: this.isWireframe,
      side: THREE.DoubleSide
    });

    this.mesh = new THREE.Mesh(geometry, material);
    this.scene.add(this.mesh);

    // Update UI Metrics
    document.getElementById('stl-metric-solid').textContent = data.solid_name || 'Bracket';
    document.getElementById('stl-metric-facets').textContent = Number(data.facet_count).toLocaleString();
    document.getElementById('stl-metric-area').textContent = (data.surface_area * 1e4).toFixed(2) + ' cm²';
    
    const dx = ((data.bounds.max[0] - data.bounds.min[0]) * 1e3).toFixed(1);
    const dy = ((data.bounds.max[1] - data.bounds.min[1]) * 1e3).toFixed(1);
    const dz = ((data.bounds.max[2] - data.bounds.min[2]) * 1e3).toFixed(1);
    document.getElementById('stl-metric-bbox').textContent = `${dx} × ${dy} × ${dz} mm`;

    this.fitToView();
  },

  fitToView() {
    if (!this.mesh) return;
    const box = new THREE.Box3().setFromObject(this.mesh);
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
