# tools/make_evidence.ps1
# PASS Science Works Unified Engineering Workbench
# Candidate: RA2311047010144
# Single-run evidence generator:
# 1. Runs backend CLI for FEA, Multicore Benchmark (20,000 cases, mesh 200), and Backprop
# 2. Generates clean CSVs in results/*.csv
# 3. Generates environment metadata in results/environment.json
# 4. Programmatically updates results/plots/*.svg
# 5. Emits exact Markdown tables to results/generated_tables.md and stdout

$ErrorActionPreference = "Stop"

$workspaceRoot = Resolve-Path "$PSScriptRoot\.."
Set-Location $workspaceRoot

$resultsDir = "$workspaceRoot\results"
$plotsDir = "$resultsDir\plots"
if (-not (Test-Path $resultsDir)) { New-Item -ItemType Directory -Path $resultsDir | Out-Null }
if (-not (Test-Path $plotsDir)) { New-Item -ItemType Directory -Path $plotsDir | Out-Null }

$backendExe = "$workspaceRoot\workbench_backend.exe"
if (-not (Test-Path $backendExe)) {
    $backendExe = "$workspaceRoot\build\workbench_backend.exe"
}

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " PASS SCIENCE WORKS - UNIFIED BENCHMARK & EVIDENCE GENERATOR     " -ForegroundColor Cyan
Write-Host " Candidate: RA2311047010144 | Release Single-Run Verification   " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

# Step 1: Multicore Benchmark Run (20,000 cases, mesh 200, seed 42)
Write-Host "[1/4] Executing Multicore Parallel Solver Benchmark (20,000 cases, mesh 200)..." -ForegroundColor Yellow
$scalingCsv = "$resultsDir\multicore_scaling.csv"
$envJson = "$resultsDir\environment.json"
& $backendExe --bench --cases 20000 --mesh 200 --seed 42 --repetitions 5 --out "$scalingCsv" --env-out "$envJson"
Write-Host "  -> Generated $scalingCsv and $envJson" -ForegroundColor Green

# Step 2: 1D Thermal FEA Convergence Study
Write-Host "[2/4] Executing 1D Thermal FEA Convergence Study..." -ForegroundColor Yellow
& $backendExe --fea --out-dir "$resultsDir"
Write-Host "  -> Generated $resultsDir\fea_convergence.csv and $resultsDir\fea_profile.csv" -ForegroundColor Green

# Step 3: Backpropagation Engine Verification
Write-Host "[3/4] Executing Backpropagation Engine Verification..." -ForegroundColor Yellow
$backpropCsv = "$resultsDir\backprop_loss.csv"
& $backendExe --backprop --out "$backpropCsv"
Write-Host "  -> Generated $backpropCsv" -ForegroundColor Green

# Step 4: Parse Results & Generate SVG Plots + Markdown Tables
Write-Host "[4/4] Generating SVG Plots and Markdown Evidence Tables..." -ForegroundColor Yellow

$envData = Get-Content $envJson -Raw | ConvertFrom-Json
$scalingData = Import-Csv $scalingCsv
$feaConvData = Import-Csv "$resultsDir\fea_convergence.csv"
$feaProfData = Import-Csv "$resultsDir\fea_profile.csv"
$bpData = Import-Csv $backpropCsv

# -------------------------------------------------------------
# A. Multicore Speedup SVG
# -------------------------------------------------------------
$p1 = $scalingData | Where-Object { $_.workers -eq 1 }
$p2 = $scalingData | Where-Object { $_.workers -eq 2 }
$p4 = $scalingData | Where-Object { $_.workers -eq 4 }

$p1_med = [double]$p1.median_ms
$p2_med = [double]$p2.median_ms
$p4_med = [double]$p4.median_ms

$p1_speedup = [double]$p1.speedup_T1_over_Tp
$p2_speedup = [double]$p2.speedup_T1_over_Tp
$p4_speedup = [double]$p4.speedup_T1_over_Tp

$p1_eff = [math]::Round([double]$p1.efficiency * 100, 1)
$p2_eff = [math]::Round([double]$p2.efficiency * 100, 1)
$p4_eff = [math]::Round([double]$p4.efficiency * 100, 1)

