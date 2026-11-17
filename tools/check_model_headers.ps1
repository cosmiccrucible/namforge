# Verifies that the staged VST headers match their canonical sources.
#
# The core headers are tracked and must match cpp/. The four model headers are
# staged from a pack by tools\stage_vst_models.ps1 and must match that pack.
#
# Usage: pwsh -File tools\check_model_headers.ps1 [-PacksDir packs]
# Exit code 0 = all match and present, 1 = drift or missing file.

param(
    [string]$PacksDir = 'packs'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$staged = Join-Path $root 'vst\Source\Models'

$packs = $null
if (Test-Path $PacksDir) {
    $packs = (Resolve-Path $PacksDir).Path
}

$core = @(
    @{ Name = 'NamA2Core.h'; Source = 'cpp\NamA2Core.h' }
    @{ Name = 'NamA2DualCore.h'; Source = 'cpp\NamA2DualCore.h' }
)

$weights = @(
    @{ Name = 'KrakenParametricWeights.h'; Source = 'KrakenParametric\src\KrakenParametricWeights.h' }
    @{ Name = 'KrakenParametricEcoWeights.h'; Source = 'KrakenParametric\src\KrakenParametricEcoWeights.h' }
    @{ Name = 'Peavey6505ParametricWeights.h'; Source = 'Peavey6505Parametric\src\Peavey6505ParametricWeights.h' }
    @{ Name = 'Peavey6505ParametricEcoWeights.h'; Source = 'Peavey6505Parametric\src\Peavey6505ParametricEcoWeights.h' }
)

$failures = 0

function Compare-Header([string]$name, [string]$source, [string]$label) {
    $stagedPath = Join-Path $staged $name
    if (-not (Test-Path $stagedPath)) {
        Write-Host ("MISSING   {0} (run tools\stage_vst_models.ps1 -PacksDir <packs>)" -f $name) -ForegroundColor Red
        $script:failures++
        return
    }
    $a = (Get-FileHash $stagedPath -Algorithm SHA256).Hash
    $b = (Get-FileHash $source -Algorithm SHA256).Hash
    if ($a -eq $b) {
        Write-Host ("OK        {0}  [{1}]" -f $name, $label) -ForegroundColor Green
    } else {
        Write-Host ("DRIFT     {0}  [{1}]" -f $name, $label) -ForegroundColor Yellow
        Write-Host ("          staged {0}" -f $a)
        Write-Host ("          source {1}" -f $b)
        $script:failures++
    }
}

foreach ($entry in $core) {
    Compare-Header $entry.Name (Join-Path $root $entry.Source) 'cpp'
}

if ($null -eq $packs) {
    Write-Host ("No packs directory at {0}; model headers were not checked." -f $PacksDir) -ForegroundColor Yellow
    foreach ($entry in $weights) {
        $stagedPath = Join-Path $staged $entry.Name
        if (-not (Test-Path $stagedPath)) {
            Write-Host ("MISSING   {0}" -f $entry.Name) -ForegroundColor Red
            $script:failures++
        }
    }
} else {
    foreach ($entry in $weights) {
        $source = Join-Path $packs $entry.Source
        if (-not (Test-Path $source)) {
            Write-Host ("NO SOURCE {0} (expected {1})" -f $entry.Name, $entry.Source) -ForegroundColor Red
            $script:failures++
            continue
        }
        Compare-Header $entry.Name $source $PacksDir
    }
}

if ($failures -gt 0) {
    Write-Host ""
    Write-Host ("{0} header(s) out of sync or missing." -f $failures) -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "All staged headers match their canonical sources." -ForegroundColor Green
exit 0
