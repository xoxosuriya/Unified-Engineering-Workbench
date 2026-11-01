# tests/smoke_routes.ps1
# Automates route extraction and smoke testing for Unified Engineering Workbench
$ErrorActionPreference = "Stop"

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " RUNNING SMOKE ROUTES VERIFICATION FOR WORKBENCH BACKEND        " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

# 1. Select a free local TCP port
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()

Write-Host "[INIT] Assigned dynamic test port: $port" -ForegroundColor Green

# 2. Start workbench_backend.exe
$exePath = Join-Path $PSScriptRoot "..\workbench_backend.exe"
if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $PSScriptRoot "..\build\workbench_backend.exe"
}
if (-not (Test-Path $exePath)) {
    throw "workbench_backend.exe not found at $exePath"
}

$webDir = Join-Path $PSScriptRoot "..\src\frontend"
$serverProc = Start-Process -FilePath $exePath -ArgumentList "--port", $port, "--web-dir", "`"$webDir`"" -PassThru -NoNewWindow

try {
    # 3. Wait for server readiness
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 200
        try {
            $resp = Invoke-WebRequest -Uri "http://127.0.0.1:$port/api/status" -TimeoutSec 1 -UseBasicParsing -ErrorAction SilentlyContinue
            if ($resp.StatusCode -eq 200) {
                $ready = $true
                break
            }
        } catch { }
    }

    if (-not $ready) {
        throw "Server failed to start or respond on port $port within 6 seconds."
    }
    Write-Host "[INIT] Server online and responsive on port $port.`n" -ForegroundColor Green

    # 4. Extract all /api/... routes from frontend code
    $frontendFiles = Get-ChildItem -Path (Join-Path $PSScriptRoot "..\src\frontend\js\*.js"), (Join-Path $PSScriptRoot "..\src\frontend\index.html")
    $extractedRoutes = @()

    foreach ($f in $frontendFiles) {
        $content = Get-Content $f.FullName -Raw
        $matches = [regex]::Matches($content, '(/api/[a-zA-Z0-9_\-]+(?:/[a-zA-Z0-9_\-]+)*)')
        foreach ($m in $matches) {
            $extractedRoutes += $m.Value
        }
    }

    $uniqueRoutes = $extractedRoutes | Sort-Object -Unique
    Write-Host "[INFO] Discovered $($uniqueRoutes.Count) unique frontend API endpoints:" -ForegroundColor Yellow
    $uniqueRoutes | ForEach-Object { Write-Host "   - $_" -ForegroundColor DarkGray }
    Write-Host ""

    $stlSample = "solid test`nfacet normal 0 0 1`nouter loop`nvertex 0 0 0`nvertex 1 0 0`nvertex 0 1 0`nendloop`nendfacet`nendsolid`n"
    $ansysSample = "node_id,x_m,y_m,z_m,temperature_C`n1,0.0,0.0,0.0,25.0`n2,0.01,0.0,0.0,30.0`n"

    $passed = 0
    $failed = 0

    foreach ($route in $uniqueRoutes) {
        if ($route -eq "/api/shutdown") {
            continue # Handled at end
        }

        $method = "GET"
        $body = $null
        $query = ""
        $contentType = "application/json"

        switch ($route) {
            "/api/stl/parse" {
                $method = "POST"
                $body = $stlSample
                $contentType = "text/plain"
            }
            "/api/ansys/parse" {
                $method = "POST"
                $body = $ansysSample
                $contentType = "text/plain"
            }
            "/api/fea/solve" {
                $query = "?L=1.0&A=0.0001&k=10.0&q=10000.0&T0=100.0&TL=20.0&N=10"
            }
            "/api/fea/convergence" {
                $query = "?L=1.0&A=0.0001&k=10.0&q=10000.0&T0=100.0&TL=20.0"
            }
            "/api/performance/run" {
                $query = "?cases=5&mesh=10&seed=42"
            }
            "/api/performance/cancel" {
                $method = "POST"
                $body = ""
            }
            "/api/hardware/connect" {
                $method = "POST"
                $body = "mcu_stm32h7"
                $contentType = "text/plain"
            }
            "/api/hardware/disconnect" {
                $method = "POST"
                $body = "mcu_stm32h7"
                $contentType = "text/plain"
            }
            "/api/hardware/telemetry" {
                $query = "?id=mcu_stm32h7"
            }
            "/api/hardware/inject_fault" {
                $method = "POST"
                $body = "mcu_stm32h7:timeout"
                $contentType = "text/plain"
            }
            "/api/hardware/recover" {
                $method = "POST"
                $body = "mcu_stm32h7"
                $contentType = "text/plain"
            }
            "/api/export/fea_csv" {
                $query = "?L=1.0&A=0.0001&k=10.0&q=10000.0&T0=100.0&TL=20.0&format=json"
            }
            "/api/export/fea_profile_csv" {
                $query = "?L=1.0&A=0.0001&k=10.0&q=10000.0&T0=100.0&TL=20.0&format=json"
            }
            "/api/export/performance_csv" {
                $query = "?cases=5&mesh=10&seed=42&format=json"
            }
            "/api/export/backprop_csv" {
                $query = "?format=json"
            }
            default { }
        }

        $fullUrl = "http://127.0.0.1:$port$route$query"
        try {
            if ($method -eq "GET") {
                $response = Invoke-WebRequest -Uri $fullUrl -Method GET -UseBasicParsing -TimeoutSec 10
            } else {
                $response = Invoke-WebRequest -Uri $fullUrl -Method POST -Body $body -ContentType $contentType -UseBasicParsing -TimeoutSec 10
            }

            if ($response.StatusCode -eq 404) {
                Write-Host "  [FAIL] $method $route -> HTTP 404 Not Found" -ForegroundColor Red
                $failed++
                continue
            }

            # Check if body is valid JSON
            $jsonObj = $response.Content | ConvertFrom-Json
            if ($null -eq $jsonObj) {
                Write-Host "  [FAIL] $method $route -> Non-JSON or null output" -ForegroundColor Red
                $failed++
            } else {
                Write-Host "  [PASS] $method $route -> HTTP $($response.StatusCode) (valid JSON)" -ForegroundColor Green
                $passed++
            }
        } catch {
            Write-Host "  [FAIL] $method $route -> Exception: $($_.Exception.Message)" -ForegroundColor Red
            $failed++
        }
    }

    # Test /api/shutdown endpoint
    try {
        $shutResp = Invoke-WebRequest -Uri "http://127.0.0.1:$port/api/shutdown" -Method POST -UseBasicParsing -TimeoutSec 5
        $jsonObj = $shutResp.Content | ConvertFrom-Json
        if ($shutResp.StatusCode -ne 404 -and $null -ne $jsonObj) {
            Write-Host "  [PASS] POST /api/shutdown -> HTTP $($shutResp.StatusCode) (valid JSON)" -ForegroundColor Green
            $passed++
        } else {
            Write-Host "  [FAIL] POST /api/shutdown -> Non-JSON or 404" -ForegroundColor Red
            $failed++
        }
    } catch {
        Write-Host "  [FAIL] POST /api/shutdown -> Exception: $($_.Exception.Message)" -ForegroundColor Red
        $failed++
    }

    $summaryColor = if ($failed -eq 0) { "Green" } else { "Red" }
    Write-Host "`n=================================================================" -ForegroundColor Cyan
    Write-Host " Smoke Test Summary: $passed Passed, $failed Failed" -ForegroundColor $summaryColor
    Write-Host "=================================================================" -ForegroundColor Cyan

    if ($failed -gt 0) {
        exit 1
    }
} finally {
    if ($serverProc -and -not $serverProc.HasExited) {
        Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
    }
}
