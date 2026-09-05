[CmdletBinding()]
param()
. "$PSScriptRoot\_common.ps1"

Write-Host 'ESP32 Project Doctor'
$failed = $false
function Check-Command([string]$Label, [scriptblock]$Action) {
    try { $value = (& $Action 2>&1 | Select-Object -First 1); Write-Host "[OK] $Label`: $value" }
    catch { $script:failed = $true; Write-Host "[FAIL] $Label`: $($_.Exception.Message)" }
}
Check-Command 'Node.js' { node --version }
Check-Command 'npm' { npm --version }
try { Enter-IdfEnvironment; Write-Host "[OK] ESP-IDF: $(& idf.py --version)" } catch { $failed=$true; Write-Host "[FAIL] ESP-IDF: $_" }
Check-Command 'Git' { git --version }
Check-Command 'Python' { python --version }
Check-Command 'CMake' { cmake --version }
Check-Command 'Ninja' { ninja --version }
Check-Command 'esptool' { python -m esptool version }
if (Test-Path -LiteralPath (Join-Path $script:ProjectRoot 'web\dist\index.html')) {
    Write-Host '[OK] React production assets'
} else { $failed=$true; Write-Host '[FAIL] React production assets: run .\scripts\build-web.ps1' }
if (Test-Path -LiteralPath (Join-Path $script:ProjectRoot 'partitions.csv')) {
    Write-Host '[OK] Custom 4 MB partition table'
} else { $failed=$true; Write-Host '[FAIL] partitions.csv missing' }
try {
    $board = Find-EspBoard -Quiet
    Write-Host "[OK] Serial port: $($board.Port)"
    Write-Host "[OK] USB serial: $($board.Name) [$($board.Vid):$($board.Pid)]"
    Write-Host "[OK] Chip: $($board.Chip)"
    Write-Host '[OK] Device communication'
} catch { $failed=$true; Write-Host "[FAIL] Board: $($_.Exception.Message)" }
if ($failed) { Write-Host 'Environment has failures.'; exit 1 }
Write-Host 'Environment ready.'
