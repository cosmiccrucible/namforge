# Stages the four model headers the VST demo compiles against.
#
# The demo tracks only NamA2Core.h and NamA2DualCore.h. The four weight headers
# are generated from a model pack, so they are copied from a packs directory at
# build time and never committed.
#
# Usage: pwsh -File tools\stage_vst_models.ps1 -PacksDir packs
# Exit code 0 = staged, 1 = a source pack/header is missing.

param(
    [Parameter(Mandatory = $true)][string]$PacksDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$staged = Join-Path $root 'vst\Source\Models'

if (-not (Test-Path $PacksDir)) {
    Write-Host ("No packs directory at {0}. Export a pack first, or pass -PacksDir <dir>." -f $PacksDir) -ForegroundColor Red
    exit 1
}

$packs = (Resolve-Path $PacksDir).Path

# core headers are canonical in cpp/ and tracked in both places
$core = @(
    @{ Name = 'NamA2Core.h'; Source = 'cpp\NamA2Core.h' }
    @{ Name = 'NamA2DualCore.h'; Source = 'cpp\NamA2DualCore.h' }
)

# weight headers come from the packs
$weights = @(
    @{ Name = 'KrakenParametricWeights.h'; Source = 'KrakenParametric\src\KrakenParametricWeights.h' }
    @{ Name = 'KrakenParametricEcoWeights.h'; Source = 'KrakenParametric\src\KrakenParametricEcoWeights.h' }
    @{ Name = 'Peavey6505ParametricWeights.h'; Source = 'Peavey6505Parametric\src\Peavey6505ParametricWeights.h' }
    @{ Name = 'Peavey6505ParametricEcoWeights.h'; Source = 'Peavey6505Parametric\src\Peavey6505ParametricEcoWeights.h' }
)

New-Item -ItemType Directory -Force -Path $staged | Out-Null

$missing = @($weights | Where-Object { -not (Test-Path (Join-Path $packs $_.Source)) })
if ($missing.Count -gt 0) {
    Write-Host "Missing headers in the packs directory:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host ("  {0}" -f $_.Source) }
    Write-Host "The demo needs the Kraken and Peavey quality pairs (Standard + Eco)." -ForegroundColor Yellow
    exit 1
}

foreach ($entry in $core) {
    Copy-Item (Join-Path $root $entry.Source) (Join-Path $staged $entry.Name) -Force
    Write-Host ("staged  {0}" -f $entry.Name) -ForegroundColor Green
}

foreach ($entry in $weights) {
    Copy-Item (Join-Path $packs $entry.Source) (Join-Path $staged $entry.Name) -Force
    Write-Host ("staged  {0}" -f $entry.Name) -ForegroundColor Green
}

Write-Host ""
Write-Host ("Model headers staged into {0}" -f $staged) -ForegroundColor Green
exit 0
