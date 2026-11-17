<#
.SYNOPSIS
    Creates a new Nam Forge test plugin from vst/template.

.DESCRIPTION
    Copies the scaffold, substitutes plugin metadata tokens, and prints the build
    commands. The result builds immediately with placeholder stages that the
    caller replaces with real models.

.PARAMETER Name
    Display name shown in the DAW, e.g. "NamForge Kraken Demo".

.PARAMETER Target
    CMake target / folder name. Derived from -Name when omitted.

.PARAMETER ManufacturerCode
    Four alphanumeric characters. Defaults to CCDV.

.PARAMETER PluginCode
    Four characters, unique per plugin. Derived from -Target when omitted.

.EXAMPLE
    pwsh -File tools\New-TestPlugin.ps1 -Name "NamForge Foo Demo"

.EXAMPLE
    pwsh -File tools\New-TestPlugin.ps1 -Name "NamForge Foo Demo" -PluginCode FOO1
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Name,

    [string] $Target,

    [string] $Manufacturer = 'Cosmic Crucible Development',

    [string] $ManufacturerCode = 'CCDV',

    [string] $PluginCode,

    [string] $BundleId,

    [string] $DestRoot = 'vst'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$template = Join-Path $repoRoot 'vst\template'

if (-not (Test-Path $template)) {
    throw "Template not found: $template"
}

if (-not $Target) {
    $Target = ($Name -replace '[^A-Za-z0-9]', '')
}

if (-not $Target) {
    throw 'Could not derive a target name from -Name; pass -Target explicitly.'
}

if (-not $PluginCode) {
    $letters = ($Target.ToUpperInvariant() -replace '[^A-Z0-9]', '')
    if ($letters.Length -lt 4) {
        throw "Cannot derive a 4-character PluginCode from '$Target'; pass -PluginCode."
    }
    $PluginCode = $letters.Substring(0, 4)
}

if (-not $BundleId) {
    $BundleId = 'com.cosmiccrucible.namforge.' + $Target.ToLowerInvariant()
}

foreach ($pair in @(@('ManufacturerCode', $ManufacturerCode), @('PluginCode', $PluginCode))) {
    if ($pair[1] -notmatch '^[A-Za-z0-9]{4}$') {
        throw "$($pair[0]) must be exactly 4 alphanumeric characters (got '$($pair[1])')."
    }
}

$dest = Join-Path (Join-Path $repoRoot $DestRoot) $Target

if (Test-Path $dest) {
    throw "Destination already exists: $dest"
}

Write-Host "Creating $Name"
Write-Host "  target      : $Target"
Write-Host "  plugin code : $PluginCode"
Write-Host "  bundle id   : $BundleId"
Write-Host "  location    : $dest"

Copy-Item -Path $template -Destination $dest -Recurse -Force

$tokens = [ordered]@{
    '{{TARGET}}'            = $Target
    '{{PLUGIN_NAME}}'       = $Name
    '{{MANUFACTURER}}'      = $Manufacturer
    '{{MANUFACTURER_CODE}}' = $ManufacturerCode
    '{{PLUGIN_CODE}}'       = $PluginCode
    '{{BUNDLE_ID}}'         = $BundleId
}

$edited = 0
Get-ChildItem -Path $dest -Recurse -File |
    Where-Object { $_.Extension -in '.h', '.cpp', '.txt', '.md' } |
    ForEach-Object {
        $text   = Get-Content -Path $_.FullName -Raw
        $before = $text
        foreach ($key in $tokens.Keys) {
            $text = $text.Replace($key, [string] $tokens[$key])
        }
        if ($text -ne $before) {
            Set-Content -Path $_.FullName -Value $text -NoNewline -Encoding utf8
            $edited++
        }
    }

# Fail loudly if a token was missed rather than shipping a literal {{...}}.
$leftover = Get-ChildItem -Path $dest -Recurse -File |
    Where-Object { $_.Extension -in '.h', '.cpp', '.txt', '.md' } |
    Select-String -Pattern '\{\{[A-Z_]+\}\}'

if ($leftover) {
    Write-Host ''
    Write-Warning 'Unsubstituted tokens remain:'
    $leftover | ForEach-Object { Write-Host "  $($_.Path):$($_.LineNumber)  $($_.Line.Trim())" }
}

Write-Host ''
Write-Host "Created $edited file(s)." -ForegroundColor Green
Write-Host ''
Write-Host 'Next steps:'
Write-Host "  1. Replace the placeholder stage in $DestRoot\$Target\Source\PluginProcessor.cpp"
Write-Host '  2. Build:'
Write-Host "       `$cmake -S $DestRoot\$Target -B ..\build-$Target -G `"Visual Studio 18 2026`" -A x64 -DJUCE_PATH=`"`$PWD\..\build-vst\_deps\juce-src`""
Write-Host "       `$cmake --build ..\build-$Target --config Release"
Write-Host '  3. Verify:'
Write-Host "       & ..\build-$Target\${Target}SelfTest_artefacts\Release\${Target}SelfTest.exe"
Write-Host "       & ..\build-$Target\${Target}UiShot_artefacts\Release\${Target}UiShot.exe 'ui-$Target-1.png'"
