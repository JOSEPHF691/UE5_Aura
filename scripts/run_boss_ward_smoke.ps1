param([switch]$Capture)

$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$logPath = Join-Path $root ("Saved\Logs\BossWard_{0}.log" -f $runId)
$renderFlags = if ($Capture) {
    '-RenderOffscreen -ResX=1280 -ResY=720 -windowed -AuraBossWardCapture'
} else { '-nullrhi' }
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nosound {1} -AuraBossWardSmoke -AuraNoPersistentSave -TestExit=AURA_BOSS_WARD -abslog="{2}"' -f
    $project, $renderFlags, $logPath
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $process.WaitForExit(65000)) { throw "Boss ward smoke timed out: $logPath" }
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
if (-not (Select-String -LiteralPath $logPath -Pattern 'AURA_BOSS_WARD Passed=true' -SimpleMatch -Quiet)) {
    $diagnostic = Select-String -LiteralPath $logPath -Pattern 'AURA_BOSS_WARD|Boss ward smoke intermediate' |
        Select-Object -Last 3 | ForEach-Object { $_.Line }
    throw "Boss ward smoke failed: $logPath`n$diagnostic"
}
$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
if ($Capture) {
    $imagePath = Join-Path $root 'Saved\Screenshots\WindowsEditor\AuraBossWard.png'
    if (-not (Test-Path -LiteralPath $imagePath)) {
        throw "Boss ward screenshot missing: $imagePath"
    }
    Write-Output "Boss ward screenshot: $imagePath"
}
Write-Output "Boss ward smoke passed: $logPath"
