$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$serverLog = Join-Path $logDir ("M25InventoryCoopServer_$runId.log")
$clientOneLog = Join-Path $logDir ("M25InventoryCoopClient1_$runId.log")
$clientTwoLog = Join-Path $logDir ("M25InventoryCoopClient2_$runId.log")
$launched = @()

try {
    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port=7799 -unattended -nosplash -nullrhi -nosound -AuraInventoryMoveCoopM25Smoke -AuraNoPersistentSave -TestExit=AURA_M25_INVENTORY_COOP_DONE -abslog="{1}"' -f $project, $serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 2

    $clientOneArgs = '"{0}" 127.0.0.1:7799 -game -unattended -nosplash -nullrhi -nosound -AuraInventoryMoveCoopM25Observe -AuraNoPersistentSave -abslog="{1}"' -f $project, $clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1

    $clientTwoArgs = '"{0}" 127.0.0.1:7799 -game -unattended -nosplash -nullrhi -nosound -AuraInventoryMoveCoopM25Observe -AuraNoPersistentSave -abslog="{1}"' -f $project, $clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo

    if (-not $server.WaitForExit(45000)) {
        throw "M25 inventory co-op server timed out: $serverLog"
    }
    Start-Sleep -Seconds 1
}
finally {
    foreach ($launchedProcess in $launched) {
        $launchedProcess.Refresh()
        if (-not $launchedProcess.HasExited) {
            Stop-Process -Id $launchedProcess.Id
        }
    }
}

foreach ($check in @(
    @{ Path = $serverLog; Marker = 'AURA_M25_INVENTORY_COOP_SERVER Passed=true' },
    @{ Path = $clientOneLog; Marker = 'AURA_M25_INVENTORY_COOP_CLIENT Passed=true' },
    @{ Path = $clientTwoLog; Marker = 'AURA_M25_INVENTORY_COOP_CLIENT Passed=true' }
)) {
    if (-not (Select-String -LiteralPath $check.Path -Pattern $check.Marker -SimpleMatch -Quiet)) {
        throw "M25 inventory co-op failed: $($check.Path)"
    }
    Write-Output "Passed: $($check.Path)"
}
