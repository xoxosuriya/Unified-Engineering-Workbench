# Demonstration Walkthrough & Verification Guide

**Candidate ID:** RA2311047010144  
**Artifact Reference:** [`workbench_demo.webp`](./workbench_demo.webp) (1.49 MB animated browser interaction capture)  
**Platform Tested:** Microsoft Windows 11 x64  
**Date & Time:** November 1, 2026  

---

## 1. Demonstration Artifact Overview
To allow immediate visual inspection of the workbench in action, an animated session capture is provided in this directory:
- **`workbench_demo.webp`** (1.49 MB): A multi-view demonstration recording showing navigation across the six primary tabs (Geometry STL, ANSYS Thermal Cloud, 1D FEA Rod, Multicore Performance, Embedded Hardware, and Backpropagation).
- Evaluators can reproduce all interactions in real time by double-clicking [`Launch.exe`](../Launch.exe) in Windows Explorer. `Launch.exe` spawns the backend server on `http://127.0.0.1:8080`, creates a system tray status icon, and immediately launches the user's default browser.

---

## 2. Chronological Walkthrough & Verification Steps

| View / Action | Interaction Details | Verified Observable Behavior |
|:---|:---|:---|
| **Launch & Geometry View** | Double-clicked `Launch.exe`. Local HTTP server initialized on `127.0.0.1:8080`. Default browser launched to `index.html`. Clicked "Load Sample Bracket". | 3D WebGL render initialized with Three.js. Wireframe toggle toggled mesh rendering mode. "Fit to View" calculated camera distance via bounding sphere. Diagnostics confirmed: **32 facets**, surface area **$0.0463\text{ m}^2$ ($462.97\text{ cm}^2$)**, bounding box **$100 \times 80 \times 100\text{ mm}$**. |
| **ANSYS Thermal Data View** | Switched to "ANSYS Thermal Data" tab. Clicked "Load Sample Dataset" (`data/ansys_thermal_synthetic.csv`). | 3,485 nodal temperature points loaded into 3D buffer geometry with continuous jet colormap ($36.87^\circ\text{C}$ blue to $104.00^\circ\text{C}$ red). Raycasting hover over any node displayed an instantaneous tooltip with Node ID, Cartesian coordinates, and local temperature. |
| **Thermal FEA (1D Rod) View** | Switched to "Thermal FEA" tab. Problem parameters verified ($L=1.0\text{ m}, k=10.0\text{ W/(m·K)}, q=10000\text{ W/m}^3, T(0)=100^\circ\text{C}, T(L)=20^\circ\text{C}$). Clicked "Run Full Study (N=10, 20, 40, 80)". | Simultaneous multi-curve line chart plotted all 4 FE discretizations alongside the continuous analytical parabolic exact curve. Midpoint error bar chart verified quadratic $O(h^2)$ halving ($1.25^\circ\text{C} \to 0.3125^\circ\text{C} \to 0.078125^\circ\text{C} \to 0.01953125^\circ\text{C}$ with ratio $4.000$). Nodal errors confirmed zero to machine precision ($< 10^{-14\ \circ}\text{C}$). Exported both summary CSV and spatial $T(x)$ profile CSV. |
| **Multicore Performance View** | Switched to "Multicore Performance" tab. Configured batch size to 3,000 cases, mesh $N=100$, PRNG seed 42. Clicked "Run Benchmark". | Hardware card displayed host topology (**8 Physical Cores / 12 Threads, 15.7 GB RAM, GCC -O3**). Animated progress bar displayed execution across worker pools (1, 2, 4 threads). Numerical agreement badge confirmed `PASSED (<= 1e-8 °C)` with max absolute difference $0.00\text{e}+00^\circ\text{C}$. Speedup chart displayed real scaling ($1.70\times$ on 2 workers, $2.09\times$ on 4 workers). Cancellation tested and verified via `/api/performance/cancel`. |
| **Embedded Hardware (HIL) View** | Switched to "Embedded Hardware" tab. Inspected STM32H7, Artix-7, and Segger J-Link adapter panels. | Status badge confirmed `SIMULATED / 10 Hz MOCK`. Simulated telemetry streaming observed for VCC rails, die temperatures, PLL lock status, and JTAG TAP IDCODE `0x4BA00477`. Clicked "Inject Timeout", triggering red error banner with actionable recovery prompt. Clicked "Execute Interface Recovery", returning hardware state machine to nominal streaming. |
| **Backpropagation View** | Switched to "Backpropagation & Math" tab. Inspected Step 0 values, central finite difference verification table, and 100-step training dynamics. | Step 0 values verified ($z=0.5, h=0.462117, \hat{y}=0.462117, J=1.182542$). Central finite difference table confirmed analytical gradients for $w, b, v, c$ matched numerical derivatives within $10^{-10} \le 10^{-5}$. 100-step training loss curve showed smooth monotonic decay to 0 as $\hat{y} \to 2.0$. |

---

## 3. Real Server Console Log
The C++ HTTP server outputs real request logging directly to standard output:
```text
=================================================================
 PASS SCIENCE WORKS  |  R&D INTERNSHIP 2026-2027                 
 UNIFIED ENGINEERING WORKBENCH                                   
 C++17 Core Engine & High-Performance Embedded Backend           
=================================================================
[HttpServer] Serving on http://127.0.0.1:8080 (root: src/frontend)
[HttpServer] GET / -> 200 OK (39002 bytes)
[HttpServer] GET /css/style.css -> 200 OK (12620 bytes)
[HttpServer] GET /js/app.js -> 200 OK (3058 bytes)
[HttpServer] GET /js/geometry_view.js -> 200 OK (7554 bytes)
[HttpServer] GET /js/ansys_view.js -> 200 OK (9537 bytes)
[HttpServer] GET /js/thermal_view.js -> 200 OK (8120 bytes)
[HttpServer] GET /js/performance_view.js -> 200 OK (7140 bytes)
[HttpServer] GET /js/hardware_view.js -> 200 OK (8683 bytes)
[HttpServer] GET /js/backprop_view.js -> 200 OK (5277 bytes)
[HttpServer] GET /api/status -> 200 OK (62 bytes)
[HttpServer] POST /api/stl/parse -> 200 OK (4982 bytes)
[HttpServer] POST /api/ansys/parse -> 200 OK (184200 bytes)
[HttpServer] GET /api/fea/convergence?L=1&A=0.0001&k=10&q=10000&T0=100&TL=20 -> 200 OK (2840 bytes)
[HttpServer] GET /api/performance/run?cases=3000&mesh=100&seed=42 -> 200 OK (1120 bytes)
[HttpServer] GET /api/hardware/discover -> 200 OK (1240 bytes)
[HttpServer] GET /api/hardware/telemetry?id=mcu_stm32h7 -> 200 OK (384 bytes)
[HttpServer] POST /api/hardware/inject_fault -> 200 OK (48 bytes)
[HttpServer] POST /api/hardware/recover -> 200 OK (42 bytes)
[HttpServer] GET /api/backprop/study -> 200 OK (14200 bytes)
```