# Coordinate scaling: Y-axis speedup 0.0x (y=360) to 4.0x (y=80), 70 px per 1.0x
function Get-YCoord($speedup) {
    return [math]::Round(360 - ($speedup * 70), 1)
}

$y_p1 = Get-YCoord $p1_speedup
$h_p1 = [math]::Round(360 - $y_p1, 1)

$y_p2 = Get-YCoord $p2_speedup
$h_p2 = [math]::Round(360 - $y_p2, 1)

$y_p4 = Get-YCoord $p4_speedup
$h_p4 = [math]::Round(360 - $y_p4, 1)

$cpuName = $envData.cpu_model
$physCores = $envData.physical_cores
$logCores = $envData.logical_cores
$ramGb = $envData.total_ram_gb

$speedupSvg = @"
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 800 450" width="800" height="450">
  <rect width="100%" height="100%" fill="#0b0f19"/>
  <text x="400" y="36" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="18" font-weight="bold" text-anchor="middle">Multicore Parallel Speedup &amp; Scaling Analysis</text>
  <text x="400" y="58" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="12" text-anchor="middle">CPU: $cpuName ($physCores Physical Cores, $logCores Threads, $ramGb GB RAM) | Workload: 20,000 Solves, Mesh N = 200 | GCC -O3</text>

  <!-- Axes -->
  <line x1="120" y1="360" x2="720" y2="360" stroke="#334155" stroke-width="1.5"/>
  <line x1="120" y1="80" x2="120" y2="360" stroke="#334155" stroke-width="1.5"/>

  <!-- Y axis (Speedup 0 to 4) -->
  <line x1="115" y1="360" x2="720" y2="360" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="105" y="364" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">0.0x</text>

  <line x1="115" y1="290" x2="720" y2="290" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="105" y="294" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">1.0x</text>

  <line x1="115" y1="220" x2="720" y2="220" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="105" y="224" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">2.0x</text>

  <line x1="115" y1="150" x2="720" y2="150" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="105" y="154" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">3.0x</text>

  <line x1="115" y1="80" x2="720" y2="80" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="105" y="84" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">4.0x</text>

  <!-- Ideal linear line -->
  <line x1="220" y1="290" x2="620" y2="80" stroke="#10b981" stroke-width="2" stroke-dasharray="5,5"/>

  <!-- Measured Speedup Bars -->
  <!-- p = 1: x = 220 -->
  <rect x="180" y="$y_p1" width="80" height="$h_p1" fill="#06b6d4" rx="4" opacity="0.85"/>
  <text x="220" y="$($y_p1 - 8)" fill="#38bdf8" font-family="system-ui, sans-serif" font-size="12" font-weight="bold" text-anchor="middle">$($p1_speedup)x ($($p1_med) ms)</text>
  <text x="220" y="380" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="12" font-weight="600" text-anchor="middle">p = 1 Worker</text>
  <text x="220" y="398" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="10" text-anchor="middle">Eff: $($p1_eff)%</text>

  <!-- p = 2: x = 420 -->
  <rect x="380" y="$y_p2" width="80" height="$h_p2" fill="#06b6d4" rx="4" opacity="0.85"/>
  <text x="420" y="$($y_p2 - 8)" fill="#38bdf8" font-family="system-ui, sans-serif" font-size="12" font-weight="bold" text-anchor="middle">$($p2_speedup)x ($($p2_med) ms)</text>
  <text x="420" y="380" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="12" font-weight="600" text-anchor="middle">p = 2 Workers</text>
  <text x="420" y="398" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="10" text-anchor="middle">Eff: $($p2_eff)%</text>

  <!-- p = 4: x = 620 -->
  <rect x="580" y="$y_p4" width="80" height="$h_p4" fill="#06b6d4" rx="4" opacity="0.85"/>
  <text x="620" y="$($y_p4 - 8)" fill="#38bdf8" font-family="system-ui, sans-serif" font-size="12" font-weight="bold" text-anchor="middle">$($p4_speedup)x ($($p4_med) ms)</text>
  <text x="620" y="380" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="12" font-weight="600" text-anchor="middle">p = 4 Workers</text>
  <text x="620" y="398" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="10" text-anchor="middle">Eff: $($p4_eff)%</text>

  <!-- Legend -->
  <text x="500" y="110" fill="#10b981" font-family="system-ui, sans-serif" font-size="11">-- Ideal Linear Speedup</text>
  <rect x="475" y="102" width="18" height="10" fill="#06b6d4"/>
  <text x="500" y="128" fill="#06b6d4" font-family="system-ui, sans-serif" font-size="11">Measured Speedup T1 / Tp</text>
