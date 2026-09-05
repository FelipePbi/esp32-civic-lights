<#
.SYNOPSIS
Runs a scripted BLE diagnostic session over the serial console and captures the
full transcript.

.DESCRIPTION
Sends each command in order and drains the serial output the whole time, so
nothing emitted between commands is lost.

The CH9102 adapter on this board drops off USB whenever the ESP32 resets, and
the reset itself can be triggered by opening the port. The session therefore
reconnects on its own, waits for the firmware to boot and for the SP624E links
to settle, and then re-sends the command that was interrupted.

Use "wait:<seconds>" as a pseudo command to hold while the firmware works, for
example during a scan window.

.EXAMPLE
.\scripts\diag-session.ps1 -Commands 'diag enable','diag scan clear',
    'diag scan start 15','wait:18','diag scan list' -Label scan-leds-on
#>
[CmdletBinding()]
param(
    [string]$Port,
    [Parameter(Mandatory)][string[]]$Commands,
    [double]$SettleSeconds = 2,
    [double]$TailSeconds = 3,
    [double]$OpenTimeoutSeconds = 90,
    [double]$BootSettleSeconds = 25,
    [int]$MaxReconnects = 6,
    [string]$Label = 'diag'
)

. "$PSScriptRoot\_common.ps1"

if (-not $Port) {
    $candidate = Get-SerialCandidates | Where-Object LikelyEsp | Select-Object -First 1
    if (-not $candidate) { throw 'No likely ESP USB serial port found.' }
    $Port = $candidate.Port
}

$logDirectory = Join-Path $script:ProjectRoot 'logs\diag'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $logDirectory "$timestamp-$Label.log"

$script:Serial = $null
$script:Reconnects = 0
$script:Aborted = $false
$writer = [System.IO.StreamWriter]::new($logPath, $false, [System.Text.Encoding]::UTF8)
$buffer = [byte[]]::new(4096)

function Write-Transcript {
    param([string]$Text)
    $writer.Write($Text)
    $writer.Flush()
    [Console]::Write($Text)
}

function Write-Marker {
    param([string]$Text)
    $writer.WriteLine("`n$Text")
    $writer.Flush()
    Write-Host "`n$Text"
}

function Close-Serial {
    if ($null -eq $script:Serial) { return }
    try { if ($script:Serial.IsOpen) { $script:Serial.Close() } } catch { }
    try { $script:Serial.Dispose() } catch { }
    $script:Serial = $null
}

function Open-Serial {
    param([double]$TimeoutSeconds)
    Close-Serial
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $present = Get-SerialCandidates | Where-Object Port -eq $Port
        if ($present) {
            $serial = [System.IO.Ports.SerialPort]::new($Port, 115200,
                [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
            # Deasserted before open so the DCB does not drive EN/IO0 on this adapter.
            $serial.DtrEnable = $false
            $serial.RtsEnable = $false
            $serial.ReadTimeout = 200
            try {
                $serial.Open()
                $serial.DtrEnable = $false
                $serial.RtsEnable = $false
                $script:Serial = $serial
                $watch.Stop()
                return $true
            } catch {
                try { $serial.Dispose() } catch { }
            }
        }
        Start-Sleep -Seconds 2
    }
    $watch.Stop()
    return $false
}

function Restore-Link {
    if ($script:Reconnects -ge $MaxReconnects) {
        Write-Marker ">>> GIVING UP: $MaxReconnects reconnects exhausted"
        $script:Aborted = $true
        return $false
    }
    $script:Reconnects++
    Write-Marker ">>> SERIAL LINK LOST; reconnect attempt $($script:Reconnects)/$MaxReconnects"
    if (-not (Open-Serial -TimeoutSeconds $OpenTimeoutSeconds)) {
        Write-Marker ">>> $Port did not come back within $OpenTimeoutSeconds s"
        $script:Aborted = $true
        return $false
    }
    Write-Marker ">>> reconnected; waiting $BootSettleSeconds s for boot and SP624E links"
    Invoke-Drain -Seconds $BootSettleSeconds
    return -not $script:Aborted
}

function Invoke-Drain {
    param([double]$Seconds)
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        if ($script:Aborted) { break }
        try {
            $available = $script:Serial.BytesToRead
            if ($available -le 0) {
                Start-Sleep -Milliseconds 20
                continue
            }
            $read = $script:Serial.Read($buffer, 0, [Math]::Min($available, $buffer.Length))
            if ($read -le 0) { continue }
        } catch {
            $watch.Stop()
            if (-not (Restore-Link)) { return }
            $watch = [System.Diagnostics.Stopwatch]::StartNew()
            continue
        }
        Write-Transcript ([System.Text.Encoding]::UTF8.GetString($buffer, 0, $read))
    }
    $watch.Stop()
}

function Send-Command {
    param([string]$Text)
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        if ($script:Aborted) { return }
        Write-Marker ">>> $Text"
        try {
            $script:Serial.Write("$Text`r`n")
            Invoke-Drain -Seconds $SettleSeconds
            return
        } catch {
            if (-not (Restore-Link)) { return }
        }
    }
}

try {
    if (-not (Open-Serial -TimeoutSeconds $OpenTimeoutSeconds)) {
        throw "Serial port $Port did not become available within $OpenTimeoutSeconds s."
    }
    Write-Host "=== diag session on $Port -> $logPath ==="
    Invoke-Drain -Seconds 0.5
    foreach ($command in $Commands) {
        if ($script:Aborted) { break }
        if ($command -match '^wait:([\d.]+)$') {
            $seconds = [double]$Matches[1]
            Write-Marker "--- waiting $seconds s ---"
            Invoke-Drain -Seconds $seconds
            continue
        }
        Send-Command -Text $command
    }
    if (-not $script:Aborted) { Invoke-Drain -Seconds $TailSeconds }
} finally {
    Close-Serial
    $writer.Dispose()
}

if ($script:Aborted) {
    Write-Host "`n=== transcript saved (session aborted): $logPath ==="
    exit 1
}
Write-Host "`n=== transcript saved: $logPath ==="
