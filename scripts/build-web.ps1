[CmdletBinding()]
param([switch]$SkipTests)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$webRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'web'
Push-Location $webRoot
try {
    if (-not (Test-Path -LiteralPath 'node_modules')) {
        & npm ci
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    if (-not $SkipTests) {
        & npm test
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    & npm run build
    exit $LASTEXITCODE
} finally { Pop-Location }
