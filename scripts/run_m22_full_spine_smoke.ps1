# Deterministically clear all eight mainline reward rooms in a disposable run.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$logPath = Join-Path $root "Saved\Logs\M22FullSpine_$runId.log"
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraFullSpineM22Smoke -AuraNoPersistentSave -TestExit=AURA_M22_FULL_SPINE -abslog="{1}"' -f $project, $logPath
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $process.WaitForExit(50000)) { throw "M22 spine smoke timed out: $logPath" }
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
$rooms = @(Select-String -LiteralPath $logPath -Pattern 'AURA_M22_SPINE_ROOM Id=' -SimpleMatch)
if ($rooms.Count -ne 8 -or
    @($rooms | Where-Object { $_.Line -notmatch 'Passed=true' }).Count -ne 0 -or
    -not (Select-String -LiteralPath $logPath -Pattern 'AURA_M22_FULL_SPINE Passed=true Rooms=8' -SimpleMatch -Quiet)) {
    throw "M22 spine smoke failed: $logPath"
}
$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
Write-Output "M22 full spine passed: $logPath"
