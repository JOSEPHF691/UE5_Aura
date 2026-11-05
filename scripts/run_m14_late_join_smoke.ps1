# Two players bind the team seals; one leaves and a new client joins the active boss room.
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$serverLog = Join-Path $logDir "M14LateDedicatedServer_$runId.log"
$clientOneLog = Join-Path $logDir "M14LateClient1_$runId.log"
$clientTwoLog = Join-Path $logDir "M14LateClient2_$runId.log"
$clientThreeLog = Join-Path $logDir "M14LateClient3_$runId.log"
$launched = @()

function Wait-ForLogMarker($path, $marker, $timeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path -LiteralPath $path) -and
            (Select-String -Path $path -Pattern $marker -SimpleMatch -Quiet)) {
            return
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out waiting for $marker in $path"
}

try {
    # Keep the server alive until the late joiner has actually observed replicated state.
    # A fixed TestExit timer can otherwise close the server before client 3's observer fires.
    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port=7800 -unattended -nosplash -nullrhi -nosound -AuraSealCoopM14Smoke -abslog="{1}"' -f $project, $serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 2

    $clientOneArgs = '"{0}" 127.0.0.1:7800 -game -unattended -nosplash -nullrhi -nosound -AuraSealCoopM14Observe -abslog="{1}"' -f $project, $clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1

    $clientTwoArgs = '"{0}" 127.0.0.1:7800 -game -unattended -nosplash -nullrhi -nosound -AuraSealCoopM14Observe -abslog="{1}"' -f $project, $clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo

    Wait-ForLogMarker $serverLog 'AURA_M14_COOP_SERVER Passed=true' 35
    Wait-ForLogMarker $clientOneLog 'AURA_M14_COOP_CLIENT Passed=true' 25
    Stop-Process -Id $clientOne.Id
    Start-Sleep -Seconds 1

    $clientThreeArgs = '"{0}" 127.0.0.1:7800 -game -unattended -nosplash -nullrhi -nosound -AuraSealCoopM14Observe -abslog="{1}"' -f $project, $clientThreeLog
    $clientThree = Start-Process -FilePath $editor -ArgumentList $clientThreeArgs -PassThru -WindowStyle Hidden
    $launched += $clientThree
    Wait-ForLogMarker $clientThreeLog 'AURA_M14_COOP_CLIENT Passed=true' 25

    Wait-ForLogMarker $serverLog 'AURA_M14_COOP_SERVER_DONE' 35
}
finally {
    foreach ($launchedProcess in $launched) {
        $launchedProcess.Refresh()
        if (-not $launchedProcess.HasExited) {
            Stop-Process -Id $launchedProcess.Id
        }
    }
}

$lateJoin = Select-String -Path $serverLog -Pattern 'ActiveRoom=true' -SimpleMatch
$clientTwoResult = Select-String -Path $clientTwoLog -Pattern 'AURA_M14_COOP_CLIENT Passed=true' -SimpleMatch
$clientThreeResult = Select-String -Path $clientThreeLog -Pattern 'AURA_M14_COOP_CLIENT Passed=true' -SimpleMatch
Write-Output ('ActiveRoomJoin: ' + [bool]$lateJoin)
Write-Output ('RemainingClient: ' + [bool]$clientTwoResult)
Write-Output ('LateClient: ' + [bool]$clientThreeResult)
if (-not ($lateJoin -and $clientTwoResult -and $clientThreeResult)) {
    throw 'M14 late-join regression failed; inspect M14Late* logs.'
}
