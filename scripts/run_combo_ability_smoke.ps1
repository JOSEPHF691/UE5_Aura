$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logDir = 'D:\Unreal Projects\Aura\Saved\Logs'
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)

foreach ($rankThreeQ in @($false, $true)) {
    $variant = if ($rankThreeQ) { 'Rank3Q' } else { 'Rank1Q' }
    $extraFlag = if ($rankThreeQ) { ' -AuraComboAbilityRank3Q' } else { '' }
    $logPath = Join-Path $logDir ("ComboAbility_{0}_{1}.log" -f $variant, $runId)
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraComboAbilitySmoke -AuraNoPersistentSave{1} -TestExit=AURA_COMBO_ABILITY_SMOKE -abslog="{2}"' -f
        $project, $extraFlag, $logPath
    $runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $runProcess.WaitForExit(50000)) {
            throw "Combo ability smoke timed out: $logPath"
        }
    }
    finally {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern 'AURA_COMBO_ABILITY_SMOKE Passed=true' -SimpleMatch -Quiet)) {
        throw "Combo ability smoke failed: $logPath"
    }
    Write-Output "Combo ability $variant passed: $logPath"
}