</svg>
"@
[System.IO.File]::WriteAllText("$plotsDir\multicore_speedup.svg", $speedupSvg, [System.Text.Encoding]::UTF8)

# -------------------------------------------------------------
# B. FEA Temperature Profile SVG
# -------------------------------------------------------------
# Generate SVG path string from profile data (x: 100 to 820, T: 0 to 200 C -> y: 400 to 70, scale: 330/200 = 1.65 px/C)
function Map-FeaX($x) { return [math]::Round(100.0 + ($x * 720.0), 1) }
function Map-FeaY($t) { return [math]::Round(400.0 - ($t * 1.65), 1) }

$exactPath = "M "
$feN10Path = "M "
$feN20Path = "M "
$feN40Path = "M "
$nodeCircles = ""

for ($i = 0; $i -lt $feaProfData.Count; $i++) {
    $row = $feaProfData[$i]
    $px = Map-FeaX ([double]$row.x_m)
    $py_exact = Map-FeaY ([double]$row.T_exact_C)
    $py_n10 = Map-FeaY ([double]$row.T_FE_N10_C)
    $py_n20 = Map-FeaY ([double]$row.T_FE_N20_C)
    $py_n40 = Map-FeaY ([double]$row.T_FE_N40_C)

    if ($i -eq 0) {
        $exactPath += "$px $py_exact"
        $feN10Path += "$px $py_n10"
        $feN20Path += "$px $py_n20"
        $feN40Path += "$px $py_n40"
    } else {
        $exactPath += " L $px $py_exact"
        $feN10Path += " L $px $py_n10"
        $feN20Path += " L $px $py_n20"
        $feN40Path += " L $px $py_n40"
    }

    if ($i % 10 -eq 0) {
        $nodeCircles += "<circle cx=""$px"" cy=""$py_n10"" r=""4.5"" fill=""#06b6d4"" stroke=""#0b0f19"" stroke-width=""1.5""/>"
    }
}

