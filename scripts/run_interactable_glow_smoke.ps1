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
$cases = if ($Capture) {
    @(
        @{ Name = 'Chest'; Flags = '-AuraInteractableGlowCapture'; Render = $true },
        @{ Name = 'Npc'; Flags = '-AuraInteractableGlowCapture -AuraInteractableGlowCaptureNpc'; Render = $true }
    )
} else {
    @(@{ Name = 'Logic'; Flags = ''; Render = $false })
}

foreach ($case in $cases) {
    $logPath = Join-Path $root ("Saved\Logs\InteractableGlow{0}_{1}.log" -f $case.Name, $runId)
    $renderFlags = if ($case.Render) { '-RenderOffscreen -ResX=1280 -ResY=720 -windowed' } else { '-nullrhi' }
    $arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nosound {1} -AuraInteractableGlowSmoke {2} -AuraNoPersistentSave -TestExit=AURA_INTERACTABLE_GLOW -abslog="{3}"' -f
        $project, $renderFlags, $case.Flags, $logPath
    $runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $runProcess.WaitForExit(60000)) {
            throw "Interactable glow $($case.Name) timed out: $logPath"
        }
    }
    finally {
        $runProcess.Refresh()
        if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
    }
    if (-not (Select-String -LiteralPath $logPath -Pattern 'AURA_INTERACTABLE_GLOW Passed=true' -SimpleMatch -Quiet)) {
        $diagnostic = Select-String -LiteralPath $logPath -Pattern 'AURA_INTERACTABLE_GLOW' -SimpleMatch |
            Select-Object -Last 2 | ForEach-Object { $_.Line }
        throw "Interactable glow $($case.Name) failed: $logPath`n$diagnostic"
    }
    Write-Output "Interactable glow $($case.Name) passed: $logPath"
}

$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $beforeHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
