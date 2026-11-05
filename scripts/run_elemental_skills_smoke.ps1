param([switch]$Capture, [switch]$Coop, [switch]$Lag)
$ErrorActionPreference = 'Stop'
$root = 'D:\Unreal Projects\Aura'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $root 'Aura.uproject'
$id = [guid]::NewGuid().ToString('N').Substring(0,10)
$serverLog = Join-Path $root "Saved\Logs\Elemental_$id.log"
$render = if ($Capture) { '-RenderOffscreen -ResX=1280 -ResY=800 -windowed -AuraElementalCapture' } else { '-nullrhi' }
$mode = if ($Coop) { '-server' } else { '-game' }
$port = Get-Random -Minimum 18000 -Maximum 24000
$network = if ($Lag) { '-PktLag=80 -PktLoss=2' } else { '' }
$launched = @()
$clients = @()
$profiles = @('AuraProfile.sav','AuraProfile_Editor.sav')
$before = @{}
foreach ($name in $profiles) {
    $path = Join-Path $root "Saved\SaveGames\$name"
    $before[$name] = if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path).Hash } else { $null }
}
try {
    $arguments = '"{0}" /Game/Map/DungeonDemo {1} -port={2} -unattended -nosplash {3} -nosound -AuraElementalSmoke -AuraNoPersistentSave -TestExit=AURA_ELEMENTAL_DONE -abslog="{4}"' -f $project,$mode,$port,$render,$serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $launched += $server
    if ($Coop) {
        $limit = [DateTime]::UtcNow.AddSeconds(35)
        do {
            $ready = (Test-Path -LiteralPath $serverLog) -and (Select-String -LiteralPath $serverLog -Pattern 'GameNetDriver.*listening on port' -Quiet)
            if (-not $ready) { Start-Sleep -Milliseconds 250 }
        } until ($ready -or $server.HasExited -or [DateTime]::UtcNow -ge $limit)
        if (-not $ready) { throw "Server not ready: $serverLog" }
        for ($i=1;$i -le 2;$i++) {
            $clientLog = Join-Path $root "Saved\Logs\Elemental_Client${i}_$id.log"
            $clients += $clientLog
            $arguments = '"{0}" 127.0.0.1:{1} -game -unattended -nosplash -nullrhi -nosound -AuraElementalObserve -AuraNoPersistentSave {2} -abslog="{3}"' -f $project,$port,$network,$clientLog
            $launched += Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
        }
    }
    if (-not $server.WaitForExit(55000)) { throw "Elemental test timeout: $serverLog" }
}
finally {
    foreach ($owned in $launched) { $owned.Refresh(); if (-not $owned.HasExited) { Stop-Process -Id $owned.Id } }
}
foreach ($name in $profiles) {
    $path = Join-Path $root "Saved\SaveGames\$name"
    $after = if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path).Hash } else { $null }
    if ($after -ne $before[$name]) { throw "Real profile modified: $name" }
}
Select-String -LiteralPath $serverLog -Pattern 'AURA_ELEMENTAL_CHECK|AURA_ELEMENTAL_RESULT|Error:|Fatal error' | ForEach-Object { $_.Line }
if (-not (Select-String -LiteralPath $serverLog -Pattern 'AURA_ELEMENTAL_RESULT Passed=true' -SimpleMatch -Quiet)) { throw "Elemental failed: $serverLog" }
foreach ($clientLog in $clients) {
    if (-not (Select-String -LiteralPath $clientLog -Pattern 'AURA_ELEMENTAL_CLIENT Passed=true' -SimpleMatch -Quiet)) { throw "Replication failed: $clientLog" }
    Select-String -LiteralPath $clientLog -Pattern 'AURA_ELEMENTAL_CLIENT' | ForEach-Object { $_.Line }
}
Write-Output "Elemental test passed: $serverLog"
