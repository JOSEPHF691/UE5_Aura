# Temporary dedicated server plus two clients; stop only the processes launched here.
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$serverLog = Join-Path $logDir 'M14SealDedicatedServer.log'
$clientOneLog = Join-Path $logDir 'M14SealClient1.log'
$clientTwoLog = Join-Path $logDir 'M14SealClient2.log'
$launched = @()

try {
    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port=7799 -unattended -nosplash -nullrhi -nosound -AuraSealCoopM14Smoke -TestExit=AURA_M14_COOP_SERVER_DONE -abslog="{1}"' -f $project, $serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 2

    $clientOneArgs = '"{0}" 127.0.0.1:7799 -game -unattended -nosplash -nullrhi -nosound -AuraSealCoopM14Observe -abslog="{1}"' -f $project, $clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1

    $clientTwoArgs = '"{0}" 127.0.0.1:7799 -game -unattended -nosplash -nullrhi -nosound -AuraSealCoopM14Observe -abslog="{1}"' -f $project, $clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo

    if (-not $server.WaitForExit(45000)) {
        throw 'M14 dedicated server did not finish within 45 seconds.'
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

$serverResult = Select-String -Path $serverLog -Pattern 'AURA_M14_COOP_SERVER Passed=true' -SimpleMatch
$clientOneResult = Select-String -Path $clientOneLog -Pattern 'AURA_M14_COOP_CLIENT Passed=true' -SimpleMatch
$clientTwoResult = Select-String -Path $clientTwoLog -Pattern 'AURA_M14_COOP_CLIENT Passed=true' -SimpleMatch
Write-Output ('Server: ' + [bool]$serverResult)
Write-Output ('Client1: ' + [bool]$clientOneResult)
Write-Output ('Client2: ' + [bool]$clientTwoResult)
if (-not ($serverResult -and $clientOneResult -and $clientTwoResult)) {
    throw 'M14 co-op seal smoke failed; inspect M14Seal* logs.'
}
