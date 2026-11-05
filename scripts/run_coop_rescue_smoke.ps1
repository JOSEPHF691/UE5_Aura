param([switch]$Listen, [switch]$Lag, [switch]$Capture)

$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 10)
$mode = if ($Listen) { 'Listen' } else { 'Dedicated' }
$port = Get-Random -Minimum 18000 -Maximum 24000
$serverLog = Join-Path $root "Saved\Logs\Rescue_${mode}_Server_$runId.log"
$clientLogs = @()
$launched = @()
$networkFlags = if ($Lag) { '-PktLag=80 -PktLoss=2' } else { '' }
$serverMode = if ($Listen) { '-game' } else { '-server' }
$renderFlags = if ($Capture) { '-RenderOffscreen -ResX=1280 -ResY=800 -windowed -AuraRescueCapture' } else { '-nullrhi' }
$serverRenderFlags = if ($Listen) { $renderFlags } else { '-nullrhi' }
$map = if ($Listen) { '/Game/Map/DungeonDemo?listen' } else { '/Game/Map/DungeonDemo' }
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) { (Get-FileHash -LiteralPath $profile).Hash } else { $null }
try {
    $serverArgs = '"{0}" {1} {2} -port={3} -unattended -nosplash {5} -nosound -AuraRescueCoopSmoke -AuraNoPersistentSave -TestExit=AURA_RESCUE_COOP_DONE -abslog="{4}"' -f $project, $map, $serverMode, $port, $serverLog, $serverRenderFlags
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    $readyUntil = [DateTime]::UtcNow.AddSeconds(35)
    do {
        if ($server.HasExited) { throw "Server exited before listening: $serverLog" }
        $ready = (Test-Path -LiteralPath $serverLog) -and
            (Select-String -LiteralPath $serverLog -Pattern 'GameNetDriver.*listening on port' -Quiet)
        if (-not $ready) { Start-Sleep -Milliseconds 300 }
    } until ($ready -or [DateTime]::UtcNow -ge $readyUntil)
    if (-not $ready) { throw "Server listen timeout: $serverLog" }
    $clientCount = if ($Listen) { 1 } else { 2 }
    for ($index = 1; $index -le $clientCount; $index++) {
        $clientLog = Join-Path $root "Saved\Logs\Rescue_${mode}_Client${index}_$runId.log"
        $clientLogs += $clientLog
        $clientArgs = '"{0}" 127.0.0.1:{1} -game -unattended -nosplash {4} -nosound -AuraRescueCoopSmoke -AuraNoPersistentSave {2} -abslog="{3}"' -f $project, $port, $networkFlags, $clientLog, $renderFlags
        $launched += Start-Process -FilePath $editor -ArgumentList $clientArgs -PassThru -WindowStyle Hidden
        Start-Sleep -Milliseconds 700
    }
    if (-not $server.WaitForExit(55000)) { throw "Rescue smoke timed out: $serverLog" }
}
finally {
    foreach ($ownedProcess in $launched) {
        $ownedProcess.Refresh()
        if (-not $ownedProcess.HasExited) { Stop-Process -Id $ownedProcess.Id }
    }
}
$afterHash = if (Test-Path -LiteralPath $profile) { (Get-FileHash -LiteralPath $profile).Hash } else { $null }
if ($afterHash -ne $beforeHash) { throw 'AuraProfile.sav changed unexpectedly.' }
$result = Select-String -LiteralPath $serverLog -Pattern 'AURA_RESCUE_COOP_RESULT Passed=true' -SimpleMatch
if (-not $result) {
    Select-String -LiteralPath $serverLog -Pattern 'AURA_RESCUE|Rescue started|Rescue completed|Error:|Fatal error' |
        Select-Object -Last 55 | ForEach-Object { Write-Output $_.Line }
    throw "Rescue smoke failed: $serverLog"
}
foreach ($clientLog in $clientLogs) {
    if (-not (Select-String -LiteralPath $clientLog -Pattern 'AURA_RESCUE_CLIENT_OBSERVED Flags=95' -SimpleMatch -Quiet)) {
        throw "Client did not observe spectating, progress, camera direction, and camera return: $clientLog"
    }
}
$result | ForEach-Object { Write-Output $_.Line }
Write-Output "Rescue $mode smoke passed: $serverLog"
$clientLogs | ForEach-Object { Write-Output "Client evidence: $_" }
