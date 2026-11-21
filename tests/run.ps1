# Run every unit/static check (no game needed). Usage: .\tests\run.ps1
# Steps: test_shipcontrol.exe (owner registry), test_abi.py (SC_ exports vs sc_api.h).
# Exit code 0 = no FAIL (SKIP is allowed), non-zero = at least one step FAILED.
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path -Parent $PSScriptRoot)
$bt = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'  # same MSVC as build.ps1

New-Item -ItemType Directory -Force tests\out | Out-Null
$rows = @()

function Add-Row($name, $status, $detail) {
    $script:rows += [pscustomobject]@{ Step = $name; Status = $status; Detail = $detail }
}

# ---- 1. test_shipcontrol (ShipControl owner registry, sc_owner.hpp) ----
Write-Host "== test_shipcontrol" -ForegroundColor Cyan
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
cl /nologo /EHsc /std:c++17 /W3 /I . tests\test_shipcontrol.cpp /Fe:tests\out\test_shipcontrol.exe /Fo:tests\out\
if ($LASTEXITCODE -ne 0) {
    Add-Row 'test_shipcontrol' 'FAIL' 'compile failed'
} else {
    $out = & .\tests\out\test_shipcontrol.exe 2>&1
    $out | ForEach-Object { Write-Host $_ }
    $line = ($out | Where-Object { $_ -match '^test_shipcontrol: ' } | Select-Object -Last 1)
    if ($LASTEXITCODE -eq 0) { Add-Row 'test_shipcontrol' 'PASS' $line } else { Add-Row 'test_shipcontrol' 'FAIL' $line }
}

# ---- 2. python tests ----
$py = (Get-Command python -ErrorAction SilentlyContinue)
function Run-Py($step, $script) {
    Write-Host "== $step" -ForegroundColor Cyan
    if (-not $py) { Add-Row $step 'SKIP' 'python not on PATH'; return }
    $out = & python -I $script 2>&1
    $out | ForEach-Object { Write-Host $_ }
    $ok = @($out | Where-Object { $_ -match '^ok ' }).Count
    $bad = @($out | Where-Object { $_ -match '^FAIL ' }).Count
    $detail = "$ok passed, $bad failed"
    if ($LASTEXITCODE -eq 0) { Add-Row $step 'PASS' $detail } else { Add-Row $step 'FAIL' $detail }
}
Run-Py 'test_abi' 'tests\test_abi.py'

# ---- summary ----
Write-Host ""
Write-Host "== SUMMARY" -ForegroundColor Cyan
$rows | Format-Table -AutoSize | Out-String | Write-Host
$fails = @($rows | Where-Object { $_.Status -eq 'FAIL' }).Count
if ($fails -gt 0) { Write-Host "RESULT: $fails step(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host "RESULT: no failures (SKIP = not run)" -ForegroundColor Green
exit 0
