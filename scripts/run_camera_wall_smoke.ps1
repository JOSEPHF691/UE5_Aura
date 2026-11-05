# Keep camera-facing dungeon walls visible while fading, then restore their material.
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
    @{ Name = 'Wall'; Flag = 'AuraCameraWallSmoke'; Marker = 'AURA_CAMERA_WALL Passed=true'; Exit = 'AURA_CAMERA_WALL' },
    @{ Name = 'Gate'; Flag = 'AuraGateFadeSmoke'; Marker = 'Gate fade smoke test completed. Passed=true'; Exit = 'Gate fade smoke test completed.' }
)) {
    $logPath = Join-Path $root ("Saved\Logs\Camera{0}_{1}.log" -f $case.Name, $runId)
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -{1} -AuraNoPersistentSave -TestExit="{2}" -abslog="{3}"' -f
        $project, $case.Flag, $case.Exit, $logPath
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $process.WaitForExit(50000)) { throw "Camera $($case.Name) timed out: $logPath" }
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern $case.Marker -SimpleMatch -Quiet)) {
        $result = Select-String -LiteralPath $logPath -Pattern 'AURA_CAMERA_WALL|Gate fade smoke test completed' -SimpleMatch | Select-Object -Last 1
        throw "Camera $($case.Name) failed: $logPath`n$result"
    }
    Write-Output "Camera $($case.Name) passed: $logPath"
}

$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
