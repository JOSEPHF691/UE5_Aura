param([switch]$Cancel, [switch]$Capture)

$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$root = 'D:\Unreal Projects\Aura'
$project = Join-Path $root 'Aura.uproject'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$beforeHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$logPath = Join-Path $root ("Saved\Logs\BossBarrage_{0}.log" -f $runId)
$smokeFlag = if ($Cancel) { '-AuraBossBarrageCancelSmoke' } else { '-AuraBossBarrageSmoke' }
$renderFlags = if ($Capture) {
    '-RenderOffscreen -ResX=1280 -ResY=720 -windowed -AuraBossBarrageCapture'
} else { '-nullrhi' }
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nosound {1} {2} -AuraNoPersistentSave -TestExit="Boss barrage smoke test completed" -abslog="{3}"' -f $project, $smokeFlag, $renderFlags, $logPath
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $process.WaitForExit(65000)) { throw "Boss barrage smoke timed out: $logPath" }
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
$result = Select-String -LiteralPath $logPath -Pattern 'Boss barrage smoke test completed. Passed=' -SimpleMatch | Select-Object -Last 1
if (-not $result -or $result.Line -notmatch 'Passed=true') {
    $diagnostic = Select-String -LiteralPath $logPath -Pattern 'Boss barrage|Boss action started|Fatal error' |
        Select-Object -Last 16 | ForEach-Object { $_.Line }
    throw "Boss barrage smoke failed: $logPath`n$diagnostic"
}
$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
if ($Capture) {
    $expectedScreenshots = if ($Cancel) {
        @('AuraBossBarrageWarning.png')
    } else {
        @('AuraBossBarrageWarning.png', 'AuraBossBarrageFiring.png')
    }
    foreach ($name in $expectedScreenshots) {
        $screenshot = Join-Path $root ("Saved\Screenshots\WindowsEditor\{0}" -f $name)
        if (-not (Test-Path -LiteralPath $screenshot)) { throw "Missing screenshot: $screenshot" }
        Write-Output "Barrage screenshot: $screenshot"
    }
}
Write-Output $result.Line
Write-Output "Boss barrage smoke passed: $logPath"
