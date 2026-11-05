# Old combat/NPC smoke paths now unlock their combos through M21 growth rules.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)

function Invoke-M21Regression($flag, $marker) {
    $logPath = Join-Path $logDir ("M21Regression_{0}_{1}.log" -f $flag, $runId)
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} -AuraNoPersistentSave -TestExit={2} -abslog="{3}"' -f
        $project, $flag, $marker, $logPath
    $runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $runProcess.WaitForExit(50000)) {
            throw "M21 regression timed out: $logPath"
        }
    }
    finally {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern "$marker Passed=true" -SimpleMatch -Quiet)) {
        throw "M21 regression failed: $logPath"
    }
    Write-Output "$flag passed: $logPath"
}

Invoke-M21Regression 'AuraServiceUIM17Smoke' 'AURA_M17_SERVICE_UI'
Invoke-M21Regression 'AuraComboM18Smoke' 'AURA_M18_COMBO'
Invoke-M21Regression 'AuraLegendaryM16Smoke' 'AURA_M16_LEGENDARY'
Invoke-M21Regression 'AuraComboSmoke' 'AURA_COMBO_SMOKE'
Invoke-M21Regression 'AuraSkillRankSmoke' 'AURA_SKILL_RANK_SMOKE'
Invoke-M21Regression 'AuraFractureRankM21Smoke' 'AURA_M21_FRACTURE'
