param([switch]$Capture)

# Exercises every enemy-skill Niagara family and verifies that looping systems
# are explicitly removed after their owning cast or hazard ends.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$logPath = Join-Path $root ("Saved\Logs\EnemySkillVFX_{0}.log" -f $runId)
$renderFlags = if ($Capture) {
    '-RenderOffscreen -ResX=1600 -ResY=900 -windowed -AuraEnemySkillVFXCapture'
} else { '-nullrhi' }
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nosound {1} -AuraEnemySkillVFXSmoke -AuraNoPersistentSave -TestExit=AURA_ENEMY_SKILL_VFX_RESULT -abslog="{2}"' -f
    $project, $renderFlags, $logPath
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $process.WaitForExit(55000)) {
        throw "Enemy skill VFX smoke timed out: $logPath"
    }
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
$result = Select-String -LiteralPath $logPath -Pattern 'AURA_ENEMY_SKILL_VFX_RESULT Passed=' -SimpleMatch |
    Select-Object -Last 1
if (-not $result -or $result.Line -notmatch 'Passed=true') {
    $diagnostic = Select-String -LiteralPath $logPath -Pattern 'AURA_ENEMY_SKILL_VFX|Error:|Fatal error' |
        Select-Object -Last 30 | ForEach-Object { $_.Line }
    throw "Enemy skill VFX smoke failed: $logPath`n$diagnostic"
}
$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
if ($Capture) {
    foreach ($name in @(
        'AuraEnemySkillVFX_005.png',
        'AuraEnemySkillVFX_033.png',
        'AuraEnemyCue_GoblinWindup.png',
        'AuraEnemyCue_GoblinStrike.png',
        'AuraEnemyCue_ShroomFadeIn.png',
        'AuraEnemyCue_ShroomReady.png',
        'AuraEnemyCue_ShroomBurst.png',
        'AuraEnemyCue_ShroomCrack.png')) {
        $screenshot = Join-Path $root ("Saved\Screenshots\WindowsEditor\{0}" -f $name)
        if (-not (Test-Path -LiteralPath $screenshot)) { throw "Missing screenshot: $screenshot" }
        Write-Output "Enemy skill VFX screenshot: $screenshot"
    }
}
Write-Output $result.Line
Write-Output "Enemy skill VFX smoke passed: $logPath"
