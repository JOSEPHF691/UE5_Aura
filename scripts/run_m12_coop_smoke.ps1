# Launch exactly one temporary dedicated server and two clients, then stop only
# those processes. No user's existing editor/session is touched.
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$serverLog = Join-Path $logDir 'M12GrowthDedicatedServer.log'
$clientOneLog = Join-Path $logDir 'M12GrowthClient1.log'
$clientTwoLog = Join-Path $logDir 'M12GrowthClient2.log'
$launched = @()

try {
    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port=7797 -unattended -nosplash -nullrhi -nosound -AuraGrowthCoopM12Smoke -AuraPostBossCoopM12Smoke -TestExit=AURA_M12_COOP_SERVER_DONE -abslog="{1}"' -f $project, $serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 2

    $clientOneArgs = '"{0}" 127.0.0.1:7797 -game -unattended -nosplash -nullrhi -nosound -AuraGrowthCoopM12Observe -AuraPostBossCoopM12Smoke -abslog="{1}"' -f $project, $clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1

    $clientTwoArgs = '"{0}" 127.0.0.1:7797 -game -unattended -nosplash -nullrhi -nosound -AuraGrowthCoopM12Observe -AuraPostBossCoopM12Smoke -abslog="{1}"' -f $project, $clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo

    if (-not $server.WaitForExit(45000)) {
        throw 'Dedicated server did not finish the M12 co-op smoke within 45 seconds.'
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

$serverResult = Select-String -Path $serverLog -Pattern 'AURA_M12_COOP_SERVER Passed=true' -SimpleMatch
$clientOneResult = Select-String -Path $clientOneLog -Pattern 'AURA_M12_COOP_CLIENT Passed=true' -SimpleMatch
$clientTwoResult = Select-String -Path $clientTwoLog -Pattern 'AURA_M12_COOP_CLIENT Passed=true' -SimpleMatch
$serverCampResult = Select-String -Path $serverLog -Pattern 'AURA_M12_COOP_CAMP_SERVER Passed=true' -SimpleMatch
$clientOneCampResult = Select-String -Path $clientOneLog -Pattern 'AURA_M12_COOP_CAMP_CLIENT Passed=true' -SimpleMatch
$clientTwoCampResult = Select-String -Path $clientTwoLog -Pattern 'AURA_M12_COOP_CAMP_CLIENT Passed=true' -SimpleMatch
Write-Output ('Server: ' + [bool]$serverResult)
Write-Output ('Client1: ' + [bool]$clientOneResult)
Write-Output ('Client2: ' + [bool]$clientTwoResult)
Write-Output ('CampServer: ' + [bool]$serverCampResult)
Write-Output ('CampClient1: ' + [bool]$clientOneCampResult)
Write-Output ('CampClient2: ' + [bool]$clientTwoCampResult)
if (-not ($serverResult -and $clientOneResult -and $clientTwoResult -and
    $serverCampResult -and $clientOneCampResult -and $clientTwoCampResult)) {
    throw 'M12 co-op growth smoke failed; inspect the three M12Growth* logs.'
}
