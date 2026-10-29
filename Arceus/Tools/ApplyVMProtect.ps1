#
# ApplyVMProtect.ps1 — post-link VMProtect protection pass.
#
# Usage (from MSBuild post-build, requires RIVEN_APPLY_VMP=1 env var):
#   powershell -File Tools\ApplyVMProtect.ps1 -ProjectDir "$(ProjectDir)" -TargetExe "$(TargetPath)" -OutDir "$(ProjectDir)Protected"
#
# Or standalone:
#   powershell -File Tools\ApplyVMProtect.ps1 -TargetExe "x64\DirectML\RIVEN.exe" -OutDir "Protected"
#
# What it does:
#   1. Locates VMProtect_Con.exe (default: C:\Users\Computer\Desktop\VMProtect).
#   2. Uses an existing .vmp project (GUI-created) if one is found next to the
#      target; otherwise GENERATES one and protects the ENTIRE file with
#      Virtualization + Mutation + Ultra (+ packing / imports / memory /
#      runtime-checker when licensed).
#   3. Runs the console and reports the protected output.
#
# IMPORTANT:
#   - A valid, activated VMProtect license is required. If the license is
#     missing/expired the pass will FAIL — the raw exe stays usable, so the
#     build never breaks.
#   - Kernel-mode (ring-0) anti-dump/anti-debug driver: enable in the GUI
#     project (Settings), it cannot be expressed in the generated template
#     reliably across versions. On Secure Boot systems the driver may be
#     refused — VMProtect then runs user-mode only.
#   - The XML template is for VMProtect 3.x. If the console rejects it, open
#     the GUI once, create the project manually and save it as RIVEN.vmp next
#     to the exe — the script will pick it up unchanged.
#

param(
    [string]$ProjectDir = "",
    [string]$TargetExe = "",
    [string]$OutDir = "",
    [string]$VmProtectDir = ""
)

$ErrorActionPreference = "Stop"

if (-not $VmProtectDir) {
    $VmProtectDir = "C:\Users\Computer\Desktop\VMProtect"
}
if (-not (Test-Path -LiteralPath $VmProtectDir)) {
    Write-Host "[VMP] VMProtect dir not found: $VmProtectDir — skipping (raw build kept)." -ForegroundColor Yellow
    exit 0
}
if (-not $TargetExe) {
    Write-Host "[VMP] No target exe specified — skipping." -ForegroundColor Yellow
    exit 0
}
if (-not (Test-Path -LiteralPath $TargetExe)) {
    Write-Host "[VMP] Target exe missing: $TargetExe — skipping." -ForegroundColor Yellow
    exit 0
}

$con = Join-Path $VmProtectDir "VMProtect_Con.exe"
if (-not (Test-Path -LiteralPath $con)) {
    Write-Host "[VMP] VMProtect_Con.exe not found in $VmProtectDir — skipping." -ForegroundColor Yellow
    exit 0
}

$resolvedExe = [System.IO.Path]::GetFullPath($TargetExe)
$exeDir = [System.IO.Path]::GetDirectoryName($resolvedExe)
$exeName = [System.IO.Path]::GetFileNameWithoutExtension($resolvedExe)

if (-not $OutDir) {
    $OutDir = Join-Path $exeDir "Protected"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outExe = Join-Path $OutDir ($exeName + "_vmp.exe")

$projectFile = Join-Path $exeDir ($exeName + ".vmp")
$generated = $false
if (-not (Test-Path -LiteralPath $projectFile)) {
    # Generate a best-effort VMProtect 3.x project protecting the entire file.
    $xml = @"
<?xml version="1.0" encoding="utf-8"?>
<VMPROJECT>
  <INPUT_FILENAME>$($resolvedExe.Replace('\', '/'))</INPUT_FILENAME>
  <OUTPUT_FILENAME>$($outExe.Replace('\', '/'))</OUTPUT_FILENAME>
  <OPTIONS>
    <!-- Whole-file protection profile -->
    <VIRTUALIZATION>1</VIRTUALIZATION>
    <MUTATION>1</MUTATION>
    <ULTRA>1</ULTRA>
    <!-- Uncomment if your license supports these; adjust names if the console rejects: -->
    <PACK>1</PACK>
    <IMPORT_PROTECTION>1</IMPORT_PROTECTION>
    <MEMORY_PROTECTION>1</MEMORY_PROTECTION>
    <DISABLE_DEBUGGER_ATTACH>1</DISABLE_DEBUGGER_ATTACH>
    <RUNTIME_CHECKER>1</RUNTIME_CHECKER>
  </OPTIONS>
</VMPROJECT>
"@
    Set-Content -LiteralPath $projectFile -Value $xml -Encoding UTF8
    $generated = $true
}

Write-Host "[VMP] Project: $projectFile"
Write-Host "[VMP] Running VMProtect_Con.exe ..." -ForegroundColor Cyan
& $con $projectFile 2>&1 | ForEach-Object { Write-Host "[VMP] $_" }
$exitCode = $LASTEXITCODE

if (-not (Test-Path -LiteralPath $outExe)) {
    if ($generated) {
        Write-Host "[VMP] FAILED with generated project — the XML template may differ for your VMProtect version." -ForegroundColor Red
        Write-Host "[VMP] Fix: open VMProtect GUI -> New Project -> select $resolvedExe -> enable Mutation/Virtualization/Ultra -> Save project as $projectFile -> rerun." -ForegroundColor Yellow
    }
    Write-Host "[VMP] Raw build kept at $resolvedExe (release path unchanged)." -ForegroundColor Yellow
    exit $exitCode
}

Write-Host "[VMP] OK -> $outExe" -ForegroundColor Green
exit 0