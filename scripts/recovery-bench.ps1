[CmdletBinding()]
param(
    [string]$Port,
    [switch]$Flash,
    [switch]$Monitor,
    [switch]$ManualBoot,
    [uint32]$CaptureSeconds = 0
)

. "$PSScriptRoot\_common.ps1"

$expectedVid = '1A86'
$expectedPid = '55D4'
$candidates = @(Get-SerialCandidates)
$candidate = if ($Port) {
    $candidates | Where-Object Port -eq $Port | Select-Object -First 1
} else {
    $candidates | Where-Object {
        $_.Vid -eq $expectedVid -and $_.Pid -eq $expectedPid
    } | Select-Object -First 1
}

if (-not $candidate) {
    $seen = if ($candidates.Count -gt 0) {
        ($candidates | ForEach-Object { "$($_.Port) [$($_.Vid):$($_.Pid)]" }) -join ', '
    } else {
        'nenhuma porta'
    }
    throw "CH9102 esperado [$expectedVid`:$expectedPid] não encontrado. Detectado: $seen"
}
if ($candidate.Vid -ne $expectedVid -or $candidate.Pid -ne $expectedPid) {
    throw "Porta $($candidate.Port) é [$($candidate.Vid):$($candidate.Pid)], não o CH9102 esperado [$expectedVid`:$expectedPid]."
}

$firmwarePath = Join-Path $script:ProjectRoot 'build\sp624e_controller.bin'
$webPath = Join-Path $script:ProjectRoot 'build\web.bin'
$flashArgsPath = Join-Path $script:ProjectRoot 'build\flash_args'
foreach ($required in @($firmwarePath, $webPath, $flashArgsPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Artefato ausente: $required. Execute .\scripts\build.ps1 -FullClean."
    }
}

$flashArgs = Get-Content -Raw -LiteralPath $flashArgsPath
if ($flashArgs -match '(?m)^0x0*9000\s') {
    throw 'Plano de flash inclui 0x9000 (NVS); operação recusada.'
}

Enter-IdfEnvironment
Write-Host "Preflight CH9102: $($candidate.Port) $($candidate.Name)"
$probe = (& python -m esptool --port $candidate.Port --baud 115200 chip-id 2>&1 | Out-String)
if ($probe -match '(?im)^Chip is ESP32(?:\s|$)') {
    Write-Host ($probe.Trim())
} elseif ($probe -match 'Wrong boot mode detected') {
    Write-Host 'CH9102 presente; ESP32 está em modo de aplicação. Auto-reset será validado no flash.'
} else {
    Write-Host $probe
    throw "A porta $($candidate.Port) não respondeu como ESP32 clássico."
}

function Write-ArtifactSummary {
    $firmware = Get-Item -LiteralPath $firmwarePath
    $firmwareHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $firmwarePath).Hash
    $webHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $webPath).Hash
    Write-Host "Firmware: $($firmware.Length) bytes SHA256=$firmwareHash"
    Write-Host "Web: SHA256=$webHash"
    Write-Host 'NVS 0x9000 não faz parte do plano de flash.'
}

Write-ArtifactSummary
if (-not $Flash -and -not $Monitor) {
    Write-Host 'Preflight PASS. Use -Flash e depois -Monitor quando a bancada estiver pronta.'
    exit 0
}

$logDirectory = Join-Path $script:ProjectRoot 'logs\recovery-bench'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$sessionLogPath = Join-Path $logDirectory "$timestamp-$($candidate.Port)-session.log"
$serialLogPath = Join-Path $logDirectory "$timestamp-$($candidate.Port)-serial.log"
Start-Transcript -LiteralPath $sessionLogPath -Force | Out-Null

function Start-RawSerialCapture {
    param(
        [Parameter(Mandatory)][string]$SerialPort,
        [Parameter(Mandatory)][string]$OutputPath,
        [uint32]$DurationSeconds
    )
    $serial = [System.IO.Ports.SerialPort]::new($SerialPort, 115200,
        [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.ReadTimeout = 200
    $stream = [System.IO.File]::Open($OutputPath,
        [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read)
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $serial.Open()
        Write-Host "Captura serial bruta: $OutputPath"
        if ($DurationSeconds -eq 0) {
            Write-Host 'Encerre com Ctrl+C.'
        } else {
            Write-Host "Capturando por $DurationSeconds segundos."
        }
        $buffer = [byte[]]::new(4096)
        while ($DurationSeconds -eq 0 -or $watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
            $available = $serial.BytesToRead
            if ($available -le 0) {
                Start-Sleep -Milliseconds 20
                continue
            }
            $read = $serial.Read($buffer, 0, [Math]::Min($available, $buffer.Length))
            if ($read -le 0) { continue }
            $stream.Write($buffer, 0, $read)
            $stream.Flush()
            [Console]::Write([System.Text.Encoding]::UTF8.GetString($buffer, 0, $read))
        }
    } finally {
        $watch.Stop()
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
        $stream.Dispose()
    }
}
try {
    Push-Location $script:ProjectRoot
    try {
        if ($Flash) {
            & "$PSScriptRoot\build.ps1" -FullClean
            if ($LASTEXITCODE -ne 0) { throw "FullClean falhou: $LASTEXITCODE" }
            Write-ArtifactSummary
            if ($ManualBoot) {
                Write-Host 'Bootloader manual necessário.'
                Write-Host '1. Mantenha BOOT pressionado.'
                Write-Host '2. Toque EN/RESET.'
                Write-Host '3. Solte BOOT.'
                Read-Host 'Pressione Enter somente depois dessa sequência' | Out-Null
                Push-Location 'build'
                try {
                    & python -m esptool --chip esp32 -p $candidate.Port -b 460800 `
                        --before=no-reset --after=hard-reset write-flash '@flash_args'
                } finally {
                    Pop-Location
                }
            } else {
                & idf.py -p $candidate.Port flash
            }
            if ($LASTEXITCODE -ne 0) { throw "Flash falhou: $LASTEXITCODE" }
        }
        if ($Monitor) {
            Start-RawSerialCapture -SerialPort $candidate.Port `
                -OutputPath $serialLogPath -DurationSeconds $CaptureSeconds
        }
    } finally {
        Pop-Location
    }
} finally {
    Stop-Transcript | Out-Null
}

Write-Host "Log de sessão salvo em $sessionLogPath"
if ($Monitor) { Write-Host "Log serial salvo em $serialLogPath" }
