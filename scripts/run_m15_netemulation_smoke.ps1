param(
    [ValidateSet('Purchase', 'Combo', 'Seals')][string]$Scenario = 'Purchase',
    [ValidateSet(100, 200)][int]$LagMs = 100,
    [ValidateSet(1, 3)][int]$LossPct = 1
)

# Dedicated + two real clients. Only processes launched by this script are stopped.
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$prefix = "M15${Scenario}_${LagMs}ms_${LossPct}pct_$runId"
$serverLog = Join-Path $logDir "${prefix}_Server.log"
$clientOneLog = Join-Path $logDir "${prefix}_Client1.log"
$clientTwoLog = Join-Path $logDir "${prefix}_Client2.log"
$port = if ($Scenario -eq 'Purchase') { 7811 } elseif ($Scenario -eq 'Combo') { 7812 } else { 7813 }
$emulation = "-PktLag=$LagMs -PktLoss=$LossPct"
$launched = @()

switch ($Scenario) {
    'Purchase' {
        $serverFlags = '-AuraNetM15Smoke -TestExit=AURA_M15_NET_SERVER_DONE'
        $clientFlags = '-AuraNetM15Observe'
        $serverMarker = 'AURA_M15_NET_SERVER_DONE Passed=true'
        $clientMarker = 'AURA_M15_NET_CLIENT Passed=true'
    }
    'Combo' {
        $serverFlags = '-AuraComboSmoke -AuraComboSmokeDelay=10 -TestExit=AURA_COMBO_SMOKE'
        $clientFlags = '-AuraComboObserve -AuraComboObserveDelay=10.5'
        $serverMarker = 'AURA_COMBO_SMOKE Passed=true'
        $clientMarker = 'AURA_COMBO_OBSERVE'
    }
    'Seals' {
        $serverFlags = '-AuraSealCoopM14Smoke -TestExit=AURA_M14_COOP_SERVER_DONE'
        $clientFlags = '-AuraSealCoopM14Observe'
        $serverMarker = 'AURA_M14_COOP_SERVER Passed=true'
        $clientMarker = 'AURA_M14_COOP_CLIENT Passed=true'
    }
}

try {
    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port={1} -unattended -nosplash -nullrhi -nosound {2} {3} -abslog="{4}"' -f $project, $port, $emulation, $serverFlags, $serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 2

    $clientOneArgs = '"{0}" 127.0.0.1:{1} -game -unattended -nosplash -nullrhi -nosound {2} {3} -abslog="{4}"' -f $project, $port, $emulation, $clientFlags, $clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1

    $clientTwoArgs = '"{0}" 127.0.0.1:{1} -game -unattended -nosplash -nullrhi -nosound {2} {3} -abslog="{4}"' -f $project, $port, $emulation, $clientFlags, $clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo

    if (-not $server.WaitForExit(70000)) {
        throw "M15 $Scenario dedicated server timed out."
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

$serverResult = Select-String -LiteralPath $serverLog -Pattern $serverMarker -SimpleMatch -Quiet
$clientOneResult = Select-String -LiteralPath $clientOneLog -Pattern $clientMarker -SimpleMatch -Quiet
$clientTwoResult = Select-String -LiteralPath $clientTwoLog -Pattern $clientMarker -SimpleMatch -Quiet
$emulationResult = (Select-String -LiteralPath $serverLog -Pattern "PktLag set to $LagMs" -SimpleMatch -Quiet) -and
    (Select-String -LiteralPath $serverLog -Pattern "PktLoss set to $LossPct" -SimpleMatch -Quiet)
if ($Scenario -eq 'Combo') {
    $visible = (Select-String -LiteralPath $clientOneLog -Pattern 'ActivePersonalMarks=1' -SimpleMatch -Quiet) -or
        (Select-String -LiteralPath $clientTwoLog -Pattern 'ActivePersonalMarks=1' -SimpleMatch -Quiet)
    $clientOneResult = $clientOneResult -and $visible
    $clientTwoResult = $clientTwoResult -and $visible
}
Write-Output ("Scenario={0} Lag={1}ms Loss={2}% Emulation={3} Server={4} Client1={5} Client2={6}" -f
    $Scenario, $LagMs, $LossPct, $emulationResult, $serverResult, $clientOneResult, $clientTwoResult)
if (-not ($serverResult -and $clientOneResult -and $clientTwoResult -and $emulationResult)) {
    throw "M15 network emulation failed. Inspect $prefix logs."
}
