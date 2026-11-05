# Dedicated server and two clients verify elite stats plus both skill warnings.
$ErrorActionPreference = 'Stop'
$editor = 'D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Unreal Projects\Aura\Aura.uproject'
$root = 'D:\Unreal Projects\Aura'
$profile = Join-Path $root 'Saved\SaveGames\AuraProfile.sav'
$originalHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
$runId = [guid]::NewGuid().ToString('N').Substring(0, 12)
$serverLog = Join-Path $root "Saved\Logs\EliteServer_$runId.log"
$clientOneLog = Join-Path $root "Saved\Logs\EliteClient1_$runId.log"
$clientTwoLog = Join-Path $root "Saved\Logs\EliteClient2_$runId.log"
$port = 7803
if (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue) {
    throw "Elite smoke port $port is already in use"
}
$launched = @()
try {
    $serverArgs = '"{0}" /Game/Map/DungeonDemo -server -port={1} -unattended -nosplash -nullrhi -nosound -AuraNoPersistentSave -AuraEliteReplicationServer -TestExit=AURA_ELITE_REPLICATION_SERVER -abslog="{2}"' -f $project,$port,$serverLog
    $server = Start-Process -FilePath $editor -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    $launched += $server
    Start-Sleep -Seconds 3
    $clientOneArgs = '"{0}" 127.0.0.1:{1} -game -unattended -nosplash -nullrhi -nosound -AuraNoPersistentSave -AuraEliteReplicationObserve -AuraEliteSkillReplicationObserve -abslog="{2}"' -f $project,$port,$clientOneLog
    $clientOne = Start-Process -FilePath $editor -ArgumentList $clientOneArgs -PassThru -WindowStyle Hidden
    $launched += $clientOne
    Start-Sleep -Seconds 1
    $clientTwoArgs = '"{0}" 127.0.0.1:{1} -game -unattended -nosplash -nullrhi -nosound -AuraNoPersistentSave -AuraEliteReplicationObserve -AuraEliteSkillReplicationObserve -abslog="{2}"' -f $project,$port,$clientTwoLog
    $clientTwo = Start-Process -FilePath $editor -ArgumentList $clientTwoArgs -PassThru -WindowStyle Hidden
    $launched += $clientTwo
    if (-not $server.WaitForExit(50000)) {
        throw "Elite server smoke timed out: $serverLog"
    }
    Start-Sleep -Seconds 1
}
finally {
    foreach ($launchedProcess in $launched) {
        $launchedProcess.Refresh()
        if (-not $launchedProcess.HasExited) {
            Stop-Process -Id $launchedProcess.Id
        }
    }
}
$serverPass = Select-String -LiteralPath $serverLog -Pattern 'AURA_ELITE_REPLICATION_SERVER Passed=true Players=2' -SimpleMatch -Quiet
$clientOnePass = Select-String -LiteralPath $clientOneLog -Pattern 'AURA_ELITE_REPLICATION_CLIENT Passed=true' -SimpleMatch -Quiet
$clientTwoPass = Select-String -LiteralPath $clientTwoLog -Pattern 'AURA_ELITE_REPLICATION_CLIENT Passed=true' -SimpleMatch -Quiet
$clientOneSkillsPass = Select-String -LiteralPath $clientOneLog -Pattern 'AURA_ELITE_SKILL_REPLICATION_CLIENT Passed=true' -SimpleMatch -Quiet
$clientTwoSkillsPass = Select-String -LiteralPath $clientTwoLog -Pattern 'AURA_ELITE_SKILL_REPLICATION_CLIENT Passed=true' -SimpleMatch -Quiet
$afterHash = if (Test-Path -LiteralPath $profile) {
    (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash
} else { $null }
if ($afterHash -ne $originalHash) { throw 'Original AuraProfile.sav changed unexpectedly.' }
if (-not ($serverPass -and $clientOnePass -and $clientTwoPass -and $clientOneSkillsPass -and $clientTwoSkillsPass)) {
    throw "Elite replication failed: $serverLog, $clientOneLog, $clientTwoLog"
}
Write-Output "Elite replication passed: $serverLog, $clientOneLog, $clientTwoLog"
