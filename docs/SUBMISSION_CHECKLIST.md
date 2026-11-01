# Candidate Submission Action Checklist

**Candidate ID:** RA2311047010144  
**Project:** Unified Engineering Workbench  
**Organization:** PASS SCIENCE WORKS — R&D Internship 2026–2027 Assessment  
**Submission Deadline:** 02-11-2026 at 10:00 AM  
**Google Form Submission Link:** https://forms.gle/YH21idmUVsZHxjY39

---

## Actions Only the Candidate Can Perform

Before submitting your final assessment archive and form, verify and complete the following 5 human-only deliverables:

### 1. Photograph Handwritten Backpropagation Derivation
- [ ] On clean, ruled paper, handwrite the Task F two-page backpropagation derivation (model architecture, forward evaluation at Step 0, analytical gradients $\partial J/\partial w, \partial J/\partial b, \partial J/\partial v, \partial J/\partial c$, simultaneous parameter update for Step 1, and central finite difference verification).
- [ ] Take clear, legible camera photographs (maximum 2 pages):
  - Save as `docs/handwritten_backprop/page_1_scan.jpg`
  - Save as `docs/handwritten_backprop/page_2_scan.jpg`
- [ ] Confirm all numbers match the typed mathematical reference in `docs/handwritten_backprop/handwritten_derivation.md`.

### 2. Record 3–5 Minute Demonstration Video
- [ ] On a Windows 11 x64 machine, double-click `Launch.exe` from the unzipped directory.
- [ ] Record a continuous 3 to 5-minute screen video (MP4 format) demonstrating:
  1. Instantaneous zero-terminal launch and browser navigation to `http://127.0.0.1:8080`.
  2. Loading the sample bracket STL (`/api/samples/stl`) with 3D rotation, wireframe toggle, and bounding box inspection.
  3. Loading the synthetic ANSYS dataset (`/api/samples/ansys`) with jet thermal colormap and raycast hover tooltips.
  4. Executing the 1D Thermal FEA convergence study ($N=10, 20, 40, 80$) and reviewing quadratic midpoint error reduction.
  5. Running the Multicore parallel benchmark with real-time progress, 0.0 diff agreement, and cancellation testing.
  6. Navigating to Embedded Hardware, streaming 10 Hz telemetry, injecting a fault, and performing interface recovery.
  7. Viewing the Backpropagation derivation, gradient table, and 100-step training loss curve.
  8. Graceful shutdown via browser header or system tray icon.

### 3. Test in Windows Sandbox (Zero Dev-Tools Verification)
- [ ] Enable Windows Sandbox on Windows 11 (or use a clean PC without GCC, CMake, Python, or Node.js).
- [ ] Copy `RA2311047010144_Workbench.zip` into the Sandbox and extract it.
- [ ] Double-click `Launch.exe` and verify that the workbench opens immediately in Microsoft Edge and all features work out-of-the-box without prompts, compilation, or administrative rights.

### 4. Review and Confirm Effort Hours and AI-Assistance Log
- [ ] Open `docs/AI_LOG.md` and `docs/OWNERSHIP.md`.
- [ ] Confirm the reported effort hours, breakdown of AI vs human authorship, and critique of unsuitable suggestions accurately represent your actual work.

### 5. Print Technical Report to PDF (Page Budget Verification)
- [ ] Open `docs/Workbench_Report.md` in VS Code / Antigravity Markdown Preview or browser.
- [ ] Print to PDF (Standard A4 / Letter, normal margins).
- [ ] Verify that the generated PDF prints on **at most 6 pages** (target word count is 2,201 words).

---

## Submission Package Contents
- **Primary Archive:** `RA2311047010144_Workbench.zip` (generated via `tools/package.ps1` using `tar -a -c -f`).
- **Complete Git History Bundle:** `RA2311047010144_Workbench.bundle` (contains all branch commits and tags).
- **Commit Log:** `docs/git_commit_log.txt` (UTF-8 chronological Git commit history).
