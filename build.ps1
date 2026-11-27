# Build the ShipControl DLL, optionally install into the game. Usage: .\build.ps1 [-Mod ShipControl] [-Install]
# Sources = the repo root (this checkout). Build runs from the RE-UE4SS mirror (cppmods\ShipControl).
param(
    [ValidateSet('ShipControl')][string]$Mod = 'ShipControl',
    [switch]$Install
)
$ErrorActionPreference = 'Continue'  # native tools write warnings to stderr; failures are checked via exit codes below
$src  = $PSScriptRoot                  # source = this checkout (repo root)
$deps = if ($env:MS_MODS_ROOT) { $env:MS_MODS_ROOT } else { 'F:\MSDemoMods' }   # backups
$re   = if ($env:MS_UE4SS_ROOT) { $env:MS_UE4SS_ROOT } else { 'F:\mscore-build\RE-UE4SS-v4' }   # UE4SS tree, must be v4
# UE4SS v4 gate: a DLL built from 3.0.1 headers cannot be loaded by v4 (ShipControl 0.7.0 first shipped that way)
$ueTag = (git -C $re describe --tags --exact-match 2>$null)
if ("$ueTag" -notmatch '^v[4-9]\.') { throw "UE4SS tree '$re' is not a v4 tag (got '$ueTag'). Set MS_UE4SS_ROOT to a v4 tree." }
$bt   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$gameDir = if ($env:MS_GAME_DIR) { $env:MS_GAME_DIR } else { 'F:\SteamLibrary\steamapps\common\Main Sequence Demo\Windows\MainSequence' }
$game = "$gameDir\Binaries\Win64\ue4ss\Mods\$Mod\dlls"
$utf8 = New-Object System.Text.UTF8Encoding $false

# build compiles from the mirror, so sync the source first (sources + CMakeLists.txt from the repo root)
$mirror = "$re\cppmods\$Mod"
New-Item -ItemType Directory -Force $mirror | Out-Null
Get-ChildItem "$src" -File | Where-Object { $_.Extension -in '.cpp', '.hpp', '.h', '.txt' } | Copy-Item -Destination $mirror -Force

# ShipControl is not in the mirror's cppmods\CMakeLists.txt yet: add it once
if ($Mod -eq 'ShipControl') {
    $cm = "$re\cppmods\CMakeLists.txt"
    $cmTxt = Get-Content $cm -Raw
    if ($cmTxt -notmatch 'add_subdirectory\("ShipControl"\)') {
        if ($cmTxt -notmatch 'add_subdirectory\("MiningBenchMod"\)') { throw "add_subdirectory(MiningBenchMod) not found in cppmods\CMakeLists.txt" }
        $cmTxt = $cmTxt -replace '(add_subdirectory\("MiningBenchMod"\)\r?\n)', "`$1add_subdirectory(`"ShipControl`")`n"
        [IO.File]::WriteAllText($cm, $cmTxt, $utf8)
        "Added add_subdirectory(ShipControl) to cppmods\CMakeLists.txt"
    }
}

# load the VS environment into this session
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$env:PATH = "$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$bt\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"

Set-Location $re
cmake --build build --target $Mod
if ($LASTEXITCODE -ne 0) { throw "BUILD FAILED" }

$dll = "$re\build\Game__Shipping__Win64\bin\$Mod.dll"
$f = Get-Item $dll
"{0}  {1:N0} B  {2}" -f $f.Name, $f.Length, $f.LastWriteTime
if ((Get-Date) - $f.LastWriteTime -gt [TimeSpan]::FromMinutes(5)) { Write-Warning "DLL is older than 5 min - build may be stale" }

if ($Install) {
    if (Get-Process -Name 'MainSequence*' -ErrorAction SilentlyContinue) { throw "Game is running - close it first" }
    $bk = "$deps\build\backups\$Mod-main.$(Get-Date -f yyyyMMdd-HHmmss).dll"
    New-Item -ItemType Directory -Force $game | Out-Null
    if (Test-Path "$game\main.dll") { Copy-Item "$game\main.dll" $bk }
    Copy-Item $dll "$game\main.dll" -Force
    # mscore.mod (manager Installed column) + MSCore install list (lets the manager Remove this mod)
    $verFile = Get-ChildItem "$src\*version*.hpp" | Select-Object -First 1
    $vh = Get-Content $verFile.FullName -Raw
    if ($vh -notmatch 'kVersion\s*=\s*"([^"]+)"') { throw "kVersion not found in $($verFile.Name)" }
    $ver = $Matches[1]
    $modDir = Split-Path $game -Parent
    [IO.File]::WriteAllText("$modDir\mscore.mod", "name=$Mod`nrepo=xxalex3xx97/$Mod`nversion=$ver`n", $utf8)
    if (-not (Test-Path "$modDir\enabled.txt")) { [IO.File]::WriteAllText("$modDir\enabled.txt", '', $utf8) }
    $files = @("Mods/$Mod/dlls/main.dll", "Mods/$Mod/mscore.mod", "Mods/$Mod/enabled.txt")
    $ilDir = "$gameDir\Binaries\Win64\ue4ss\Mods\MSCore\installed"
    New-Item -ItemType Directory -Force $ilDir | Out-Null
    [IO.File]::WriteAllText("$ilDir\$Mod.txt", "$ver`n" + (($files | Sort-Object) -join "`n") + "`n", $utf8)
    "Installed. Backup: $bk"
}
