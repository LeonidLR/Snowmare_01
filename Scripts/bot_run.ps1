# Runs the playtest bot (UPlaytestBotSubsystem) N times without rendering, fixed-step and as fast as the CPU allows;
# every run appends its record to Saved\Telemetry\raw_runs\runs.jsonl (the Wave Editor's analytics read it).
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\bot_run.ps1 [-Runs 1] [-Profile NORMAL] [-Loadout COLLECT] [-TimeoutSeconds 600]
# -Profile CASUAL | NORMAL | VETERAN; -Loadout COLLECT (explore first) | UNIQUE | PRESET (straight to the fight).
# Speed of the Godot archive's runner is not needed: the run is fixed-step (-benchmark -FPS=60), not time-scaled.
param(
    [int]$Runs = 1,
    [string]$Profile = "NORMAL",
    [string]$Loadout = "COLLECT",
    [int]$TimeoutSeconds = 600,
    [string]$Map = "/Game/Maps/L_MovementTest"
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$Profile = $Profile.ToUpper()
$Results = @{ VICTORY = 0; DEFEAT = 0; ABORTED = 0; ERROR = 0 }

for ($Run = 1; $Run -le $Runs; $Run++) {
    $Log = Join-Path $ProjectDir "Saved\Logs\Bot-$Profile-$Run.log"
    Write-Host ("[{0} {1}/{2}] simulating..." -f $Profile, $Run, $Runs)
    $Watch = [Diagnostics.Stopwatch]::StartNew()
    $GameArgs = "`"$Project`" $Map -game -nullrhi -nosound -nosplash -unattended -windowed -benchmark -FPS=60 -CodexBot " +
        "-BotProfile=$Profile -BotLoadout=$Loadout -BotTimeout=$TimeoutSeconds `"-abslog=$Log`""
    $Process = Start-Process -FilePath $Editor -ArgumentList $GameArgs -PassThru -WindowStyle Hidden
    if (-not $Process.WaitForExit(($TimeoutSeconds + 120) * 1000)) {
        $Process.Kill()
    }
    $Line = Select-String -Path $Log -Pattern "\[Bot\] RESULT (\w+)" -ErrorAction SilentlyContinue | Select-Object -Last 1
    $Result = if ($Line) { $Line.Matches[0].Groups[1].Value } else { "ERROR" }
    $Results[$Result] = $Results[$Result] + 1
    $Waves = Select-String -Path $Log -Pattern "\[Bot\] Wave (\d+) cleared" -ErrorAction SilentlyContinue | Select-Object -Last 1
    $Cleared = if ($Waves) { $Waves.Matches[0].Groups[1].Value } else { "0" }
    Write-Host ("    {0}, waves cleared {1}, {2:N0} s real (log {3})" -f $Result, $Cleared, $Watch.Elapsed.TotalSeconds, $Log)
}

Write-Host ""
Write-Host ("{0}: {1} runs - victories {2}, defeats {3}, timeouts {4}, errors {5}" -f $Profile, $Runs, $Results.VICTORY,
    $Results.DEFEAT, $Results.ABORTED, $Results.ERROR)
Write-Host ("Telemetry: {0}" -f (Join-Path $ProjectDir "Saved\Telemetry\raw_runs\runs.jsonl"))
