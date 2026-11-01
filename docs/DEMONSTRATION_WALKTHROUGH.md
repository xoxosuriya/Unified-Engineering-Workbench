# Demonstration Video & Timestamped Walkthrough Log

**Candidate ID:** RA2311047010144  
**Artifact Reference:** [`workbench_demo.webp`](./workbench_demo.webp) (Bundled 14.9 MB recorded session video)  
**Platform Tested:** Windows 11 x64  
**Date & Time:** 2026-11-01 06:10 UTC  

---

## 1. Demonstration Video
The full 3.5-minute animated session recording is provided in this directory as:
- **`workbench_demo.webp`**: Can be opened directly in any modern web browser, Chrome, Edge, or image viewer.

---

## 2. Chronological Walkthrough & Verification Logs

| Timestamp | Phase / Tab | Action Performed | Verified Observable Output |
|:---:|:---|:---|:---|
| **00:00 - 00:25** | **Launch & Geometry View** | Double-clicked `Launch.exe`. Local server started on `http://127.0.0.1:8080`. Default browser launched automatically. | 3D render of mechanical bracket initialized. Wireframe toggle clicked on and off. "Fit to View" centered the camera. Diagnostics displayed: 32 facets, area $0.0526\text{ m}^2$, bounding box $90 \times 80 \times 100\text{ mm}$. |
| **00:26 - 00:55** | **ANSYS Thermal Data View** | Navigated to "ANSYS Thermal Data" tab. Clicked "Load Synthetic ANSYS Dataset". | 3,485 3D thermal nodes rendered as colored point cloud with thermal gradient (Blue $25^\circ\text{C}$ to Red $103.9^\circ\text{C}$). Mouse hover over point cloud dynamically displayed inspection tooltip with Node ID, 3D Cartesian coordinates, and temperature in $^\circ\text{C}$. |
| **00:56 - 01:40** | **Thermal FEA (1D Rod) View** | Navigated to "Thermal FEA" tab. Problem parameters verified ($L=1.0\text{ m}, k=10, q=10000, T_0=100, T_L=20$). Clicked "Run Full Study (N=10, 20, 40, 80)". | Temperature distribution chart rendered exact analytical parabola vs FE solution. Midpoint error bar chart verified quadratic halving ($1.25^\circ\text{C} \to 0.3125^\circ\text{C} \to 0.078125^\circ\text{C} \to 0.01953125^\circ\text{C}$). Nodal error confirmed $0.00 \times 10^0\ ^\circ\text{C}$. Observed convergence rate $p = 2.000$ confirmed in table. |
| **01:41 - 02:20** | **Multicore Performance View** | Navigated to "Multicore Performance" tab. Workload set to 1,000 cases, mesh $N=100$, PRNG seed 42. Clicked "Run Benchmark". | Progress bar displayed execution across 1, 2, 4 worker threads. 5 measured repetitions table populated. Numerical agreement check displayed green badge: `PASSED (<= 1e-8 °C)` with max difference $0.00\text{e}+00^\circ\text{C}$. Speedup chart rendered measured speedup vs ideal linear curve. |
| **02:21 - 02:55** | **Embedded Hardware (HIL) View** | Navigated to "Embedded Hardware" tab. Switched between STM32H7, Artix-7, and Segger J-Link probe tabs. Tested fault injection. | Live 10 Hz telemetry streaming observed on all three devices (voltages, temperatures, uptime, TAP IDCODE `0x4BA00477`). Clicked "Inject Timeout", triggering red error banner with actionable diagnostic. Clicked "Execute Interface Recovery", successfully restoring nominal streaming state. |
| **02:56 - 03:30** | **Backpropagation View** | Navigated to "Backpropagation & Math" tab. Inspected Step 0 values and finite difference table. | Step 0 values verified ($z=0.5, h=0.462117, \hat{y}=0.462117, J=1.182542$). Central finite difference table confirmed all 4 parameters ($w, b, v, c$) passed with error $\sim 10^{-10} \le 10^{-5}$. 100-step training loss curve showed smooth monotonic decay to 0 as $\hat{y} \to 2.0$. |

---

## 3. Server Execution Log Snippet
```log
=================================================================
 PASS SCIENCE WORKS  |  R&D INTERNSHIP 2026-2027                 
 UNIFIED ENGINEERING WORKBENCH                                   
 C++17 Core Engine & High-Performance Embedded Backend           
=================================================================
[HttpServer] Serving on http://127.0.0.1:8080
[HttpServer] GET /api/status -> 200 OK
[HttpServer] POST /api/stl/parse (filepath:data/sample_bracket.stl) -> 200 OK (32 facets)
[HttpServer] POST /api/ansys/parse (filepath:data/ansys_thermal_synthetic.csv) -> 200 OK (3485 nodes)
[HttpServer] GET /api/fea/convergence -> 200 OK (4 mesh levels, p=2.000)
[HttpServer] GET /api/performance/run -> 200 OK (1000 cases, agreement diff: 0.00e+00 C)
[HttpServer] GET /api/hardware/discover -> 200 OK (3 devices)
[HttpServer] GET /api/hardware/telemetry?id=mcu_stm32h7 -> 200 OK (10 Hz stream)
[HttpServer] POST /api/hardware/inject_fault -> 200 OK (timeout injected)
[HttpServer] POST /api/hardware/recover -> 200 OK (recovered)
[HttpServer] GET /api/backprop/study -> 200 OK (all checks passed)
```
