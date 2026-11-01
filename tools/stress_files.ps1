# tools/stress_files.ps1
# Generates stress STL files (50k, 180k, 270k facets) and ANSYS CSV (500k nodes),
# sends them to a running workbench_backend server, and records real file sizes,
# parse times, and response sizes into results/file_limits.csv.

$ErrorActionPreference = "Stop"

$workspaceRoot = Resolve-Path "$PSScriptRoot\.."
Set-Location $workspaceRoot

$scratchDir = "$workspaceRoot\scratch"
if (-not (Test-Path $scratchDir)) {
    New-Item -ItemType Directory -Path $scratchDir | Out-Null
}
$resultsDir = "$workspaceRoot\results"
if (-not (Test-Path $resultsDir)) {
    New-Item -ItemType Directory -Path $resultsDir | Out-Null
}

$port = 9091
$backendExe = "$workspaceRoot\workbench_backend.exe"
if (-not (Test-Path $backendExe)) {
    $backendExe = "$workspaceRoot\build\workbench_backend.exe"
}

Write-Host "Starting workbench_backend on port $port..." -ForegroundColor Cyan
$serverProc = Start-Process -FilePath $backendExe -ArgumentList "--port $port" -PassThru -WindowStyle Hidden

try {
    # Wait for server readiness
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 200
        try {
            $resp = Invoke-RestMethod -Uri "http://127.0.0.1:$port/api/status" -TimeoutSec 2 -ErrorAction Stop
            if ($resp.status -eq "ONLINE") {
                $ready = $true
                break
            }
        } catch { }
    }

    if (-not $ready) {
        throw "Server failed to start on port $port."
    }
    Write-Host "Server is ONLINE on port $port." -ForegroundColor Green

    $records = [System.Collections.Generic.List[PSObject]]::new()

    # Function to generate synthetic STL with N facets
    function New-StressStl($filePath, $facetCount) {
        Write-Host "Generating STL with $facetCount facets at $filePath..." -ForegroundColor Yellow
        $sw = [System.IO.StreamWriter]::new($filePath, $false, [System.Text.Encoding]::ASCII, 65536)
        $sw.WriteLine("solid stress_test_$facetCount")
        for ($i = 0; $i -lt $facetCount; $i++) {
            $v1 = [double]($i % 100) * 0.01
            $v2 = [double]($i % 100) * 0.01 + 0.01
            $sw.WriteLine("  facet normal 0.0 0.0 1.0")
            $sw.WriteLine("    outer loop")
            $sw.WriteLine("      vertex $v1 0.0 0.0")
            $sw.WriteLine("      vertex $v2 0.0 0.0")
            $sw.WriteLine("      vertex $v1 0.01 0.0")
            $sw.WriteLine("    endloop")
            $sw.WriteLine("  endfacet")
        }
        $sw.WriteLine("endsolid stress_test_$facetCount")
        $sw.Close()
    }

    # Function to generate synthetic ANSYS CSV with N nodes
    function New-StressCsv($filePath, $nodeCount) {
        Write-Host "Generating ANSYS CSV with $nodeCount nodes at $filePath..." -ForegroundColor Yellow
        $sw = [System.IO.StreamWriter]::new($filePath, $false, [System.Text.Encoding]::ASCII, 65536)
        $sw.WriteLine("node_id,x_m,y_m,z_m,temperature_C")
        for ($i = 1; $i -le $nodeCount; $i++) {
            $x = ($i % 1000) * 0.001
            $y = (($i / 1000) % 1000) * 0.001
            $z = 0.0
            $temp = 20.0 + ($i % 300) * 0.5
            $sw.WriteLine("$i,$x,$y,$z,$temp")
        }
        $sw.Close()
    }

    # 1. STL Tests: 50k, 180k, 270k facets
    $stlTests = @(50000, 180000, 270000)
    foreach ($count in $stlTests) {
        $stlPath = "$scratchDir\stress_$($count)_facets.stl"
        New-StressStl $stlPath $count

        $fileBytes = [System.IO.File]::ReadAllBytes($stlPath)
        $fileSizeMB = [math]::Round($fileBytes.Length / (1024 * 1024), 2)
        $fileSizeBytes = $fileBytes.Length

        Write-Host "Sending $count facets ($fileSizeMB MB) to /api/stl/parse..." -ForegroundColor Cyan
        $swWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $httpReq = [System.Net.HttpWebRequest]::Create("http://127.0.0.1:$port/api/stl/parse")
        $httpReq.Method = "POST"
        $httpReq.ContentType = "text/plain"
        $httpReq.Headers["Origin"] = "http://127.0.0.1:$port"
        $httpReq.ContentLength = $fileBytes.Length
        $reqStream = $httpReq.GetRequestStream()
        $reqStream.Write($fileBytes, 0, $fileBytes.Length)
        $reqStream.Close()

        $httpResp = $httpReq.GetResponse()
        $respStream = $httpResp.GetResponseStream()
        $ms = [System.IO.MemoryStream]::new()
        $respStream.CopyTo($ms)
        $swWatch.Stop()

        $parseTimeMs = [math]::Round($swWatch.Elapsed.TotalMilliseconds, 2)
        $respBytes = $ms.Length
        $httpResp.Close()

        Write-Host "  -> Parsed in $parseTimeMs ms (Response: $respBytes bytes)" -ForegroundColor Green

        $records.Add([PSCustomObject]@{
            file_type = "STL_ASCII"
            entity_count = $count
            file_size_mb = $fileSizeMB
            file_size_bytes = $fileSizeBytes
            parse_time_ms = $parseTimeMs
            response_size_bytes = $respBytes
            status = "SUCCESS"
        })
    }

    # 2. ANSYS CSV Test: 500,000 nodes
    $csvCount = 500000
    $csvPath = "$scratchDir\stress_$($csvCount)_nodes.csv"
    New-StressCsv $csvPath $csvCount

    $csvBytes = [System.IO.File]::ReadAllBytes($csvPath)
    $csvSizeMB = [math]::Round($csvBytes.Length / (1024 * 1024), 2)
    $csvSizeBytes = $csvBytes.Length

    Write-Host "Sending $csvCount nodes ($csvSizeMB MB) to /api/ansys/parse..." -ForegroundColor Cyan
    $swWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $httpReq = [System.Net.HttpWebRequest]::Create("http://127.0.0.1:$port/api/ansys/parse")
    $httpReq.Method = "POST"
    $httpReq.ContentType = "text/plain"
    $httpReq.Headers["Origin"] = "http://127.0.0.1:$port"
    $httpReq.ContentLength = $csvBytes.Length
    $reqStream = $httpReq.GetRequestStream()
    $reqStream.Write($csvBytes, 0, $csvBytes.Length)
    $reqStream.Close()

    $httpResp = $httpReq.GetResponse()
    $respStream = $httpResp.GetResponseStream()
    $ms = [System.IO.MemoryStream]::new()
    $respStream.CopyTo($ms)
    $swWatch.Stop()

    $parseTimeMs = [math]::Round($swWatch.Elapsed.TotalMilliseconds, 2)
    $respBytes = $ms.Length
    $httpResp.Close()

    Write-Host "  -> Parsed in $parseTimeMs ms (Response: $respBytes bytes)" -ForegroundColor Green

    $records.Add([PSCustomObject]@{
        file_type = "ANSYS_CSV"
        entity_count = $csvCount
        file_size_mb = $csvSizeMB
        file_size_bytes = $csvSizeBytes
        parse_time_ms = $parseTimeMs
        response_size_bytes = $respBytes
        status = "SUCCESS"
    })

    # Export to results/file_limits.csv
    $csvOut = "$resultsDir\file_limits.csv"
    $records | Export-Csv -Path $csvOut -NoTypeInformation
    Write-Host "Stress limits successfully recorded to $csvOut" -ForegroundColor Green

    # Output formatted table
    Write-Host "`n=== Measured Parser Stress Limits Table ===" -ForegroundColor Cyan
    $records | Format-Table -AutoSize

} finally {
    if ($serverProc -and -not $serverProc.HasExited) {
        Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
    }
}
