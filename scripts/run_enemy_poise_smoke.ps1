# Verify hit gauge, one-time stagger, immunity, reset, and passive recovery.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$logPath = Join-Path $root "Saved\Logs\EnemyPoise_$runId.log"
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraEnemyPoiseSmoke -AuraNoPersistentSave -TestExit=AURA_ENEMY_POISE -abslog="{1}"' -f $project, $logPath
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $process.WaitForExit(55000)) { throw "Poise smoke timed out: $logPath" }
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
if (-not (Select-String -LiteralPath $logPath -Pattern 'AURA_ENEMY_POISE Passed=true' -SimpleMatch -Quiet)) {
    $result = Select-String -LiteralPath $logPath -Pattern 'AURA_ENEMY_POISE' -SimpleMatch | Select-Object -Last 1
    throw "Poise smoke failed: $logPath`n$result"
}
$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
Write-Output "Enemy poise smoke passed: $logPath"
