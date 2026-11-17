# Start the NAM Forge guided app.
#
# Double-click "Start NAM Forge.cmd" in the repo root, or run:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\start_app.ps1
#
# First run creates the workspace venv and installs the full stack
# (PyTorch CUDA is a multi-GB download). Later runs just start the server.
#
# Options:
#   -Port 8765       preferred port (auto-advances if busy)
#   -NoBrowser       do not open the browser
#   -ForceInstall    reinstall Python dependencies

[CmdletBinding()]
param(
    [int]$Port = 8765,
    [switch]$NoBrowser,
    [switch]$ForceInstall
)

$ErrorActionPreference = 'Continue'

$repo = Split-Path -Parent $PSScriptRoot
$workspace = Split-Path -Parent $repo
$venv = Join-Path $workspace '.venv'
$python = Join-Path $venv 'Scripts\python.exe'
$requirements = Join-Path $repo 'requirements.txt'
$torchRequirements = Join-Path $repo 'requirements-torch.txt'
$vendorParametric = Join-Path $workspace 'vendor\neural-amp-modeler-parametric'

function Write-Step([string]$Message) {
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Invoke-Checked([string]$FilePath, [string[]]$Arguments, [string]$What) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed with exit code $LASTEXITCODE."
    }
}

function Test-Imports([string]$Code) {
    & $python -c $Code *> $null
    return ($LASTEXITCODE -eq 0)
}

function Test-AppAt([string]$BaseUrl) {
    try {
        $health = Invoke-RestMethod -Uri "$BaseUrl/api/health" -TimeoutSec 2
        return ($health.ok -eq $true -and $health.root -eq $repo)
    }
    catch {
        return $false
    }
}

function Test-PortFree([int]$Candidate) {
    $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Candidate)
    try {
        $listener.Start()
        $listener.Stop()
        return $true
    }
    catch {
        return $false
    }
}

function New-WorkspaceVenv {
    Write-Step "Creating the workspace venv at $venv"
    $created = $false

    if (Get-Command py -ErrorAction SilentlyContinue) {
        & py -3.12 -m venv $venv
        $created = Test-Path $python
    }

    if (-not $created -and (Get-Command python -ErrorAction SilentlyContinue)) {
        & python -m venv $venv
        $created = Test-Path $python
    }

    if (-not $created) {
        throw "Python 3.12 was not found. Install Python 3.12, then run this launcher again."
    }
}

function Install-All {
    Write-Step "First-time setup: downloading PyTorch CUDA and Python dependencies."
    Write-Host "    This is several GB and can take 10-20 minutes on a first run." -ForegroundColor Yellow

    if (-not (Test-Path $vendorParametric)) {
        Write-Step "Restoring the vendor checkouts"
        & (Join-Path $PSScriptRoot 'bootstrap_vendor.ps1')
        if (-not $?) { throw "Vendor bootstrap failed." }
    }

    Write-Step "Installing PyTorch CUDA 12.8"
    Invoke-Checked -FilePath $python -Arguments @('-m', 'pip', 'install', '-r', $torchRequirements) -What 'PyTorch install'

    Write-Step "Installing pipeline and app dependencies"
    Invoke-Checked -FilePath $python -Arguments @('-m', 'pip', 'install', '-r', $requirements) -What 'Python dependency install'

    Write-Step "Installing the parametric trainer"
    Invoke-Checked -FilePath $python -Arguments @('-m', 'pip', 'install', '-e', $vendorParametric) -What 'Editable trainer install'
}

# --- Environment -----------------------------------------------------------

if (-not (Test-Path $python)) {
    New-WorkspaceVenv
    Install-All
}
elseif ($ForceInstall) {
    Install-All
}

if (-not (Test-Imports "import fastapi, uvicorn, numpy, soundfile")) {
    Write-Step "Installing missing app dependencies"
    Invoke-Checked -FilePath $python -Arguments @('-m', 'pip', 'install', '-r', $requirements) -What 'Python dependency install'
}

if (-not (Test-Imports "import torch, nam")) {
    Write-Warning "Training dependencies are missing. Run 'Start NAM Forge.cmd -ForceInstall' before starting a training run."
}

# --- Server ----------------------------------------------------------------

$baseUrl = "http://127.0.0.1:$Port"
if (Test-AppAt $baseUrl) {
    Write-Step "NAM Forge is already running at $baseUrl"
    if (-not $NoBrowser) { Start-Process $baseUrl }
    exit 0
}

if (-not (Test-PortFree $Port)) {
    $original = $Port
    while (-not (Test-PortFree $Port) -and $Port -lt ($original + 50)) {
        $Port++
    }
    if (-not (Test-PortFree $Port)) {
        throw "No free port found near $original."
    }
    $baseUrl = "http://127.0.0.1:$Port"
    Write-Step "Port $original is busy; using $Port instead"
}

Write-Step "Starting NAM Forge at $baseUrl"
$server = Start-Process -FilePath $python `
    -ArgumentList @('-m', 'uvicorn', 'app.main:app', '--host', '127.0.0.1', '--port', "$Port") `
    -WorkingDirectory $repo `
    -NoNewWindow `
    -PassThru `
    -ErrorAction Stop

$ready = $false
$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    if ($server.HasExited) { break }
    if (Test-AppAt $baseUrl) {
        $ready = $true
        break
    }
    Start-Sleep -Milliseconds 500
}

if (-not $ready) {
    if (-not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
    }
    throw "The server did not become ready at $baseUrl. Check the console output above."
}

if (-not $NoBrowser) {
    Start-Process $baseUrl
}

Write-Host ""
Write-Host "NAM Forge is running at $baseUrl" -ForegroundColor Green
Write-Host "Close this window or press Ctrl+C to stop it." -ForegroundColor DarkGray

if (-not $server.HasExited) {
    Wait-Process -Id $server.Id -ErrorAction SilentlyContinue
}
$server.Refresh()
if ($server.HasExited -and $server.ExitCode -ne 0) {
    throw "The server exited with code $($server.ExitCode)."
}