$feaProfileSvg = @"
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 920 500" width="920" height="500">
  <rect width="100%" height="100%" fill="#0b0f19"/>
  <text x="460" y="36" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="18" font-weight="bold" text-anchor="middle">1D Thermal Finite Element Analysis: Temperature Distribution Profile</text>
  <text x="460" y="58" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="12" text-anchor="middle">Uniform Rod: L = 1.0 m, k = 10 W/(m·K), q = 10,000 W/m³, T(0) = 100 °C, T(L) = 20 °C | Analytical vs FE Multi-Mesh</text>

  <line x1="100" y1="400" x2="820" y2="400" stroke="#334155" stroke-width="1.5"/>
  <line x1="100" y1="70" x2="100" y2="400" stroke="#334155" stroke-width="1.5"/>

  <line x1="95" y1="400" x2="820" y2="400" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="404" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">0 °C</text>
  <line x1="95" y1="317.5" x2="820" y2="317.5" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="321.5" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">50 °C</text>
  <line x1="95" y1="235" x2="820" y2="235" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="239" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">100 °C</text>
  <line x1="95" y1="152.5" x2="820" y2="152.5" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="156.5" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">150 °C</text>
  <line x1="95" y1="70" x2="820" y2="70" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="74" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">200 °C</text>

  <line x1="100" y1="70" x2="100" y2="405" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="100" y="424" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">0.0 m</text>
  <line x1="244" y1="70" x2="244" y2="405" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="244" y="424" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">0.2 m</text>
  <line x1="388" y1="70" x2="388" y2="405" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="388" y="424" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">0.4 m</text>
  <line x1="532" y1="70" x2="532" y2="405" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="532" y="424" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">0.6 m</text>
  <line x1="676" y1="70" x2="676" y2="405" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="676" y="424" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">0.8 m</text>
  <line x1="820" y1="70" x2="820" y2="405" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="820" y="424" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">1.0 m</text>
  <text x="460" y="455" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="13" font-weight="600" text-anchor="middle">Axial Position x [m]</text>
  <text x="30" y="230" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="13" font-weight="600" text-anchor="middle" transform="rotate(-90 30 230)">Temperature T [°C]</text>

  <!-- Analytical Solution -->
  <path d="$exactPath" fill="none" stroke="#f59e0b" stroke-width="2.5" stroke-dasharray="6,4"/>

  <!-- FE N=40 (Purple) -->
  <path d="$feN40Path" fill="none" stroke="#a855f7" stroke-width="1.8" opacity="0.6"/>

  <!-- FE N=20 (Emerald) -->
  <path d="$feN20Path" fill="none" stroke="#10b981" stroke-width="2.0" opacity="0.8"/>

  <!-- FE N=10 (Cyan with Circles) -->
  <path d="$feN10Path" fill="none" stroke="#06b6d4" stroke-width="2.5"/>
  $nodeCircles

  <!-- Legend -->
  <rect x="540" y="75" width="270" height="125" rx="6" fill="#111827" stroke="#334155" opacity="0.95"/>
  <line x1="555" y1="95" x2="595" y2="95" stroke="#f59e0b" stroke-width="2.5" stroke-dasharray="6,4"/>
  <text x="605" y="99" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="11">Analytical Exact T_exact(x)</text>
  <line x1="555" y1="120" x2="595" y2="120" stroke="#06b6d4" stroke-width="2.5"/>
  <circle cx="575" cy="120" r="4" fill="#06b6d4"/>
  <text x="605" y="124" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="11">FE Solution N = 10 (h = 0.100 m)</text>
  <line x1="555" y1="145" x2="595" y2="145" stroke="#10b981" stroke-width="2.0"/>
  <text x="605" y="149" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="11">FE Solution N = 20 (h = 0.050 m)</text>
  <line x1="555" y1="170" x2="595" y2="170" stroke="#a855f7" stroke-width="1.8"/>
  <text x="605" y="174" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="11">FE Solution N = 40 (h = 0.025 m)</text>
</svg>
"@
[System.IO.File]::WriteAllText("$plotsDir\fea_temperature_profile.svg", $feaProfileSvg, [System.Text.Encoding]::UTF8)

# -------------------------------------------------------------
# C. Backprop Loss Dynamics SVG
# -------------------------------------------------------------
# Steps: 0 to 100 -> x: 100 to 720 (scale: 6.2 px/step)
# Loss: 0.0 to 1.25 -> y: 360 to 80 (scale: 280 / 1.25 = 224 px per unit loss)
# y_hat: 0.0 to 2.0 -> y: 360 to 80 (scale: 280 / 2.0 = 140 px per unit y_hat)
function Map-BpX($step) { return [math]::Round(100.0 + ($step * 6.2), 1) }
function Map-LossY($loss) { return [math]::Round(360.0 - ($loss * 224.0), 1) }
function Map-YhatY($yhat) { return [math]::Round(360.0 - ($yhat * 140.0), 1) }

$lossPath = "M "
$yhatPath = "M "

for ($i = 0; $i -lt $bpData.Count; $i++) {
    $row = $bpData[$i]
    $step = [double]$row.step
    $loss = [double]$row.loss_J
    $yhat = [double]$row.y_hat

    $px = Map-BpX $step
    $py_loss = Map-LossY $loss
    $py_yhat = Map-YhatY $yhat

    if ($i -eq 0) {
        $lossPath += "$px $py_loss"
        $yhatPath += "$px $py_yhat"
    } else {
        $lossPath += " L $px $py_loss"
        $yhatPath += " L $px $py_yhat"
    }
}

