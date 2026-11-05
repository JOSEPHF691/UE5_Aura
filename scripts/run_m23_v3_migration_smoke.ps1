# Seed a v3 profile in a unique test slot, migrate to v4, and verify both reloads.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$saveRoot = [IO.Path]::GetFullPath((Join-Path $root 'Saved\SaveGames'))
$original = [IO.Path]::GetFullPath((Join-Path $saveRoot 'AuraProfile.sav'))
$originalHash = if (Test-Path -LiteralPath $original) {
    (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$slot = "AuraM23V3_$runId"
$testSave = [IO.Path]::GetFullPath((Join-Path $saveRoot ($slot + '.sav')))
$logDir = Join-Path $root 'Saved\Logs'
$seedLog = Join-Path $logDir "M23V3Seed_$runId.log"
$auditLog = Join-Path $logDir "M23V3Audit_$runId.log"
$firstLog = Join-Path $logDir "M23V3First_$runId.log"
$secondLog = Join-Path $logDir "M23V3Second_$runId.log"
if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
    [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav') -or
    (Test-Path -LiteralPath $testSave)) {
    throw 'Isolated v3 save path validation failed.'
}

function Invoke-M23V3Stage($flag, $extraArgs, $marker, $logPath) {
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} {2} -TestExit={3} -abslog="{4}"' -f
        $project, $flag, $extraArgs, $marker, $logPath
    $runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $runProcess.WaitForExit(50000)) { throw "M23 v3 stage timed out: $logPath" }
    }
    finally {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern "$marker Passed=true" -SimpleMatch -Quiet)) {
        throw "M23 v3 stage failed: $logPath"
    }
}

try {
    Invoke-M23V3Stage 'AuraSeedV3SaveM23Smoke' "-AuraNoPersistentSave -AuraM23AuditSlot=$slot" 'AURA_M23_V3_SEED' $seedLog
    if (-not (Test-Path -LiteralPath $testSave)) { throw 'v3 seed slot was not created.' }
    Invoke-M23V3Stage 'AuraSaveReadOnlyM21Smoke' "-AuraNoPersistentSave -AuraM21AuditSlot=$slot" 'AURA_M21_SAVE_READONLY' $auditLog
    Invoke-M23V3Stage 'AuraLoadedSaveM21Smoke' "-AuraSaveSlot=$slot" 'AURA_M21_SAVE_LOADED' $firstLog
    Invoke-M23V3Stage 'AuraLoadedSaveM21Smoke' "-AuraSaveSlot=$slot" 'AURA_M21_SAVE_LOADED' $secondLog
    if (-not (Select-String -LiteralPath $auditLog -Pattern 'Schema=3 Level=10 XP=1730 Souls=321 SkillPoints=5' -SimpleMatch -Quiet) -or
        -not (Select-String -LiteralPath $firstLog -Pattern 'Version=3 Level=10' -SimpleMatch -Quiet) -or
        -not (Select-String -LiteralPath $secondLog -Pattern 'Version=4 Level=10' -SimpleMatch -Quiet) -or
        -not (Select-String -LiteralPath $secondLog -Pattern 'SkillPoints=5' -SimpleMatch -Quiet)) {
        throw "v3-to-v4 growth migration was not stable: $auditLog, $firstLog, $secondLog"
    }
    Write-Output "M23 v3-to-v4 isolated migration passed: $auditLog, $firstLog, $secondLog"
}
finally {
    if (Test-Path -LiteralPath $testSave) {
        if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
            [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav')) {
            throw 'Refusing to remove a save outside the isolated v3 test slot.'
        }
        Remove-Item -LiteralPath $testSave
    }
    $afterHash = if (Test-Path -LiteralPath $original) {
        (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
    } else { $null }
    if ($afterHash -ne $originalHash) {
        throw 'Original AuraProfile.sav changed unexpectedly.'
    }
}
