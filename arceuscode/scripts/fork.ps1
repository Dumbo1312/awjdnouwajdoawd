# fork.ps1 — build an ArceusCode checkout from a clean OpenCursor clone (Windows).
#
# Usage:
#   .\fork.ps1 [-TargetDir <path>]
#
# This script does NOT build; it produces the source tree. Afterward:
#   cd <TargetDir>; npm install; npm run compile; .\scripts\code.bat

param(
    [string]$TargetDir = "",
    [string]$UpstreamRef = ""
)

$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $here
if ([string]::IsNullOrEmpty($TargetDir)) {
    $TargetDir = Join-Path (Split-Path -Parent $root) "arceuscode-src"
}
if ([string]::IsNullOrEmpty($UpstreamRef)) {
    $UpstreamRef = $env:ARCEUSCODE_UPSTREAM_REF
    if ([string]::IsNullOrEmpty($UpstreamRef)) { $UpstreamRef = "main" }
}

$upstream = "https://github.com/noah-thing/open-cursor.git"

Write-Host "-> ArceusCode fork"
Write-Host "   target  : $TargetDir"
Write-Host "   upstream: $upstream@$UpstreamRef"

if (Test-Path $TargetDir) {
    Write-Error "$TargetDir already exists. Delete it or pass another path."
}

Write-Host "-> cloning OpenCursor ($UpstreamRef, shallow)..."
$env:GIT_LFS_SKIP_SMUDGE = "1"
git clone --depth 1 --branch $UpstreamRef $upstream $TargetDir

Write-Host "-> overlaying product.json"
Copy-Item "$here/patches/product.json" "$TargetDir/product.json" -Force

Write-Host "-> overlaying extensions"
New-Item -ItemType Directory -Force -Path "$TargetDir/extensions" | Out-Null
Copy-Item "$here/overlay/extensions/theme-arceuscode"   "$TargetDir/extensions/theme-arceuscode"   -Recurse -Force
Copy-Item "$here/overlay/extensions/arceuscode-presets" "$TargetDir/extensions/arceuscode-presets" -Recurse -Force

Write-Host "-> overlaying branding resources"
if (Test-Path "$here/overlay/resources/branding") {
    New-Item -ItemType Directory -Force -Path "$TargetDir/resources/arceuscode" | Out-Null
    Copy-Item "$here/overlay/resources/branding/*" "$TargetDir/resources/arceuscode/" -Recurse -Force
}

if (Test-Path "$TargetDir/README.md") {
    Move-Item "$TargetDir/README.md" "$TargetDir/README.OPENCURSOR.md" -Force
}
Copy-Item "$here/README.md" "$TargetDir/README.md" -Force

Write-Host ""
Write-Host "Source tree ready at: $TargetDir"
Write-Host ""
Write-Host "Next:"
Write-Host "  cd `"$TargetDir`""
Write-Host "  npm install                               # ~5 min"
Write-Host "  cd extensions\arceuscode-presets; npm install; npm run build; cd ..\.."
Write-Host "  npm run compile                           # ~5-15 min"
Write-Host "  .\scripts\code.bat                        # launches ArceusCode"
