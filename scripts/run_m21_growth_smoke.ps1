# M21 growth checks use fresh processes with persistence disabled.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$soloLog = Join-Path $logDir "M21GrowthSolo_$runId.log"
$serverLog = Join-Path $logDir "M21GrowthServer_$runId.log"
$clientOneLog = Join-Path $logDir "M21GrowthClient1_$runId.log"
$clientTwoLog = Join-Path $logDir "M21GrowthClient2_$runId.log"
$launched = @()

function Assert-Marker($path, $marker) {
    if (-not (Select-String -LiteralPath $path -Pattern $marker -SimpleMatch -Quiet)) {
        throw "Missing '$marker' in $path"
    }
}

try {
    $soloArgs = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraGrowthM21Smoke -AuraNoPersistentSave -TestExit=AURA_M21_GROWTH_SMOKE -abslog="{1}"' -f $project, $soloLog
    $solo = Start-Process -FilePath $editor -ArgumentList $soloArgs -PassThru -WindowStyle Hidden
    $launched += $solo
    if (-not $solo.WaitForExit(70000)) { throw "M21 solo test timed out: $soloLog" }
    Assert-Marker $soloLog 'AURA_M21_GROWTH_SMOKE Passed=true'
    Assert-Marker $soloLog 'AURA_FIREBOLT_VOLLEY_SMOKE Passed=true'

    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port=7809 -unattended -nosplash -nullrhi -nosound -AuraGrowthCoopM21Smoke -AuraNoPersistentSave -TestExit=AURA_M21_COOP_SERVER_DONE -abslog="{1}"' -f $project, $serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 2

    $clientOneArgs = '"{0}" 127.0.0.1:7809 -game -unattended -nosplash -nullrhi -nosound -AuraGrowthCoopM21Observe -AuraNoPersistentSave -abslog="{1}"' -f $project, $clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1

    $clientTwoArgs = '"{0}" 127.0.0.1:7809 -game -unattended -nosplash -nullrhi -nosound -AuraGrowthCoopM21Observe -AuraNoPersistentSave -abslog="{1}"' -f $project, $clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo

    if (-not $server.WaitForExit(60000)) { throw "M21 coop test timed out: $serverLog" }
    Start-Sleep -Seconds 1
    Assert-Marker $serverLog 'AURA_M21_COOP_SERVER Passed=true'
    Assert-Marker $clientOneLog 'AURA_M21_COOP_CLIENT Passed=true'
    Assert-Marker $clientTwoLog 'AURA_M21_COOP_CLIENT Passed=true'
    Write-Output "M21 growth smoke passed: $soloLog, $serverLog, $clientOneLog, $clientTwoLog"
}
finally {
    foreach ($runProcess in $launched) {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
}
