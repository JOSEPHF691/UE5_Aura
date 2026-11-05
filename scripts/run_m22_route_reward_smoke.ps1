# Test first-clear quota and reload only in a newly named isolated save slot.
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
$slot = "AuraM22Reward_$runId"
$testSave = [IO.Path]::GetFullPath((Join-Path $saveRoot ($slot + '.sav')))
$firstLog = Join-Path $root "Saved\Logs\M22RewardFirst_$runId.log"
$reloadLog = Join-Path $root "Saved\Logs\M22RewardReload_$runId.log"
if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
    [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav') -or
    (Test-Path -LiteralPath $testSave)) {
    throw 'Isolated reward save path validation failed.'
}

function Invoke-M22Stage($flag, $marker, $logPath) {
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} -AuraSaveSlot={2} -TestExit={3} -abslog="{4}"' -f
        $project, $flag, $slot, $marker, $logPath
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $process.WaitForExit(50000)) { throw "M22 reward stage timed out: $logPath" }
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern "$marker Passed=true" -SimpleMatch -Quiet)) {
        throw "M22 reward stage failed: $logPath"
    }
}

try {
    Invoke-M22Stage 'AuraRouteRewardsM22Smoke' 'AURA_M22_ROUTE_REWARD' $firstLog
    if (-not (Test-Path -LiteralPath $testSave)) {
        throw "First-clear test did not write isolated save: $testSave"
    }
    Invoke-M22Stage 'AuraRouteRewardsM22ReloadSmoke' 'AURA_M22_ROUTE_RELOAD' $reloadLog
    Write-Output "M22 first-clear reward and reload passed: $firstLog, $reloadLog"
}
finally {
    if (Test-Path -LiteralPath $testSave) {
        if ([IO.Path]::GetDirectoryName($testSave) -ne $saveRoot -or
            [IO.Path]::GetFileName($testSave) -ne ($slot + '.sav')) {
            throw 'Refusing to remove a save outside the isolated test slot.'
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
