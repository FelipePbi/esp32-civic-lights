Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ProjectRoot = Split-Path -Parent $PSScriptRoot

function Enter-IdfEnvironment {
    if (Get-Command idf.py -ErrorAction SilentlyContinue) { return }

    $configPath = 'C:\Espressif\tools\eim_idf.json'
    if (-not (Test-Path -LiteralPath $configPath)) {
        throw "ESP-IDF EIM config not found: $configPath"
    }
    $config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
    $selected = $config.idfInstalled | Where-Object id -eq $config.idfSelectedId | Select-Object -First 1
    if (-not $selected) { throw 'No active ESP-IDF installation found in EIM config.' }
    if (-not (Test-Path -LiteralPath $selected.activationScript)) {
        throw "ESP-IDF activation script not found: $($selected.activationScript)"
    }
    # EIM profile probes optional CommandInfo properties that are absent for
    # some aliases; StrictMode would turn that harmless probe into an error.
    Set-StrictMode -Off
    $previousInformationPreference = $InformationPreference
    $InformationPreference = 'SilentlyContinue'
    try { . $selected.activationScript | Out-Null }
    finally { $InformationPreference = $previousInformationPreference }
    Set-StrictMode -Version Latest
}

function Get-SerialCandidates {
    $devices = @()

    foreach ($serial in Get-CimInstance Win32_SerialPort) {
        $devices += [pscustomobject]@{
            Port = $serial.DeviceID
            Name = $serial.Name
            PnpId = $serial.PNPDeviceID
        }
    }

    # Win32_SerialPort does not consistently enumerate CH9102 adapters.
    # The Ports PnP class does, so merge both sources and de-duplicate by COM port.
    foreach ($device in Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue) {
        if ($device.FriendlyName -match '\((COM\d+)\)') {
            $devices += [pscustomobject]@{
                Port = $Matches[1]
                Name = $device.FriendlyName
                PnpId = $device.InstanceId
            }
        }
    }

    $devices | Group-Object Port | ForEach-Object {
        $device = $_.Group | Where-Object PnpId -Match 'VID_' | Select-Object -First 1
        if (-not $device) { $device = $_.Group | Select-Object -First 1 }
        $usbVid = if ($device.PnpId -match 'VID_([0-9A-F]{4})') { $Matches[1] } else { '' }
        $usbPid = if ($device.PnpId -match 'PID_([0-9A-F]{4})') { $Matches[1] } else { '' }
        [pscustomobject]@{
            Port = $device.Port
            Name = $device.Name
            PnpId = $device.PnpId
            Vid = $usbVid
            Pid = $usbPid
            LikelyEsp = $usbVid -in @('303A','10C4','1A86','0403')
        }
    } | Sort-Object @{Expression='LikelyEsp';Descending=$true}, Port
}

function Find-EspBoard {
    param([switch]$Quiet)
    Enter-IdfEnvironment
    $fallback = $null
    foreach ($candidate in Get-SerialCandidates) {
        if (-not $Quiet) { Write-Host "Testing $($candidate.Port): $($candidate.Name) [$($candidate.Vid):$($candidate.Pid)]" }
        $output = (& python -m esptool --port $candidate.Port --baud 115200 chip-id 2>&1 | Out-String)
        if ($output -match '(?im)^Chip is (ESP32[^\r\n ]*)') {
            $chip = $Matches[1].ToLowerInvariant()
            return [pscustomobject]@{ Port=$candidate.Port; Chip=$chip; Vid=$candidate.Vid; Pid=$candidate.Pid; Name=$candidate.Name; Probe=$output }
        }
        if ($output -match 'Wrong boot mode detected' -and $candidate.LikelyEsp) {
            $fallback = [pscustomobject]@{ Port=$candidate.Port; Chip='esp32'; Vid=$candidate.Vid; Pid=$candidate.Pid; Name=$candidate.Name; Probe=$output }
        }
    }
    if ($fallback) { return $fallback }
    throw 'No responding Espressif board found on serial ports.'
}
