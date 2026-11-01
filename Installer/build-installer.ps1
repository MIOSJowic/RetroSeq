# Builds Installer\Output\RetroSeq-<version>-Setup.exe
# Needs: a Release build of RetroSeq.exe (Visual Studio, x64-Release) and Inno Setup 6
#        (winget install JRSoftware.InnoSetup)
# Usage:  build-installer.cmd            (finds the newest Release build)
#         build-installer.cmd -Exe C:\path\RetroSeq.exe

param([string]$Exe = "")
$ErrorActionPreference = "Stop"

$inst = $PSScriptRoot
$root = Split-Path -Parent $inst

# Version from Source\Version.h
$m = Select-String -Path (Join-Path $root "Source\Version.h") -Pattern 'RETROSEQ_VERSION\s+"([^"]+)"'
if (-not $m) { throw "Could not read RETROSEQ_VERSION from Source\Version.h" }
$ver = $m.Matches[0].Groups[1].Value
Write-Host "RetroSeq version $ver"

# Newest Release build of RetroSeq.exe
if (-not $Exe) {
    $dirs = @("out", "build") | ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path $_ }
    $found = @()
    foreach ($d in $dirs) {
        $found += Get-ChildItem -Path $d -Recurse -Filter "RetroSeq.exe" -ErrorAction SilentlyContinue |
                  Where-Object { $_.FullName -match '\\(Release|RelWithDebInfo|MinSizeRel)\\' }
    }
    if ($found.Count -eq 0) {
        throw "No Release build of RetroSeq.exe found under out\ or build\. Choose x64-Release in Visual Studio and build first (Debug builds are not packaged)."
    }
    $Exe = ($found | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
if (-not (Test-Path $Exe)) { throw "Not found: $Exe" }
Write-Host "Program:  $Exe"

$exeVer = (Get-Item $Exe).VersionInfo.ProductVersion
if ($exeVer -and -not $exeVer.StartsWith($ver)) {
    Write-Warning "RetroSeq.exe is version $exeVer but Version.h says $ver. Rebuild x64-Release first."
}

# Manual: take the newest PDF from ..\Claude\Manual if it exists
$manualDir = Join-Path $inst "Manual"
New-Item -ItemType Directory -Force -Path $manualDir | Out-Null
$pdf = Join-Path $manualDir "RetroSeq Reference Manual.pdf"
$claudePdf = Join-Path (Split-Path -Parent $root) "Claude\Manual\RetroSeq Reference Manual.pdf"
if ((Test-Path $claudePdf) -and (-not (Test-Path $pdf) -or (Get-Item $claudePdf).LastWriteTime -gt (Get-Item $pdf).LastWriteTime)) {
    Copy-Item $claudePdf $pdf -Force
}
if (-not (Test-Path $pdf)) { throw "Manual missing: $pdf" }
Write-Host "Manual:   $pdf"

# Inno Setup compiler
$iscc = @(
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    $cmd = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source }
}
if (-not $iscc) { throw "Inno Setup 6 not found. Install it with:  winget install JRSoftware.InnoSetup" }

& $iscc "/DAppVersion=$ver" "/DExePath=$Exe" (Join-Path $inst "RetroSeq.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }

$setup = Join-Path $inst "Output\RetroSeq-$ver-Setup.exe"
Write-Host ""
Write-Host "Done: $setup" -ForegroundColor Green
