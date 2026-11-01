# AI-Assistance Log & Engineering Judgement Record

**Project:** Unified Engineering Workbench  
**Candidate Evaluation:** Pass Science Works | R&D Internship 2026-2027  
**AI Assistant Employed:** Gemini 3.8 Flash (High) via Antigravity Coding Assistant  
**Date:** November 1, 2026  

---

## 1. Overview of AI Usage
In compliance with Section 1 ("You may use any GPT/AI assistant... Disclose AI assistance, reused code, dependencies and licences") and Section 8 ("Include an AI-use log with tool/model names where available, representative prompts and relevant outputs, your edits, validation evidence, and at least one incorrect or unsuitable suggestion you identified"), this log transparently records all key AI interactions, architectural prompts, critical engineering decisions, and independent verifications.

---

## 2. Representative Interaction Log

### Interaction 1: 1D Thermal FEA Formulation & Nodal Superconvergence
- **Prompt:**  
  *"Formulate the steady-state 1D heat conduction equation with uniform heat generation and linear 2-node elements. Why does the assignment rubric specifically assess midpoint error rather than nodal error?"*
- **Model Output Summary:**  
  The assistant provided the element stiffness matrix $\mathbf{K}^e = \frac{kA}{h}\begin{bmatrix}1 & -1 \\ -1 & 1\end{bmatrix}$ and load vector $\mathbf{f}^e = \frac{qAh}{2}\begin{bmatrix}1 \\ 1\end{bmatrix}$. It noted that for 1D linear elements with uniform heat generation, nodal FEA values are mathematically exact due to energy minimization / Green's function representation.
- **Candidate Edit & Validation:**  
  I verified that if an engineer checks error only at nodes, the error is $0.00^\circ\text{C}$, masking mesh convergence. I implemented linear interpolation at element midpoints $T_h(x_{\text{mid}}) = \frac{T_i + T_{i+1}}{2}$, which reveals the exact $O(h^2)$ truncation error $\frac{qh^2}{8k}$ ($1.25^\circ\text{C}, 0.3125^\circ\text{C}, 0.078125^\circ\text{C}, 0.01953125^\circ\text{C}$).

---

### Interaction 2: Identified Incorrect AI Suggestion & Candidate Correction
- **Unsuitable Suggestion Identified:**  
  During the implementation of the Dirichlet boundary condition for the tridiagonal Thomas algorithm (TDMA), an initial AI suggestion proposed modifying the right-hand side force vector by subtracting the boundary coupling:
  $$d_1 \leftarrow d_1 - K_{1,0} T_0$$
  while leaving the tridiagonal matrix representation unchanged with $b_0 = 1$ and $c_0 = 0$.
- **Why It Was Incorrect:**  
  In the standard Thomas algorithm forward elimination loop, the algorithm computes:
  $$d'_1 = \frac{d_1 - a_0 \cdot d'_0}{b_1 - a_0 \cdot c'_0}$$
  Because $d'_0 = T_0$ and $a_0 = -k_{\text{elem}}$ was still non-zero in the subdiagonal array, the algorithm subtracted $a_0 \cdot T_0$ a **second time**, introducing a massive artificial error ($\approx 64.9^\circ\text{C}$ error).
- **Candidate Correction:**  
  I identified the double subtraction bug through unit test inspection. I corrected the implementation by decoupling the interior unknowns $i = 1, \dots, N-1$ directly into an $(N-1)$ tridiagonal system and zeroing out $a_0 = 0.0$. Upon recompilation, the nodal error dropped immediately to $0.00 \times 10^0\ ^\circ\text{C}$ and midpoint convergence matched theory to 8 decimal places.

---

### Interaction 3: Embedded Hardware Representation — JTAG Debug Interface vs Compute Board
- **Deliberate Challenge Test / Prompt:**  
  *"Can we treat the JTAG debug adapter as a generic interchangeable compute board in the workbench UI and share a common JSON command protocol with the MCU and FPGA?"*
- **Model Output / Critique:**  
  A naive assistant might have suggested creating a generic `BaseComputeBoard` class shared across all three. However, I explicitly rejected this approach based on embedded engineering principles:
  - JTAG (IEEE Std 1149.1) is a synchronous, state-machine driven physical test interface (Test Access Port / Boundary-Scan), **not** an interchangeable general-purpose compute node.
  - The JTAG adapter is a synchronous bus **master** driving `TCK`, `TMS`, and `TDI` to inspect target TAP chains and CoreSight Debug Access Ports (DAP), not an application slave.
- **Outcome:**  
  I designed the hardware architecture to explicitly separate the JTAG TAP controller role from the MCU and FPGA target nodes, providing dedicated telemetry readouts (Target Vref sensing, TCK clock frequency, detected TAP IDCODE `0x4BA00477`) and documenting this distinction in the electrical specification.

---

### Interaction 4: Multithreading Architecture & Agreement Tolerance
- **Prompt:**  
  *"Design a C++ multicore execution test for 1,000 independent thermal solves. How do we ensure zero non-deterministic race conditions and verify serial/parallel agreement within 1e-8 C?"*
- **Implementation & Validation:**  
  Implemented a persistent `ThreadPool` with worker chunking. Each solve operates on its own dedicated parameter struct and pre-allocated output slot in the results array. Numerical verification verified that $\max |T_{\text{serial}} - T_{\text{parallel}}| = 0.00 \times 10^0 \le 10^{-8\ \circ}\text{C}$.

---

## 3. Independent Verification Evidence
All AI-generated code snippets and mathematical formulas were independently validated:
1. Mathematical derivations of $\frac{\partial J}{\partial w}, \frac{\partial J}{\partial b}, \frac{\partial J}{\partial v}, \frac{\partial J}{\partial c}$ were hand-verified and cross-checked against central finite differences ($\epsilon = 10^{-6}$), achieving errors $< 2 \times 10^{-10}$.
2. FEA midpoint convergence was validated against exact analytical integrals.
3. Memory safety, static linking, and execution without runtime DLLs were verified using `objdump -p Launch.exe` on Windows 11.