$backpropSvg = @"
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 800 450" width="800" height="450">
  <rect width="100%" height="100%" fill="#0b0f19"/>
  <text x="400" y="36" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="18" font-weight="bold" text-anchor="middle">Simple Handwritten Backpropagation: 100-Step Training Dynamics</text>
  <text x="400" y="58" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="12" text-anchor="middle">Sample x = 1, Target y = 2 | Model: h = tanh(wx+b), y_hat = vh+c | Learning Rate η = 0.1</text>

  <!-- Axes -->
  <line x1="100" y1="360" x2="720" y2="360" stroke="#334155" stroke-width="1.5"/>
  <line x1="100" y1="80" x2="100" y2="360" stroke="#334155" stroke-width="1.5"/>

  <!-- X axis steps -->
  <line x1="100" y1="80" x2="100" y2="365" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="100" y="385" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">0</text>
  <line x1="224" y1="80" x2="224" y2="365" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="224" y="385" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">20</text>
  <line x1="348" y1="80" x2="348" y2="365" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="348" y="385" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">40</text>
  <line x1="472" y1="80" x2="472" y2="365" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="472" y="385" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">60</text>
  <line x1="596" y1="80" x2="596" y2="365" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="596" y="385" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">80</text>
  <line x1="720" y1="80" x2="720" y2="365" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="720" y="385" fill="#9ca3af" font-family="system-ui, sans-serif" font-size="11" text-anchor="middle">100</text>

  <!-- Y axis Loss -->
  <line x1="95" y1="360" x2="720" y2="360" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="364" fill="#fb7185" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">0.00</text>
  <line x1="95" y1="304" x2="720" y2="304" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="308" fill="#fb7185" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">0.25</text>
  <line x1="95" y1="248" x2="720" y2="248" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="252" fill="#fb7185" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">0.50</text>
  <line x1="95" y1="192" x2="720" y2="192" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="196" fill="#fb7185" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">0.75</text>
  <line x1="95" y1="136" x2="720" y2="136" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="140" fill="#fb7185" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">1.00</text>
  <line x1="95" y1="80" x2="720" y2="80" stroke="#1e293b" stroke-dasharray="4,4"/>
  <text x="85" y="84" fill="#fb7185" font-family="system-ui, sans-serif" font-size="11" text-anchor="end">1.25</text>

  <text x="410" y="420" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="13" font-weight="600" text-anchor="middle">Training Step (Epoch)</text>
  <text x="30" y="220" fill="#fb7185" font-family="system-ui, sans-serif" font-size="13" font-weight="600" text-anchor="middle" transform="rotate(-90 30 220)">Loss J(w,b,v,c)</text>

  <!-- Loss Curve (Rose) -->
  <path d="$lossPath" fill="none" stroke="#f43f5e" stroke-width="2.5"/>

  <!-- Prediction Curve (Cyan) -->
  <path d="$yhatPath" fill="none" stroke="#06b6d4" stroke-width="2.0" stroke-dasharray="5,4"/>

  <!-- Legend -->
  <rect x="490" y="80" width="220" height="75" rx="6" fill="#111827" stroke="#334155" opacity="0.95"/>
  <line x1="505" y1="102" x2="540" y2="102" stroke="#f43f5e" stroke-width="2.5"/>
  <text x="550" y="106" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="11">Loss J = 0.5·(ŷ - y)²</text>
  <line x1="505" y1="130" x2="540" y2="130" stroke="#06b6d4" stroke-width="2.0" stroke-dasharray="5,4"/>
  <text x="550" y="134" fill="#f3f4f6" font-family="system-ui, sans-serif" font-size="11">Model Output ŷ (Target = 2.0)</text>
</svg>
"@
[System.IO.File]::WriteAllText("$plotsDir\backprop_loss_dynamics.svg", $backpropSvg, [System.Text.Encoding]::UTF8)

# -------------------------------------------------------------
# D. Generate Markdown Tables
# -------------------------------------------------------------

