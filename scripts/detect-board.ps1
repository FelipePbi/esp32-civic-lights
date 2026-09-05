[CmdletBinding()]
param([switch]$AsJson)
. "$PSScriptRoot\_common.ps1"

$board = Find-EspBoard -Quiet:$AsJson
if ($AsJson) {
    $board | Select-Object Port,Chip,Vid,Pid,Name | ConvertTo-Json -Compress
} else {
    Write-Host "PORT=$($board.Port)"
    Write-Host "CHIP=$($board.Chip)"
    Write-Host "USB_SERIAL=$($board.Name)"
    Write-Host "VID_PID=$($board.Vid):$($board.Pid)"
}
