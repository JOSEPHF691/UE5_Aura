$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)

foreach ($case in @(
    @{ Flag = 'AuraInventoryMoveM25Smoke'; Marker = 'AURA_M25_INVENTORY' },
    @{ Flag = 'AuraPursuitM25Smoke'; Marker = 'AURA_M25_PURSUIT' },
    @{ Flag = 'AuraServiceUIM17Smoke'; Marker = 'AURA_M17_SERVICE_UI' },
    @{ Flag = 'AuraInventorySmoke'; Marker = 'Inventory smoke test completed.' }
)) {
    $logPath = Join-Path $logDir ("M25_{0}_{1}.log" -f $case.Flag, $runId)
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} -AuraNoPersistentSave -TestExit="{2}" -abslog="{3}"' -f
        $project, $case.Flag, $case.Marker, $logPath
    $runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $runProcess.WaitForExit(50000)) {
            throw "Interaction smoke timed out: $logPath"
        }
    }
    finally {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern "$($case.Marker) Passed=true" -SimpleMatch -Quiet)) {
        $diagnostic = Select-String -LiteralPath $logPath -Pattern $case.Marker -SimpleMatch |
            Select-Object -Last 3 | ForEach-Object { $_.Line }
        throw "Interaction smoke failed: $logPath`n$diagnostic"
    }
    Write-Output "$($case.Flag) passed: $logPath"
}