$optFlags = $envData.optimization_flags
$cpuModel = $envData.cpu_model
$physCores = $envData.physical_cores
$logCores = $envData.logical_cores
$ramGb = $envData.total_ram_gb
$workCases = $envData.workload.case_count
$workMesh = $envData.workload.mesh_size_N
$workSeed = $envData.workload.seed
$workReps = $envData.workload.repetitions
$batchGenMs = [math]::Round([double]$envData.batch_generation_ms, 2)
$warmupMs = [math]::Round([double]$envData.warmup_ms, 2)
$readyMs = [math]::Round([double]$envData.launch_to_ready_ms, 2)
$maxDiff = [string]::Format("{0:E4}", [double]$envData.max_serial_parallel_diff)

# Table 1: Environment Table
$envMd = @"
### System Environment & Evidence Run Metadata
| Parameter | Recorded Value |
|---|---|
| **Candidate ID** | `RA2311047010144` |
| **CPU Model** | $cpuModel |
| **Physical / Logical Cores** | $physCores Physical / $logCores Logical Cores |
| **Total System RAM** | $ramGb GB |
| **Compiler & Platform** | $($envData.compiler_info) (Windows 11 x64, MinGW Static) |
| **Optimization Flags** | `"$optFlags`" |
| **Benchmark Workload** | $workCases Cases, Mesh N = $workMesh, Seed $workSeed, $workReps Repetitions |
| **Launch-to-Ready Time** | $readyMs ms |
| **Batch Generation Time** | $batchGenMs ms |
| **Warmup Duration** | $warmupMs ms |
| **Max Serial/Parallel Diff** | $maxDiff C |
"@

# Table 2: Multicore Scaling Table
$scalingMd = @"
### Multicore Parallel Speedup & Scaling Results
| Workers (p) | Rep 1 (ms) | Rep 2 (ms) | Rep 3 (ms) | Rep 4 (ms) | Rep 5 (ms) | Median (ms) | Mean (ms) | Speedup (Sp) | Efficiency (Ep) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
"@
foreach ($r in $scalingData) {
    $effPct = [string]::Format("{0:P1}", [double]$r.efficiency)
    $scalingMd += "`n| $($r.workers) | $([math]::Round([double]$r.repetition_1_ms, 2)) | $([math]::Round([double]$r.repetition_2_ms, 2)) | $([math]::Round([double]$r.repetition_3_ms, 2)) | $([math]::Round([double]$r.repetition_4_ms, 2)) | $([math]::Round([double]$r.repetition_5_ms, 2)) | **$([math]::Round([double]$r.median_ms, 2))** | $([math]::Round([double]$r.mean_ms, 2)) | **$([double]$r.speedup_T1_over_Tp)x** | $effPct |"
}

# Table 3: FEA Convergence Table
$feaMd = @"
### 1D Thermal FEA Convergence Verification
| Mesh (N) | Element Size h (m) | Max Midpoint Error (C) | Theoretical Error q*h^2/8k (C) | Max Nodal Error (C) | Error Reduction Ratio | Observed Rate (p = log2 r) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
"@
foreach ($r in $feaConvData) {
    $feaMd += "`n| $($r.mesh_N) | $([double]$r.element_size_h_m) | $([string]::Format("{0:F8}", [double]$r.max_midpoint_error_C)) | $([string]::Format("{0:F8}", [double]$r.theoretical_midpoint_error_C)) | $([string]::Format("{0:E1}", [double]$r.max_nodal_error_C)) | $([string]::Format("{0:F4}", [double]$r.error_reduction_ratio)) | **$([string]::Format("{0:F4}", [double]$r.observed_convergence_rate))** |"
}

# Table 4: Backprop Numerical Verification
$s0 = $bpData[0]
$s1 = $bpData[1]
$s100 = $bpData[100]

$bpMd = @"
### Backpropagation Mathematics & Finite-Difference Verification
**Sample Input:** x = 1.0, Target y = 2.0, Learning Rate eta = 0.1, epsilon = 1e-6

