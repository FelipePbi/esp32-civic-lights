[CmdletBinding()]
param(
    [ValidateRange(0, 100)]
    [int]$RgbCount = 0,
    [string]$BaseUrl = 'http://192.168.4.1'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$env:CIVIC_BASE_URL = $BaseUrl.TrimEnd('/')
try {
    & node (Join-Path $PSScriptRoot 'web-hardware-tests.mjs') "--rgb-count=$RgbCount"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
} finally {
    Remove-Item Env:CIVIC_BASE_URL -ErrorAction SilentlyContinue
}
