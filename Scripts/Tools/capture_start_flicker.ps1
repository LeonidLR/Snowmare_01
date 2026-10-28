# Start-flicker capture (docs/systems/environment-weather.md section 19): records every frame of the first seconds of each level start
# (CodexTactics.StartFlickerShots, rendered offscreen, the player's start: no -ExecCmds) and draws the luminance strips.
#   $env:CODEX_AGENT = "claude"; powershell -ExecutionPolicy Bypass -File Scripts/Tools/capture_start_flicker.ps1 -Tag before
#   ... -Tag after -Compare before        (also draws compare_before_after.png)
#   ... -WallClock                         (no fixed step: the real frame times, closer to PIE's first long frames)
# Output: Saved/Screenshots/Environment/flicker/<tag>/<map>.csv, <map>.log, <map>/<map>_fNNN.png; strip_<tag>.png.
# One game at a time, under the shared build lock (Scripts/agent_lock.ps1).
param(
    [string]$Tag = "start",
    [int]$Frames = 240,
    [string]$Compare = "",
    [switch]$WallClock,
    [string[]]$Maps = @(
        "/Game/Maps/L_Demo01_A_FrozenLake|-LevelJson=L_Demo01_A_FrozenLake.json",
        "/Game/Maps/L_Demo01_B_FrozenFactory|",
        "/Game/Maps/L_MovementTest|",
        "/Game/Environment/Weather/Test/L_WeatherRainTest|"
    ),
    [int]$TimeoutSeconds = 900,
    [string]$ExtraArgs = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $Root "Scripts\agent_lock.ps1")
$Project = Join-Path $Root "CodexTactics.uproject"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$FlickerDir = Join-Path $Root "Saved\Screenshots\Environment\flicker"
$TagDir = Join-Path $FlickerDir $Tag
New-Item -ItemType Directory -Force $TagDir | Out-Null

$Owned = Enter-AgentLock "start flicker capture $Tag"
try {
    foreach ($Entry in $Maps) {
        $Parts = $Entry.Split("|")
        $Map = $Parts[0]
        $Extra = $(if ($Parts.Count -gt 1) { $Parts[1] } else { "" })
        $Name = Split-Path $Map -Leaf
        $Log = Join-Path $TagDir "$Name.log"
        $Clock = $(if ($WallClock) { "" } else { "-UseFixedTimeStep -FPS=60" })
        $GameArgs = "`"$Project`" $Map -game -RenderOffscreen -ResX=1280 -ResY=720 -nosplash -nosound -unattended -windowed -FORCELOGFLUSH -NoTelemetry $Clock -StartFlickerShots=$Tag -StartFlickerFrames=$Frames `"-LogCmds=LogCodexEnvTrace Verbose`" `"-abslog=$Log`" $Extra $ExtraArgs"
        Write-Host "[flicker] $Name ($Tag) ..."
        $Proc = Start-Process "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $GameArgs -WindowStyle Hidden -PassThru
        if (-not $Proc.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $Proc.Id -Force
            Write-Host "[flicker] $Name timed out" -ForegroundColor Red
            continue
        }
        $Done = Select-String -Path $Log -Pattern "\[StartFlicker\] done" | Select-Object -Last 1
        Write-Host "[flicker] $Name exit $($Proc.ExitCode): $(if ($Done) { $Done.Line.Substring([Math]::Max(0, $Done.Line.IndexOf('[StartFlicker]'))) } else { 'no summary' })"
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}

$Tags = New-Object System.Collections.Generic.List[string]
if ($Compare) { $Tags.Add($Compare) }
$Tags.Add($Tag)
python (Join-Path $Root "Scripts\Tools\flicker_strip.py") $FlickerDir $Tags.ToArray()
