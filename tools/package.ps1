# =================================================================
# PASS SCIENCE WORKS | R&D INTERNSHIP 2026-2027
# Packaging Script: RA2311047010144_Workbench.zip
# Candidate ID: RA2311047010144
# =================================================================

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repoRoot

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " PACKAGING UNIFIED ENGINEERING WORKBENCH DISTRIBUTION           " -ForegroundColor Cyan
Write-Host " Candidate ID: RA2311047010144                                  " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

# 1. Ensure build tools in PATH
if ($env:PATH -notmatch "dev_tools") {
    $env:PATH = "C:\Users\acer\dev_tools\cmake\bin;C:\Users\acer\dev_tools\w64devkit\bin;" + $env:PATH
}

# 2. Clean build and run verification test suite
Write-Host "`n[STEP 1/5] Building Release targets with MinGW..." -ForegroundColor Yellow
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
}

Write-Host "`n[STEP 2/5] Running verification test suite..." -ForegroundColor Yellow
& .\build\workbench_tests.exe
if ($LASTEXITCODE -ne 0) {
    Write-Error "Test suite failed with exit code $LASTEXITCODE"
}

# Copy fresh binaries to repo root
Copy-Item build\Launch.exe, build\workbench_backend.exe, build\workbench_tests.exe . -Force
Write-Host "Fresh executables copied to repository root." -ForegroundColor Green

# 3. Generate UTF-8 Git Commit Log
Write-Host "`n[STEP 3/5] Exporting UTF-8 Git Commit Log to docs/git_commit_log.txt..." -ForegroundColor Yellow
if (!(Test-Path "docs")) { New-Item -ItemType Directory -Path "docs" | Out-Null }
$commitLog = git log --stat --decorate
[System.IO.File]::WriteAllText("$repoRoot/docs/git_commit_log.txt", $commitLog, [System.Text.Encoding]::UTF8)
Write-Host "Commit log exported ($(Get-Item docs/git_commit_log.txt).Length bytes)." -ForegroundColor Green

# 4. Generate Git Bundle
Write-Host "`n[STEP 4/5] Creating Git Bundle (RA2311047010144_Workbench.bundle)..." -ForegroundColor Yellow
$bundleFile = "$repoRoot/RA2311047010144_Workbench.bundle"
if (Test-Path $bundleFile) { Remove-Item $bundleFile -Force }
git bundle create $bundleFile --all
if ($LASTEXITCODE -ne 0) {
    Write-Error "Git bundle creation failed with exit code $LASTEXITCODE"
}
Write-Host "Git bundle created ($(Get-Item $bundleFile).Length bytes)." -ForegroundColor Green

# 5. Create Distribution Zip Archive using tar -a -c -f
Write-Host "`n[STEP 5/5] Building distribution ZIP with forward-slash paths via tar..." -ForegroundColor Yellow
$zipFile = "$repoRoot/RA2311047010144_Workbench.zip"
if (Test-Path $zipFile) { Remove-Item $zipFile -Force }

# Use tar -a -c -f to build zip with forward-slash paths, excluding build, .git, logs, and zip files
tar -a -c -f $zipFile --exclude="build" --exclude="build/*" --exclude=".git" --exclude=".git/*" --exclude="logs" --exclude="logs/*" --exclude="dist" --exclude="dist/*" --exclude="*.zip" *

if (!(Test-Path $zipFile)) {
    Write-Error "Failed to generate $zipFile"
}

$zipInfo = Get-Item $zipFile
Write-Host "`n=================================================================" -ForegroundColor Green
Write-Host " PACKAGING SUCCESSFUL: $($zipInfo.Name)" -ForegroundColor Green
Write-Host " Size: $([math]::Round($zipInfo.Length / 1MB, 2)) MB ($($zipInfo.Length) bytes)" -ForegroundColor Green
Write-Host " Location: $($zipInfo.FullName)" -ForegroundColor Green
Write-Host "=================================================================" -ForegroundColor Green
