# Generate a representative isolated v2 save, migrate to v4, and reload twice.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectRoot = 'D:\Unreal Projects\Aura'
$project = Join-Path $projectRoot 'Aura.uproject'
$saveRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\SaveGames'))
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$slot = "AuraM21V2_$runId"
$testSave = [IO.Path]::GetFullPath((Join-Path $saveRoot ($slot + '.sav')))
$logDir = Join-Path $projectRoot 'Saved\Logs'
$seedLog = Join-Path $logDir "M21V2Seed_$runId.log"
$auditLog = Join-Path $logDir "M21V2Audit_$runId.log"
$firstLog = Join-Path $logDir "M21V2First_$runId.log"
$secondLog = Join-Path $logDir "M21V2Second_$runId.log"
if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
    [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav') -or
    (Test-Path -LiteralPath $testSave)) {
    throw 'Isolated v2 test path validation failed.'
}

function Invoke-M21V2Stage($flag, $extraArgs, $marker, $logPath) {
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} {2} -TestExit={3} -abslog="{4}"' -f
        $project, $flag, $extraArgs, $marker, $logPath
    $runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $runProcess.WaitForExit(50000)) { throw "M21 v2 stage timed out: $logPath" }
    }
    finally {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
    $line = Select-String -LiteralPath $logPath -Pattern "$marker Passed=" -SimpleMatch |
        Select-Object -Last 1
    if (-not $line -or $line.Line -notmatch [regex]::Escape("$marker Passed=true")) {
        throw "M21 v2 stage failed: $logPath"
    }
    return $line.Line
}

function Get-M21V2Payload($line) {
    if ($line -notmatch '(Level=\d+ XP=\d+ Souls=\d+ SkillPoints=\d+ ComboPoints=\d+ Bag=\d+ Gear=\d+ ItemsHash=\d+ Checkpoint=\S+ Seals=\d+ Claimed=\d+)') {
        throw "Missing migrated profile fields: $line"
    }
    return $Matches[1]
}

try {
    Invoke-M21V2Stage 'AuraSeedV2SaveM21Smoke' "-AuraNoPersistentSave -AuraM21AuditSlot=$slot" 'AURA_M21_V2_SEED' $seedLog | Out-Null
    $auditLine = Invoke-M21V2Stage 'AuraSaveReadOnlyM21Smoke' "-AuraNoPersistentSave -AuraM21AuditSlot=$slot" 'AURA_M21_SAVE_READONLY' $auditLog
    if ($auditLine -notmatch 'Schema=2') { throw "Seed was not schema v2: $auditLog" }
    $before = Get-M21V2Payload $auditLine

    $firstLine = Invoke-M21V2Stage 'AuraLoadedSaveM21Smoke' "-AuraSaveSlot=$slot" 'AURA_M21_SAVE_LOADED' $firstLog
    $secondLine = Invoke-M21V2Stage 'AuraLoadedSaveM21Smoke' "-AuraSaveSlot=$slot" 'AURA_M21_SAVE_LOADED' $secondLog
    if ($before -ne (Get-M21V2Payload $firstLine) -or
        $before -ne (Get-M21V2Payload $secondLine) -or
        -not (Select-String -LiteralPath $firstLog -Pattern 'Version=2' -SimpleMatch -Quiet) -or
        -not (Select-String -LiteralPath $secondLog -Pattern 'Version=4' -SimpleMatch -Quiet)) {
        throw "v2 migration was not stable. Logs: $auditLog, $firstLog, $secondLog"
    }
    Write-Output "M21 v2-to-v4 migration preserved growth, item IDs, checkpoint and progress: $auditLog, $firstLog, $secondLog"
}
finally {
    if (Test-Path -LiteralPath $testSave) {
        if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
            [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav')) {
            throw 'Refusing to remove a save outside the isolated v2 test slot.'
        }
        Remove-Item -LiteralPath $testSave
    }
}
