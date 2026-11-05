$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$logPath = Join-Path $root ("Saved\Logs\ShamanGroundBurst_{0}.log" -f $runId)
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraShamanGroundBurstSmoke -AuraNoPersistentSave -TestExit="Shaman ranged smoke test completed" -abslog="{1}"' -f $project, $logPath
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $process.WaitForExit(45000)) {
        throw "Shaman ground-burst smoke timed out: $logPath"
    }
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
$result = Select-String -LiteralPath $logPath -Pattern 'Shaman ranged smoke test completed. Passed=' -SimpleMatch |
    Select-Object -Last 1
if (-not $result -or $result.Line -notmatch 'Passed=true') {
    $diagnostic = Select-String -LiteralPath $logPath -Pattern 'Shaman|GroundBurst|Error:|Fatal error' |
        Select-Object -Last 30 | ForEach-Object { $_.Line }
    throw "Shaman ground-burst smoke failed: $logPath`n$diagnostic"
}
$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
Write-Output $result.Line
Write-Output "Shaman ground-burst smoke passed: $logPath"
