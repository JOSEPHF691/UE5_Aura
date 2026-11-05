$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logPath = 'D:\Unreal Projects\Aura\Saved\Logs\M24TargetingPolish.log'
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraTargetingPolishSmoke -AuraNoPersistentSave -TestExit=AURA_M24_TARGETING -abslog="{1}"' -f $project, $logPath
$runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $runProcess.WaitForExit(50000)) {
        throw "Targeting polish smoke timed out: $logPath"
    }
}
finally {
    $runProcess.Refresh()
    if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
}
if (-not (Select-String -LiteralPath $logPath -Pattern 'AURA_M24_TARGETING Passed=true' -SimpleMatch -Quiet)) {
    throw "Targeting polish smoke failed: $logPath"
}
Write-Output "Targeting polish passed: $logPath"
