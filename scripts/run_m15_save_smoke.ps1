# Two separate processes: write a versioned profile, then restore it without duplication.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$projectRoot = 'D:\Unreal Projects\Aura'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$runId = [guid]::NewGuid().ToString('N')
$slot = 'AuraM15Smoke_' + $runId
$seedLog = Join-Path $logDir ("M15Seed_" + $runId.Substring(0, 8) + '.log')
$verifyLog = Join-Path $logDir ("M15Verify_" + $runId.Substring(0, 8) + '.log')

function Invoke-M15Stage($flag, $marker, $logPath) {
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} -AuraSaveSlot={2} -TestExit={3} -abslog="{4}"' -f
        $project, $flag, $slot, $marker, $logPath
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $process.WaitForExit(45000)) {
            throw "M15 $flag timed out."
        }
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern "$marker Passed=true" -SimpleMatch -Quiet)) {
        throw "M15 $flag failed. Inspect $logPath"
    }
}

Invoke-M15Stage 'AuraSeedSaveM15Smoke' 'AURA_M15_SEED_DONE' $seedLog
Invoke-M15Stage 'AuraVerifySaveM15Smoke' 'AURA_M15_VERIFY_DONE' $verifyLog

# Remove only this run's isolated test slot, never a user profile or a broad folder.
$saveRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\SaveGames'))
$testSave = [IO.Path]::GetFullPath((Join-Path $saveRoot ($slot + '.sav')))
if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
    [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav')) {
    throw 'Test save path escaped the intended SaveGames directory.'
}
if (Test-Path -LiteralPath $testSave) {
    Remove-Item -LiteralPath $testSave
}
Write-Output "M15 cross-process save/load, idempotent inventory/effects and skill pages: passed. Logs: $seedLog, $verifyLog"
