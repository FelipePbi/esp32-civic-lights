[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://192.168.4.1',
    [switch]$VerifyReboot,
    [switch]$AllowSp624E
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$env:CIVIC_BASE_URL = $BaseUrl.TrimEnd('/')
$arguments = @()
if ($VerifyReboot) { $arguments += '--verify-reboot' }
if ($AllowSp624E) { $arguments += '--allow-sp624e' }
Write-Host 'Battery-safe RF bench test. Default gate requires LEFT/RIGHT not READY.'
try {
    & node (Join-Path $PSScriptRoot 'rf-bench-tests.mjs') @arguments
    exit $LASTEXITCODE
} finally {
    Remove-Item Env:CIVIC_BASE_URL -ErrorAction SilentlyContinue
}
