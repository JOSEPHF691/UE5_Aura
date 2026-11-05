# M16 editor-runtime smoke tests. Each stage has a fresh process and log.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectRoot = 'D:\Unreal Projects\Aura'
$project = Join-Path $projectRoot 'Aura.uproject'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$runId = [guid]::NewGuid().ToString('N')
$slot = 'AuraM16Smoke_' + $runId

function Invoke-M16Stage($flag, $marker, $extra) {
    $logPath = Join-Path $logDir ("M16_{0}_{1}.log" -f $flag, $runId.Substring(0, 8))
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} {2} -TestExit={3} -abslog="{4}"' -f
        $project, $flag, $extra, $marker, $logPath
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $process.WaitForExit(70000)) {
            throw "M16 $flag timed out; inspect $logPath"
        }
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern "$marker Passed=true" -SimpleMatch -Quiet)) {
        throw "M16 $flag failed; inspect $logPath"
    }
    Write-Output "$flag passed: $logPath"
}

Invoke-M16Stage 'AuraSealChoiceM16Smoke' 'AURA_M16_SEAL' '-AuraNoPersistentSave'
Invoke-M16Stage 'AuraLegendaryM16Smoke' 'AURA_M16_LEGENDARY' '-AuraNoPersistentSave'
Invoke-M16Stage 'AuraAnimationM16Smoke' 'AURA_M16_ANIMATION' '-AuraNoPersistentSave'
Invoke-M16Stage 'AuraAbyssM16Smoke' 'AURA_M16_ABYSS' '-AuraNoPersistentSave'
Invoke-M16Stage 'AuraWriteV1M16Smoke' 'AURA_M16_V1_SEED' "-AuraNoPersistentSave -AuraM16TestSlot=$slot"
Invoke-M16Stage 'AuraVerifyV1M16Smoke' 'AURA_M16_V1_MIGRATION' "-AuraSaveSlot=$slot -AuraM16TestSlot=$slot"

# Remove only the fresh, isolated test save once migration was verified.
$saveRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\SaveGames'))
$testSave = [IO.Path]::GetFullPath((Join-Path $saveRoot ($slot + '.sav')))
if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
    [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav')) {
    throw 'M16 test save path escaped SaveGames.'
}
if (Test-Path -LiteralPath $testSave) {
    Remove-Item -LiteralPath $testSave
}
Write-Output 'M16 runtime smoke: 4-choose-3 seals, Legendary combo effects, locomotion/montage slot, Abyss composition/health, v1-to-v2 save migration passed.'
