$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logPath = 'D:\Unreal Projects\Aura\Saved\Logs\FireBoltClickSmoke.log'
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraFireBoltClickSmoke -AuraNoPersistentSave -TestExit=AURA_FIREBOLT_CLICK -abslog="{1}"' -f $project, $logPath
$runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $runProcess.WaitForExit(50000)) {
        throw "FireBolt click smoke timed out: $logPath"
    }
}
finally {
    $runProcess.Refresh()
    if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
}
if (-not (Select-String -LiteralPath $logPath -Pattern 'AURA_FIREBOLT_CLICK Passed=true' -SimpleMatch -Quiet)) {
    throw "FireBolt click smoke failed: $logPath"
}
Write-Output "FireBolt click passed: $logPath"