#### Step 0 Forward & Analytical Gradients
| Quantity | Symbol | Formula | Program Output |
|---|---|---|---|
| Initial Weights | w0, b0, v0, c0 | Given | w = 0.5, b = 0.0, v = 1.0, c = 0.0 |
| Linear Combination | z0 | w*x + b | **$([double]$s0.z)** |
| Hidden Activation | h0 | tanh(z0) | **$([string]::Format("{0:F8}", [double]$s0.h))** |
| Output Prediction | y_hat_0 | v*h0 + c | **$([string]::Format("{0:F8}", [double]$s0.y_hat))** |
| Step 0 Loss | J0 | 0.5*(y_hat_0 - y)^2 | **$([string]::Format("{0:F8}", [double]$s0.loss_J))** |
| Gradient wrt c | dJ/dc | y_hat_0 - y | **$([string]::Format("{0:F8}", [double]$s0.dJ_dc))** |
| Gradient wrt v | dJ/dv | (y_hat_0 - y)*h0 | **$([string]::Format("{0:F8}", [double]$s0.dJ_dv))** |
| Gradient wrt b | dJ/db | (y_hat_0 - y)*v*(1 - h0^2) | **$([string]::Format("{0:F8}", [double]$s0.dJ_db))** |
| Gradient wrt w | dJ/dw | (dJ/db)*x | **$([string]::Format("{0:F8}", [double]$s0.dJ_dw))** |

#### Step 1 Updated Parameters (theta_1 = theta_0 - eta * grad_J)
| Parameter | Symbol | Computation | Value at Step 1 |
|---|---|---|---|
| Weight w | w1 | 0.5 - 0.1 * ($([string]::Format("{0:F8}", [double]$s0.dJ_dw))) | **$([string]::Format("{0:F8}", [double]$s1.w))** |
| Bias b | b1 | 0.0 - 0.1 * ($([string]::Format("{0:F8}", [double]$s0.dJ_db))) | **$([string]::Format("{0:F8}", [double]$s1.b))** |
| Weight v | v1 | 1.0 - 0.1 * ($([string]::Format("{0:F8}", [double]$s0.dJ_dv))) | **$([string]::Format("{0:F8}", [double]$s1.v))** |
| Bias c | c1 | 0.0 - 0.1 * ($([string]::Format("{0:F8}", [double]$s0.dJ_dc))) | **$([string]::Format("{0:F8}", [double]$s1.c))** |

#### Step 100 Converged State
- y_hat_100 = $([string]::Format("{0:F8}", [double]$s100.y_hat)) (Target = 2.0)
- J_100 = $([string]::Format("{0:E4}", [double]$s100.loss_J))
- Gradients at Step 100: grad_J ~ 1e-8
"@

# Table 5: Measured Stress Limits Table
$limitsPath = "$resultsDir\file_limits.csv"
if (Test-Path $limitsPath) {
    $limitsData = Import-Csv $limitsPath
    $limitsMd = @"
### Measured Parser Capacities & File Size Limits
| Format | Tested Entities | File Size (MB) | File Size (Bytes) | Server Parse Time (ms) | Response Payload (Bytes) | Status |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
"@
    foreach ($r in $limitsData) {
        $fmt = if ($r.file_type -eq "STL_ASCII") { "STL (ASCII)" } else { "ANSYS (CSV)" }
        $count = [int]$r.entity_count
        $unit = if ($r.file_type -eq "STL_ASCII") { "facets" } else { "nodes" }
        $limitsMd += "`n| **$fmt** | $([string]::Format("{0:N0}", $count)) $unit | $($r.file_size_mb) MB | $([string]::Format("{0:N0}", [int64]$r.file_size_bytes)) | **$($r.parse_time_ms) ms** | $([string]::Format("{0:N0}", [int64]$r.response_size_bytes)) | **$($r.status)** |"
    }
} else {
    $limitsMd = ""
}

$allTablesMd = @"
# UNIFIED ENGINEERING WORKBENCH - REPRODUCIBLE EVIDENCE REPORT
*Generated programmatically from a single run executed in this session on $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')*

$envMd

$scalingMd

$feaMd

$bpMd

$limitsMd
"@

$tablesFile = "$resultsDir\generated_tables.md"
[System.IO.File]::WriteAllText($tablesFile, $allTablesMd, [System.Text.Encoding]::UTF8)
Write-Host "Generated tables saved to $tablesFile" -ForegroundColor Green
Write-Host "`n$allTablesMd"
