# Cloud Deployment Guide: Unified Engineering Workbench

**PASS Science Works — R&D Internship Assessment 2026–2027**  
**Candidate:** Suriya Prakash S (RA2311047010144)  
**Institution:** SRM Institute of Science and Technology, Kattankulathur, Chennai, India  
**GitHub Repository:** [https://github.com/xoxosuriya/Unified-Engineering-Workbench](https://github.com/xoxosuriya/Unified-Engineering-Workbench)  
**Live Frontend:** [https://xoxosuriya.github.io/Unified-Engineering-Workbench/](https://xoxosuriya.github.io/Unified-Engineering-Workbench/)  
**Live Backend Service:** [https://unified-engineering-workbench-backend.onrender.com](https://unified-engineering-workbench-backend.onrender.com)  

---

## 1. Architecture Topology

```
+-------------------------------------------------------------+
|                     CLIENT BROWSER                          |
|         Desktop / Laptop / Tablet (Chrome / Edge / Firefox) |
+------------------------------+------------------------------+
                               |
       +-----------------------+-----------------------+
       | HTTPS GET Assets                              | HTTPS REST API & CORS Preflight
       v                                               v
+-------------------------------+       +------------------------------------+
|         FRONTEND HOST         |       |            BACKEND HOST            |
|         GitHub Pages          |       |        Render Web Service          |
|  Static HTML, CSS, Three.js   |       |   Docker-based C++17 REST Server   |
|  /Unified-Engineering-        |       |  https://unified-engineering-      |
|  Workbench/                   |       |  workbench-backend.onrender.com    |
+-------------------------------+       +------------------------------------+
```

---

## 2. Frontend Deployment: GitHub Pages

The static frontend is continuously deployed using official GitHub Actions from `src/frontend/`:

1. **Repository Settings:**
   - Source: **GitHub Actions** (under Settings &rarr; Pages).
   - Base Path: `/Unified-Engineering-Workbench/`
2. **Workflow Configuration:**
   - Workflow source templates are maintained in `deploy/workflows/deploy-pages.yml` and `deploy/workflows/ci.yml`.
   - On GitHub with active workflow permissions (or upon copying to `.github/workflows/deploy-pages.yml`), deploys trigger automatically on push to branch `main`.
   - Uses `actions/upload-pages-artifact@v3` and `actions/deploy-pages@v4`.
   - Zero build step required: Pure native HTML5, ES6, and Three.js assets.
3. **Subpath Compatibility:**
   - All asset links (`href="css/style.css"`, `src="vendor/three.min.js"`) use relative paths.
   - Works identically at root `http://localhost:3000/` and subpath `/Unified-Engineering-Workbench/`.

---

## 3. Backend Deployment: Render Web Service

The C++ backend is containerized via a multi-stage Docker build and hosted on Render:

1. **Service Configuration:**
   - **Service Type:** Web Service
   - **Environment:** Docker
   - **Repository:** `https://github.com/xoxosuriya/Unified-Engineering-Workbench`
   - **Branch:** `main`
   - **DockerfilePath:** `./Dockerfile`
   - **Plan:** Free
   - **Region:** Oregon (US West) or Frankfurt (EU Central)
   - **Health Check Path:** `/api/thermal3d/health`
2. **Environment Variables:**
   - `PORT`: `10000` (Render dynamically routes to container port)
   - `HOST`: `0.0.0.0`
   - `WORKBENCH_ENV`: `production`
   - `DISABLE_SHUTDOWN`: `1`
   - `ALLOWED_ORIGINS`: `https://xoxosuriya.github.io,http://localhost:3000,http://127.0.0.1:3000`
3. **Render Blueprint (`render.yaml`):**
   - The repository includes a checked-in `render.yaml` blueprint for one-click infrastructure-as-code deployment via the Render Dashboard.
4. **Important Operational Note on Render Free Tier:**
   - Render Free web services spin down into an idle state after 15 minutes of inactivity.
   - When accessed after spinning down, a cold start of approximately 30–50 seconds may occur while the container spins up.
   - The frontend includes a dedicated status indicator (`CHECKING C++17 SOLVER...` &rarr; `PRODUCTION ONLINE`) and reconnection polling to handle cold starts transparently.

---

## 4. Local Deployment (Windows Desktop)

1. Extract the distribution archive or clone this repository.
2. Double-click `Launch.exe` in the root folder.
3. The native C++ server binds to `127.0.0.1:8080` (or the next available port) and opens your default browser automatically.
4. To terminate, click **"Exit Workbench"** or close from the system tray icon.
