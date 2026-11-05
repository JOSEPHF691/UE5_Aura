# Read the user's profile, then migrate only a uniquely named copy and reload it.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectRoot = 'D:\Unreal Projects\Aura'
$project = Join-Path $projectRoot 'Aura.uproject'
$saveRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\SaveGames'))
$sourceSave = [IO.Path]::GetFullPath((Join-Path $saveRoot 'AuraProfile.sav'))
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$slot = "AuraM21Migration_$runId"
$testSave = [IO.Path]::GetFullPath((Join-Path $saveRoot ($slot + '.sav')))
$logDir = Join-Path $projectRoot 'Saved\Logs'
$auditLog = Join-Path $logDir "M21RealSaveAudit_$runId.log"
$firstLog = Join-Path $logDir "M21RealSaveFirst_$runId.log"
$secondLog = Join-Path $logDir "M21RealSaveSecond_$runId.log"

if ([IO.Path]::GetDirectoryName($sourceSave) -ne $saveRoot -or
    [IO.Path]::GetFileName($sourceSave) -ne 'AuraProfile.sav' -or
    [IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
    [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav') -or
    -not (Test-Path -LiteralPath $sourceSave) -or
    (Test-Path -LiteralPath $testSave)) {
    throw 'Save path validation failed; no file was changed.'
}

$originalHash = (Get-FileHash -LiteralPath $sourceSave -Algorithm SHA256).Hash
$madeCopy = $false

function Invoke-M21SaveStage($flag, $extraArgs, $marker, $logPath) {
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} {2} -TestExit={3} -abslog="{4}"' -f
        $project, $flag, $extraArgs, $marker, $logPath
    $runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $runProcess.WaitForExit(50000)) {
            throw "M21 save audit timed out: $logPath"
        }
    }
    finally {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
    $line = Select-String -LiteralPath $logPath -Pattern "$marker Passed=" -SimpleMatch |
        Select-Object -Last 1
    if (-not $line -or $line.Line -notmatch [regex]::Escape("$marker Passed=true")) {
        throw "M21 save audit failed: $logPath"
    }
    return $line.Line
}

function Get-M21Payload($line) {
    if ($line -notmatch '(Level=\d+ XP=\d+ Souls=\d+ SkillPoints=\d+ ComboPoints=\d+ Bag=\d+ Gear=\d+ ItemsHash=\d+ Checkpoint=\S+ Seals=\d+ Claimed=\d+)') {
        throw "Missing growth/item identity payload: $line"
    }
    return $Matches[1]
}

try {
    $auditLine = Invoke-M21SaveStage 'AuraSaveReadOnlyM21Smoke' '-AuraNoPersistentSave -AuraM21AuditSlot=AuraProfile' 'AURA_M21_SAVE_READONLY' $auditLog
    $originalPayload = Get-M21Payload $auditLine
    if ($auditLine -notmatch 'Schema=(\d+)') { throw 'Original schema missing from audit.' }
    $oldSchema = [int]$Matches[1]

    Copy-Item -LiteralPath $sourceSave -Destination $testSave
    $madeCopy = $true
    $firstLine = Invoke-M21SaveStage 'AuraLoadedSaveM21Smoke' "-AuraSaveSlot=$slot" 'AURA_M21_SAVE_LOADED' $firstLog
    $secondLine = Invoke-M21SaveStage 'AuraLoadedSaveM21Smoke' "-AuraSaveSlot=$slot" 'AURA_M21_SAVE_LOADED' $secondLog

    $firstPayload = Get-M21Payload $firstLine
    $secondPayload = Get-M21Payload $secondLine
    if ($originalPayload -ne $firstPayload -or $firstPayload -ne $secondPayload) {
        throw "Growth or item identity changed across migration/reload. Logs: $auditLog, $firstLog, $secondLog"
    }
    if (-not (Select-String -LiteralPath $firstLog -Pattern "Version=$oldSchema" -SimpleMatch -Quiet) -or
        -not (Select-String -LiteralPath $secondLog -Pattern 'Version=4' -SimpleMatch -Quiet)) {
        throw "Save schema did not migrate once to v4. Logs: $firstLog, $secondLog"
    }
    Write-Output "M21 real profile copy migrated and reloaded without growth/item drift: $auditLog, $firstLog, $secondLog"
}
finally {
    if ($madeCopy -and (Test-Path -LiteralPath $testSave)) {
        if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
            [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav')) {
            throw 'Refusing to remove a save outside the isolated test slot.'
        }
        Remove-Item -LiteralPath $testSave
    }
    if ((Get-FileHash -LiteralPath $sourceSave -Algorithm SHA256).Hash -ne $originalHash) {
        throw 'Original AuraProfile.sav changed unexpectedly.'
    }
}
