[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) { throw 'Visual Studio C toolchain not found.' }
$environment = & $env:ComSpec /d /c "`"$vsDevCmd`" -no_logo -arch=x64 >nul && set"
$developerPath = $null
foreach ($line in $environment) {
    $separator = $line.IndexOf('=')
    if ($separator -gt 0) {
        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        if ($name -ieq 'PATH') { $developerPath = $value; continue }
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}
if ($developerPath) { [Environment]::SetEnvironmentVariable('Path', $developerPath, 'Process') }

$outputDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$testExe = Join-Path $outputDir 'sp624e_host_tests.exe'
$include = Join-Path $root 'main\sp624e'
$mainInclude = Join-Path $root 'main'
$sources = @(
    (Join-Path $PSScriptRoot 'host_sp624e_tests.c'),
    (Join-Path $include 'sp624e_protocol.c'),
    (Join-Path $include 'sp624e_state.c'),
    (Join-Path $include 'sp624e_mapping.c')
    (Join-Path $root 'main\sync\state_reconciler.c')
    (Join-Path $root 'main\sync\desired_state_logic.c')
    (Join-Path $root 'main\sync\group_types.c')
    (Join-Path $include 'sp624e_command_queue.c')
    (Join-Path $root 'main\ble\ble_backoff.c')
    (Join-Path $root 'main\ble\ble_recovery_policy.c')
    (Join-Path $root 'main\animation\animation_player.c')
    (Join-Path $root 'main\animation\police_animation.c')
    (Join-Path $root 'main\indicator\indicator_policy.c')
    (Join-Path $root 'main\interior\interior_light_policy.c')
    (Join-Path $root 'main\diagnostics\system_health_policy.c')
    (Join-Path $root 'main\diagnostics\ble_diag_format.c')
    (Join-Path $root 'main\remote\rf_input.c')
    (Join-Path $root 'main\remote\rf_config.c')
    (Join-Path $root 'main\remote\remote_action.c')
)

& cl.exe /nologo /std:c11 /W4 /WX /DSP624E_HOST_TEST "/I$include" "/I$mainInclude" @sources "/Fe:$testExe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $testExe
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$cjsonDir = Join-Path $root 'managed_components\espressif__cjson\cJSON'
$cjsonObj = Join-Path $outputDir 'cJSON.obj'
& cl.exe /nologo /std:c11 /W0 /c (Join-Path $cjsonDir 'cJSON.c') "/I$cjsonDir" "/Fo:$cjsonObj"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$apiExe = Join-Path $outputDir 'web_api_validation_tests.exe'
& cl.exe /nologo /std:c11 /W4 /WX "/I$mainInclude" "/I$cjsonDir" `
    (Join-Path $PSScriptRoot 'web_api_validation_tests.c') `
    (Join-Path $root 'main\web\api_validation.c') `
    (Join-Path $root 'main\animation\animation_player.c') `
    (Join-Path $root 'main\animation\police_animation.c') `
    (Join-Path $root 'main\remote\rf_config.c') $cjsonObj "/Fe:$apiExe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $apiExe
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$serializerExe = Join-Path $outputDir 'web_json_contract_tests.exe'
$hostStubs = Join-Path $PSScriptRoot 'host_stubs'
& cl.exe /nologo /std:c11 /W4 /WX /wd4244 "/I$hostStubs" "/I$mainInclude" "/I$cjsonDir" `
    (Join-Path $PSScriptRoot 'web_json_contract_tests.c') `
    (Join-Path $root 'main\web\json_codec.c') `
    (Join-Path $root 'main\sync\group_types.c') `
    (Join-Path $root 'main\ble\ble_recovery_policy.c') `
    (Join-Path $root 'main\animation\animation_player.c') `
    (Join-Path $root 'main\animation\police_animation.c') $cjsonObj "/Fe:$serializerExe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $serializerExe
exit $LASTEXITCODE
