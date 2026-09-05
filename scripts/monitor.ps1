[CmdletBinding()]
param()
. "$PSScriptRoot\_common.ps1"
Enter-IdfEnvironment
$board = Find-EspBoard -Quiet
Push-Location $script:ProjectRoot
try { & idf.py -p $board.Port monitor; exit $LASTEXITCODE } finally { Pop-Location }
