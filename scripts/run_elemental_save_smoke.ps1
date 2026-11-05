$ErrorActionPreference = 'Stop'
$root = 'D:\Unreal Projects\Aura'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $root 'Aura.uproject'
$id = [guid]::NewGuid().ToString('N').Substring(0,10)
$slot = "ElementalTest_$id"
$file = Join-Path $root "Saved\SaveGames\$slot.sav"
$profiles = @('AuraProfile.sav','AuraProfile_Editor.sav')
$before = @{}
foreach ($name in $profiles) {
    $path = Join-Path $root "Saved\SaveGames\$name"
    $before[$name] = if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path).Hash } else { $null }
}
$logs = @()
foreach ($mode in @('Seed','Read','Read','Invalid','Reject','Future','Reject')) {
    $index = $logs.Count
    $log = Join-Path $root "Saved\Logs\ElementalSave_${id}_${index}_$mode.log"
    $logs += $log
    $hashBefore = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { $null }
    $readOnly = if ($mode -eq 'Seed' -or $mode -eq 'Invalid' -or $mode -eq 'Future') { '-AuraNoPersistentSave' } else { '' }
    $args = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraElementalSave -ElementalSaveMode={1} -AuraSaveSlot={2} {3} -TestExit=AURA_ELEMENTAL_SAVE_DONE -abslog="{4}"' -f $project,$mode,$slot,$readOnly,$log
    $owned = Start-Process -FilePath $editor -ArgumentList $args -PassThru -WindowStyle Hidden
    try { if (-not $owned.WaitForExit(45000)) { throw "Save test timeout: $log" } }
    finally { $owned.Refresh(); if (-not $owned.HasExited) { Stop-Process -Id $owned.Id } }
    if (-not (Select-String -LiteralPath $log -Pattern "AURA_ELEMENTAL_SAVE Mode=$mode Passed=true" -SimpleMatch -Quiet)) { throw "Save test failed: $log" }
    if ($mode -eq 'Reject' -and (Get-FileHash -LiteralPath $file).Hash -ne $hashBefore) { throw "Rejected save overwritten: $file" }
    Select-String -LiteralPath $log -Pattern 'AURA_ELEMENTAL_SAVE Mode=|M15 save loaded|Invalid growth|Unsupported schema' | ForEach-Object { $_.Line }
}
foreach ($name in $profiles) {
    $path = Join-Path $root "Saved\SaveGames\$name"
    $after = if (Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path).Hash } else { $null }
    if ($after -ne $before[$name]) { throw "Real profile modified: $name" }
}
Write-Output "Seven isolated save checks passed. Fixture retained: $file"
