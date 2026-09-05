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
Write-Host "Flashing $($board.Chip) on $($board.Port)..."
Push-Location $script:ProjectRoot
try {
    if ($ManualBoot) {
        & idf.py build
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        Push-Location 'build'
        try {
            & python -m esptool --chip esp32 -p $board.Port -b 460800 --before=no-reset --after=hard-reset write-flash '@flash_args'
        } finally { Pop-Location }
    } else {
        & idf.py -p $board.Port flash
    }
    if ($LASTEXITCODE) {
        Write-Error 'Flash failed. If log says Wrong boot mode: hold BOOT, tap EN/RESET, release BOOT, then run .\scripts\flash.ps1 -ManualBoot.'
    }
    exit $LASTEXITCODE
} finally { Pop-Location }
