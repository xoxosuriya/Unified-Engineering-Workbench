# Demonstration Walkthrough & Verification Guide

**Candidate ID:** RA2311047010144  
**Artifact Reference:** [`workbench_demo.webp`](./workbench_demo.webp) (14.92 MB silent screen capture of an older build; 84.7 s duration, 847 frames)  
**Platform Tested:** Microsoft Windows 11 x64  
**Date & Time:** November 1, 2026  

---

## 1. Demonstration Artifact Overview
To allow immediate visual inspection of the workbench in action, an animated session capture is provided in this directory:
- **`workbench_demo.webp`** (14.92 MB / 14,916,542 bytes): A silent screen capture of an older build demonstrating navigation across the six primary tabs (Geometry STL, ANSYS Thermal Cloud, 1D FEA Rod, Multicore Performance, Embedded Hardware, and Backpropagation; duration 84.7 seconds).
- Evaluators can reproduce all interactions on the current build in real time by double-clicking [`Launch.exe`](../Launch.exe) in Windows Explorer. `Launch.exe` spawns the backend server on `http://127.0.0.1:8080`, creates a system tray status icon, and immediately launches the user's default browser.

---

## 2. Chronological Walkthrough & Verification Steps

| View / Action | Interaction Details | Verified Observable Behavior |
|:---|:---|:---|
| **Launch & Geometry View** | Double-clicked `Launch.exe`. Local HTTP server initialized on `127.0.0.1:8080`. Default browser launched to `index.html`. Clicked "Load Sample Bracket". | 3D WebGL render initialized with Three.js. Wireframe toggle toggled mesh rendering mode. "Fit to View" calculated camera distance via bounding sphere. Diagnostics confirmed: **32 facets**, surface area **$0.0463\text{ m}^2$ ($463.00\text{ cm}^2$)**, bounding box **$100 \times 80 \times 100\text{ mm}$**. Topology confirmed as open surface mesh. |
| **ANSYS Thermal Data View** | Switched to "ANSYS Thermal Data" tab. Clicked "Load Synthetic ANSYS Dataset" (`/api/samples/ansys`). | 3,485 nodal temperature points loaded into 3D buffer geometry with continuous jet colormap ($36.87^\circ\text{C}$ blue to $104.00^\circ\text{C}$ red). Raycasting hover over any node displayed an instantaneous tooltip with Node ID, Cartesian coordinates, and local temperature. |
| **Thermal FEA (1D Rod) View** | Switched to "Thermal FEA" tab. Problem parameters verified ($L=1.0\text{ m}, k=10.0\text{ W/(m·K)}, q=10000\text{ W/m}^3, T(0)=100^\circ\text{C}, T(L)=20^\circ\text{C}$). Clicked "Run Full Study (N=10, 20, 40, 80)". | Simultaneous multi-curve line chart plotted all 4 FE discretizations alongside the continuous analytical parabolic exact curve. Midpoint error bar chart verified quadratic $O(h^2)$ halving ($1.25^\circ\text{C} \to 0.3125^\circ\text{C} \to 0.078125^\circ\text{C} \to 0.01953125^\circ\text{C}$ with ratio $4.000$). Nodal errors confirmed zero to machine precision ($< 10^{-14\ \circ}\text{C}$). Exported both summary CSV and spatial $T(x)$ profile CSV. |
| **Multicore Performance View** | Switched to "Multicore Performance" tab. Configured workload to 20,000 cases, mesh $N=200$, PRNG seed 42, 5 repetitions. Clicked "Run Benchmark". | Hardware card displayed host topology (**8 Physical Cores / 12 Logical Cores, 15.7 GB RAM, GNU 16.2.0, -O3**). Real progress bar updated dynamically via atomic progress endpoint. Numerical agreement badge confirmed `PASSED (<= 1e-8 °C)` with max absolute difference $0.00\text{e}+00^\circ\text{C}$. Speedup chart displayed measured scaling (**276.63 ms** on 1 worker, **153.61 ms / 1.8009x** on 2 workers, **85.04 ms / 3.2531x** on 4 workers). Concurrency guard (409) and cancellation verified. |
| **Embedded Hardware (HIL) View** | Switched to "Embedded Hardware" tab. Inspected STM32H7, Artix-7, and Segger J-Link adapter panels. | Status badge confirmed `SIMULATED / 10 Hz MOCK`. Simulated telemetry streaming observed for VCC rails, die temperatures, PLL lock status, and JTAG TAP IDCODE `0x4BA00477`. Clicked "Inject Timeout", triggering red error banner with actionable recovery prompt. Clicked "Execute Interface Recovery", returning hardware state machine to nominal streaming. |
| **Backpropagation View** | Switched to "Backpropagation & Math" tab. Inspected Step 0 values, central finite difference verification table, and 100-step training dynamics. | Step 0 values verified ($z=0.5, h=0.46211716, \hat{y}=0.46211716, J=1.18254182$). Analytical gradients verified: $dJ/dc=-1.53788284, dJ/dv=-0.71068205, dJ/dw=-1.20946448, dJ/db=-1.20946448$. Step 1 updated parameters verified: $w_1=0.62094645, b_1=0.12094645, v_1=1.07106820, c_1=0.15378828$. Central finite difference table confirmed analytical gradients matched numerical derivatives within $1.13 \times 10^{-10} \le 10^{-5}$. 100-step training loss curve showed smooth monotonic decay to 0 as $\hat{y} \to 2.0$. |

---

## 3. Real Server Request Log Excerpt (`logs/server.log`)
*Excerpt recorded directly from `logs/server.log` during verification run on 2026-11-01, Build: Release (GNU 16.2.0, Windows 11 x64, MinGW Static)*

```text
[HttpServer] Serving on http://127.0.0.1:51398 (launch_to_ready: 3.372600 ms)
[HttpServer] GET /api/status -> 200 (275 bytes)
[HttpServer] POST /api/ansys/parse -> 200 (364 bytes)
[HttpServer] GET /api/backprop/study -> 200 (12085 bytes)
[HttpServer] GET /api/export/backprop_csv -> 200 (14209 bytes)
[HttpServer] GET /api/export/fea_csv -> 200 (444 bytes)
[HttpServer] GET /api/export/fea_profile_csv -> 200 (6563 bytes)
[HttpServer] GET /api/export/performance_csv -> 200 (358 bytes)
[HttpServer] GET /api/fea/convergence -> 200 (24985 bytes)
[HttpServer] GET /api/fea/solve -> 200 (1812 bytes)
[HttpServer] POST /api/hardware/connect -> 200 (16 bytes)
[HttpServer] POST /api/hardware/disconnect -> 200 (16 bytes)
[HttpServer] GET /api/hardware/discover -> 200 (1453 bytes)
[HttpServer] POST /api/hardware/inject_fault -> 200 (37 bytes)
[HttpServer] POST /api/hardware/recover -> 200 (16 bytes)
[HttpServer] GET /api/hardware/telemetry -> 200 (565 bytes)
[HttpServer] POST /api/performance/cancel -> 200 (16 bytes)
[HttpServer] GET /api/performance/progress -> 200 (99 bytes)
[HttpServer] GET /api/performance/run -> 200 (965 bytes)
[HttpServer] GET /api/samples/ansys -> 200 (145576 bytes)
[HttpServer] GET /api/samples/stl -> 200 (5474 bytes)
[HttpServer] GET /api/status -> 200 (275 bytes)
[HttpServer] POST /api/stl/parse -> 200 (385 bytes)
[HttpServer] GET /api/status -> 200 (275 bytes)
[HttpServer] POST /api/shutdown -> 200 (26 bytes)
```
