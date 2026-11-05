# Verify elite skill timing, geometry, cleanup, and non-locking damage in a disposable run.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)

foreach ($case in @(
    @{ Name = 'Variant'; Flag = 'AuraEliteVariantSmoke'; Marker = 'AURA_ELITE_VARIANT Passed=true'; Exit = 'AURA_ELITE_VARIANT' },
    @{ Name = 'Skills'; Flag = 'AuraEliteSkillSmoke'; Marker = 'AURA_ELITE_SKILLS Passed=true'; Exit = 'AURA_ELITE_SKILLS' }
)) {
    $logPath = Join-Path $root ("Saved\Logs\Elite{0}_{1}.log" -f $case.Name, $runId)
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} -AuraNoPersistentSave -TestExit={2} -abslog="{3}"' -f
        $project, $case.Flag, $case.Exit, $logPath
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $process.WaitForExit(50000)) { throw "Elite $($case.Name) timed out: $logPath" }
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern $case.Marker -SimpleMatch -Quiet)) {
        throw "Elite $($case.Name) failed: $logPath"
    }
    if ($case.Name -eq 'Skills') {
        $fieldHits = @(Select-String -LiteralPath $logPath -Pattern 'ELITE_SHROOM_FIELD_HIT Enemy=' -SimpleMatch)
        if ($fieldHits.Count -ne 1) {
            throw "Elite Shroom field must hit the stationary test player exactly once: $logPath"
        }
    }
    Write-Output "Elite $($case.Name) passed: $logPath"
}

$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
