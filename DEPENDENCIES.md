# Software Dependencies & Third-Party Licenses
**Unified Engineering Workbench**  
**Candidate ID: RA2311047010144 — Pass Science Works R&D Internship 2026-2027**

All third-party frontend libraries are bundled locally in `src/frontend/vendor/` to guarantee 100% offline functionality without internet or external CDN dependencies.

---

## 1. Dependency Inventory

| Component | Version | Source / Vendor | License | Usage in Workbench |
| :--- | :--- | :--- | :--- | :--- |
| **Three.js** | r128 | mrdoob / three.js | MIT License | WebGL 3D rendering of STL bracket mesh and ANSYS thermal point clouds |
| **OrbitControls** | r128 | mrdoob / three.js | MIT License | Interactive 3D camera pan, rotate, and zoom with damping |
| **Chart.js** | 4.4.7 | Chart.js Authors | MIT License | 2D canvas charting for FEA profiles, convergence bars, and multicore speedup |
| **C++ Standard Library** | ISO C++17 | GCC / MinGW-w64 | Boost / Apache 2.0 / Runtime Exception | Numerical algorithms (FEA, Thomas TDMA, PRNG, threads, atomics) |
| **Windows Sockets 2** | Winsock 2.2 | Microsoft Windows SDK (`ws2_32`) | Commercial OS System Library | Localhost TCP HTTP networking for `Launch.exe` and `workbench_backend.exe` |

---

## 2. Third-Party MIT License Texts

### Three.js & OrbitControls (r128)
```
The MIT License

Copyright © 2010-2021 three.js authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

### Chart.js (v4.4.7)
```
The MIT License (MIT)

Copyright (c) 2014-2024 Chart.js Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## 3. Toolchain & Build Requirements (Optional for Source Recompilation)
The delivered executable `Launch.exe` is completely standalone and requires zero prerequisites. For developers compiling from source:
- **CMake**: >= 3.20
- **C++ Compiler**: GCC >= 11 or MSVC >= 2019 with C++17 support
- **Platform**: Microsoft Windows 10/11 x64
