# Dataset Provenance and Validation Specification

This document records the provenance, coordinate systems, dimensional units, schema definitions, and validation limits for the datasets supplied with the **Unified Engineering Workbench** in accordance with **Task B** requirements.

---

## 1. Geometry Data: `data/sample_bracket.stl`

- **Filename**: `sample_bracket.stl`
- **Format**: Standard ASCII StereoLithography (`.stl`)
- **Solid Identifier**: `mechanical_mounting_bracket`
- **Provenance / Origin**: Self-created analytical CAD model representing a structural right-angle mounting bracket with base mounting plate, upright structural flange, and dual triangular stiffener gusset ribs.
- **Dimensional Units**: Meters ($m$)
- **Coordinate Reference Frame**:
  - Origin $(0, 0, 0)$ is located at the intersection of the outer rear corner of the base and upright flange.
  - $X$-axis: Extends horizontally along the base plate ($0.01\,m \le x \le 0.10\,m$).
  - $Y$-axis: Extends along bracket width ($0.00\,m \le y \le 0.08\,m$).
  - $Z$-axis: Extends vertically along upright flange ($0.00\,m \le z \le 0.10\,m$).
- **Geometry Characteristics**:
  - Triangles: 32 planar triangular facets
  - Total Surface Area: $0.0526\,m^2$
  - Bounding Box: Min $[0.00, 0.00, 0.00]\,m$, Max $[0.10, 0.08, 0.10]\,m$
  - Facet Normals: Outward-pointing unit vectors satisfying right-hand rule vertex ordering.
- **License**: Creative Commons Zero (CC0 1.0 Universal - Public Domain Dedication).
- **Tested File-Size Limits**: Tested up to 50 MB / 500,000 triangles.

---

## 2. Nodal Temperature Data: `data/ansys_thermal_synthetic.csv`

- **Filename**: `ansys_thermal_synthetic.csv`
- **Format**: Comma-Separated Values (`.csv`)
- **Provenance & Synthetic Labeling Disclosure**:
  > **DISCLOSURE**: This file is **SYNTHETIC** and was generated mathematically using a conduction-convection boundary value model to simulate the nodal temperature distribution across the bracket geometry. It is labeled as synthetic and is **NOT** represented as an actual proprietary ANSYS project/result database.
- **Columns & Schema**:
  1. `node_id` (Integer): Positive unique identifier ($1, 2, \dots, N$).
  2. `x_m` (Float): Node position along $X$-axis in meters ($m$).
  3. `y_m` (Float): Node position along $Y$-axis in meters ($m$).
  4. `z_m` (Float): Node position along $Z$-axis in meters ($m$).
  5. `temperature_C` (Float): Steady-state nodal temperature in degrees Celsius ($^\circ\text{C}$).
- **Physical Modeling Parameters Simulated**:
  - Base mounting edge boundary condition: Prescribed heat source contact ($100.0^\circ\text{C}$).
  - Convective heat transfer coefficient to ambient air ($T_{\infty} = 25.0^\circ\text{C}$) on flange faces.
  - Temperature Range: Min $25.00^\circ\text{C}$, Max $103.88^\circ\text{C}$, Average $43.21^\circ\text{C}$.
  - Total Nodes: 3,485 nodes.
- **Tested File-Size Limits**: Tested up to 50 MB / 500,000 nodes.

---

## 3. Negative Test Cases: `data/test_samples/`

The repository supplies negative test specimens to demonstrate robust error handling and actionable diagnostics:

1. `empty.stl` & `empty.csv`: 0-byte files, rejected with actionable message: *"File is empty (0 bytes)"*.
2. `corrupted_syntax.stl`: Missing `outer loop` syntax block, rejected with exact line number identification.
3. `nan_coords.stl`: Coordinates containing `NaN`, rejected with explicit notification: *"Vertex coordinate contains NaN or Infinite value"*.
4. `missing_columns.csv`: Incomplete token count (4 columns instead of 5), rejected with line-specific token mismatch error.
5. `sub_absolute_zero.csv`: Temperature value $-350.0^\circ\text{C}$, rejected by physical validator: *"Temperature is below absolute zero (-273.15 C)"*.
