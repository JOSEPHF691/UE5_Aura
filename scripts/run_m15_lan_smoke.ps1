# Create and discover an OnlineSubsystemNull LAN room in two real game processes.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$hostLog = Join-Path $logDir "M15LanHost_$runId.log"
$guestLog = Join-Path $logDir "M15LanGuest_$runId.log"
$launched = @()

function Wait-ForLogMarker($path, $marker, $timeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path -LiteralPath $path) -and
            (Select-String -LiteralPath $path -Pattern $marker -SimpleMatch -Quiet)) {
            return
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out waiting for $marker in $path"
}

try {
    $hostArgs = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraNoPersistentSave -AuraLanHostM15Smoke -abslog="{1}"' -f $project, $hostLog
    $hostProcess = Start-Process -FilePath $editor -ArgumentList $hostArgs -PassThru -WindowStyle Hidden
    $launched += $hostProcess
    Wait-ForLogMarker $hostLog 'M15 LAN host created. Success=true' 30
    Start-Sleep -Seconds 2

    $guestArgs = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraNoPersistentSave -AuraLanJoinM15Smoke -abslog="{1}"' -f $project, $guestLog
    $guestProcess = Start-Process -FilePath $editor -ArgumentList $guestArgs -PassThru -WindowStyle Hidden
    $launched += $guestProcess
    Wait-ForLogMarker $guestLog 'AURA_M15_LAN_JOINED Passed=true' 65
    Wait-ForLogMarker $hostLog 'AURA_M15_LAN_HOST_CONNECTED Passed=true' 15
}
finally {
    foreach ($launchedProcess in $launched) {
        $launchedProcess.Refresh()
        if (-not $launchedProcess.HasExited) {
            Stop-Process -Id $launchedProcess.Id
        }
    }
}

Write-Output "LAN host, discovery, join and two-player travel: passed. Logs: $hostLog, $guestLog"
