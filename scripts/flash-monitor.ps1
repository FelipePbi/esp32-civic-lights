[CmdletBinding()]
param([switch]$ManualBoot)
. "$PSScriptRoot\_common.ps1"
Push-Location $script:ProjectRoot
try {
    & "$PSScriptRoot\build-web.ps1"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
} finally { Pop-Location }
Enter-IdfEnvironment
$board = if ($ManualBoot) {
    $candidate = Get-SerialCandidates | Where-Object LikelyEsp | Select-Object -First 1
    if (-not $candidate) { throw 'No likely ESP USB serial port found.' }
    [pscustomobject]@{ Port=$candidate.Port; Chip='esp32'; Name=$candidate.Name }
} else {
    Find-EspBoard -Quiet
}
Push-Location $script:ProjectRoot
try {
    & idf.py build
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    if ($ManualBoot) {
        Push-Location 'build'
        try {
            & python -m esptool --chip esp32 -p $board.Port -b 460800 --before=no-reset --after=hard-reset write-flash '@flash_args'
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
        } finally { Pop-Location }
        & idf.py -p $board.Port monitor
    } else {
        & idf.py -p $board.Port flash monitor
    }
    exit $LASTEXITCODE
} finally { Pop-Location }
