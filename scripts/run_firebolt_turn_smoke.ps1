$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$logPath = 'D:\Unreal Projects\Aura\Saved\Logs\FireBoltTurnSmoke.log'
$arguments = '"{0}" /Game/Map/DungeonDemo -game -unattended -nosplash -nullrhi -nosound -AuraFireBoltTurnSmoke -AuraNoPersistentSave -TestExit=AURA_FIREBOLT_TURN -abslog="{1}"' -f $project, $logPath
$runProcess = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (-not $runProcess.WaitForExit(50000)) {
        throw "FireBolt turn smoke timed out: $logPath"
    }
}
finally {
    $runProcess.Refresh()
    if (-not $runProcess.HasExited) { Stop-Process -Id $runProcess.Id }
}
if (-not (Select-String -LiteralPath $logPath -Pattern 'AURA_FIREBOLT_TURN Passed=true' -SimpleMatch -Quiet)) {
    throw "FireBolt turn smoke failed: $logPath"
}
Write-Output "FireBolt turn passed: $logPath"
