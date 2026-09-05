[CmdletBinding()]
param([switch]$ManualBoot)

. "$PSScriptRoot\_common.ps1"
& "$PSScriptRoot\build-web.ps1"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
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
            & python -m esptool --chip esp32 -p $board.Port -b 460800 `
                --before=no-reset --after=hard-reset write-flash --flash-mode dio `
                --flash-freq 40m --flash-size 4MB 0x210000 web.bin
        } finally { Pop-Location }
    } else {
        & python "$env:IDF_PATH\components\partition_table\parttool.py" `
            --port $board.Port write_partition --partition-name web --input build\web.bin
    }
    if ($LASTEXITCODE) {
        Write-Error 'Web flash failed. For manual boot, hold BOOT, tap EN/RESET, keep BOOT held, then run .\scripts\flash-web.ps1 -ManualBoot.'
    }
    exit $LASTEXITCODE
} finally { Pop-Location }
