/**
 * Unified Engineering Workbench - Configuration & Data Specifications
 * Candidate: RA2311047010144 - PASS Science Works R&D Assessment
 */

window.CONFIG = {
  // Pure English UI (all Japanese/Chinese glosses disabled)
  showKanji: false,
  
  // Assessment metadata
  candidateId: 'RA2311047010144',
  institution: 'PASS Science Works',
  appTitle: 'Unified Engineering Workbench',
  shortTitle: 'UEW',
  
  // Shelf initialization (index 2 = Thermal FEA)
  initialModuleIndex: 2,
  
  // Network and mock detection
  apiBase: '',
  thermal3dApiBase: '',
  productionApiUrl: 'https://unified-engineering-workbench-backend.onrender.com',
  githubRepoUrl: 'https://github.com/xoxosuriya/Unified-Engineering-Workbench',
  liveAppUrl: 'https://unified-engineering-workbench-indol.vercel.app/',
  statusPollIntervalMs: 5000,
  mockTimeoutMs: 8000,
  
  // UI animation timings
  openDurationMs: 1050,
  closeDurationMs: 850,
  textSlideDurationMs: 900,
  inkDurationMs: 1250,
  inkStartDelayMs: 520,

  // Text helpers
  t: function(en) {
    return en;
  }
};

window.MODULE_GROUPS = [
  { id: 'ALL', label: 'ALL', gloss: 'ALL' },
  { id: 'VISUALIZE', label: 'VISUALIZE', gloss: 'VISUALIZE' },
  { id: 'SOLVE', label: 'SOLVE', gloss: 'SOLVE' },
  { id: 'SYSTEMS', label: 'SYSTEMS', gloss: 'SYSTEMS' }
];

window.MODULES = [
  {
    id: 'geometry',
    index: 0,
    title: 'STL Geometry',
    romaji: 'GEOMETRY',
    label: 'TASK B',
    group: 'VISUALIZE',
    blurb: 'Import an ASCII STL, validate it in C++, inspect it in 3D.',
    taskDesc: 'Task B - C++ parser',
    metaLine: 'TASK B / ASCII 3D MESH / C++17',
    motif: 'geometry'
  },
  {
    id: 'ansys',
    index: 1,
    title: 'ANSYS Thermal Data',
    romaji: 'THERMAL DATA',
    label: 'TASK B',
    group: 'VISUALIZE',
    blurb: 'Load nodal temperatures as a coloured point cloud with legend and node inspection.',
    taskDesc: 'Task B - Point cloud inspection',
    metaLine: 'TASK B / NODAL TEMPERATURES / C++17',
    motif: 'ansys'
  },
  {
    id: 'thermal',
    index: 2,
    title: 'Thermal FEA, 1D Rod',
    romaji: 'THERMAL FEA',
    label: 'TASK C',
    group: 'SOLVE',
    blurb: 'Steady-state rod, 2-node elements, mesh convergence against the exact solution.',
    taskDesc: 'Task C - C++ FEA solver',
    metaLine: 'TASK C / STEADY-STATE FEA / C++17',
    motif: 'thermal'
  },
  {
    id: 'thermal3d',
    index: 3,
    title: '3D Thermal FEA',
    spineTitle: '3D THERMAL FEA',
    romaji: '3D SOLVER',
    label: 'TASK C',
    group: 'SOLVE',
    blurb: 'Three-dimensional finite-volume thermal analysis of solid cube and finned heat-sink geometries using Profile Cholesky, PCG and SOR solvers.',
    taskDesc: 'TASK C / 3D SOLVER',
    metaLine: 'TASK C / 3D FINITE-VOLUME THERMAL / C++17',
    motif: 'thermal3d'
  },
  {
    id: 'backprop',
    index: 4,
    title: 'Backpropagation',
    romaji: 'BACKPROPAGATION',
    label: 'TASK F',
    group: 'SOLVE',
    blurb: 'Hand-derivable gradients, central finite-difference check, 100 training steps.',
    taskDesc: 'Task F - Gradient derivation',
    metaLine: 'TASK F / ANALYTICAL DERIVATION / C++17',
    motif: 'backprop'
  },
  {
    id: 'performance',
    index: 5,
    title: 'Multicore Batch',
    romaji: 'PARALLEL SOLVER',
    label: 'TASK D',
    group: 'SYSTEMS',
    blurb: 'Serial vs bounded worker pool on identical seeded thermal solves.',
    taskDesc: 'Task D - Multicore benchmark',
    metaLine: 'TASK D / PARALLEL SYSTEMS / C++17',
    motif: 'performance'
  },
  {
    id: 'hardware',
    index: 6,
    title: 'Embedded Hardware',
    romaji: 'HARDWARE TELEMETRY',
    label: 'TASK E',
    group: 'SYSTEMS',
    blurb: 'Simulated MCU, FPGA and JTAG adapters with fault injection and recovery.',
    taskDesc: 'Task E - Hardware telemetry',
    metaLine: 'TASK E / EMBEDDED SYSTEMS / C++17',
    motif: 'hardware'
  }
];
