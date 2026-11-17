# Bootstrap vendor dependencies
#
# Restores the read-only upstream checkouts pinned in vendor.lock.json.
# vendor/ and .venv/ live at the workspace root (../ from this repo).
#
#   pwsh -NoProfile -File tools/bootstrap_vendor.ps1
#   pwsh -NoProfile -File tools/bootstrap_vendor.ps1 -Force
#   pwsh -NoProfile -File tools/bootstrap_vendor.ps1 -InstallParametric
#
# -Force re-checks out the pinned commit when a checkout has drifted.
# -InstallParametric also installs vendor/neural-amp-modeler-parametric into
# the workspace .venv.

[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$InstallParametric
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lock = Get-Content -Raw (Join-Path $root 'vendor.lock.json') | ConvertFrom-Json

foreach ($name in $lock.repos.PSObject.Properties.Name)
{
    $repo = $lock.repos.$name
    $path = Join-Path $root $repo.path
    Write-Host "== $name =="

    if (-not (Test-Path (Join-Path $path '.git')))
    {
        if (Test-Path $path)
        {
            throw "$path exists but is not a git checkout; move it aside first."
        }

        Write-Host "cloning $($repo.url) (branch $($repo.branch))"
        New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
        git clone --branch $repo.branch $repo.url $path
        if ($LASTEXITCODE -ne 0) { throw "clone failed for $name" }
    }

    $head = (git -C $path rev-parse HEAD).Trim()
    if ($head -eq $repo.commit)
    {
        Write-Host "ok $head"
        continue
    }

    if (-not $Force)
    {
        Write-Warning "$name is at $head, expected $($repo.commit). Re-run with -Force to reset it."
        continue
    }

    git -C $path fetch origin $repo.commit
    if ($LASTEXITCODE -ne 0) { throw "fetch failed for $name" }
    git -C $path checkout --detach $repo.commit
    if ($LASTEXITCODE -ne 0) { throw "checkout failed for $name" }
    Write-Host "pinned $($repo.commit)"
}

if ($InstallParametric)
{
    $python = Join-Path $root '..\.venv\Scripts\python.exe'
    if (-not (Test-Path $python)) { throw "missing $python; create the workspace venv first" }
    & $python -m pip install -e (Join-Path $root '..\vendor\neural-amp-modeler-parametric')
    if ($LASTEXITCODE -ne 0) { throw "editable install failed" }
}

Write-Host "vendor bootstrap complete"
