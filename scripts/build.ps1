[CmdletBinding()]
param([switch]$FullClean)
. "$PSScriptRoot\_common.ps1"
Push-Location $script:ProjectRoot
try {
    & "$PSScriptRoot\build-web.ps1"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & "$script:ProjectRoot\tests\run-host-tests.ps1"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    Enter-IdfEnvironment
    if ($FullClean) {
        $buildDir = Join-Path $script:ProjectRoot 'build'
        if ((Test-Path -LiteralPath $buildDir) -and
            -not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
            Write-Host "Removing residual non-CMake build directory: $buildDir"
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        } elseif (Test-Path -LiteralPath $buildDir) {
            & idf.py fullclean
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
        }
    }
    if (-not (Test-Path -LiteralPath 'sdkconfig')) {
        & idf.py set-target esp32
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    & idf.py build
    exit $LASTEXITCODE
} finally { Pop-Location }
