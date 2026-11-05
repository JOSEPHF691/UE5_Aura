# Isolated dedicated-server/dual-client validation of M23 personal economy replication.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$serverLog = Join-Path $logDir "M23CoopServer_$runId.log"
$clientOneLog = Join-Path $logDir "M23CoopClient1_$runId.log"
$clientTwoLog = Join-Path $logDir "M23CoopClient2_$runId.log"
$launched = @()

try {
    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port=7817 -unattended -nosplash -nullrhi -nosound -AuraEconomyCoopM23Smoke -AuraNoPersistentSave -TestExit=AURA_M23_COOP_SERVER_DONE -abslog="{1}"' -f $project, $serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 2

    $clientOneArgs = '"{0}" 127.0.0.1:7817 -game -unattended -nosplash -nullrhi -nosound -AuraEconomyCoopM23Observe -AuraNoPersistentSave -abslog="{1}"' -f $project, $clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1

    $clientTwoArgs = '"{0}" 127.0.0.1:7817 -game -unattended -nosplash -nullrhi -nosound -AuraEconomyCoopM23Observe -AuraNoPersistentSave -abslog="{1}"' -f $project, $clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo

    if (-not $server.WaitForExit(60000)) {
        throw "M23 co-op server timed out: $serverLog"
    }
    Start-Sleep -Seconds 1
    foreach ($check in @(
        @($serverLog, 'AURA_M23_COOP_SERVER Passed=true'),
        @($clientOneLog, 'AURA_M23_COOP_CLIENT Passed=true'),
        @($clientTwoLog, 'AURA_M23_COOP_CLIENT Passed=true')
    )) {
        if (-not (Select-String -LiteralPath $check[0] -Pattern $check[1] -SimpleMatch -Quiet)) {
            throw "M23 co-op check failed: $($check[0])"
        }
    }
    Write-Output "M23 co-op economy passed: $serverLog, $clientOneLog, $clientTwoLog"
}
finally {
    foreach ($runProcess in $launched) {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
}
