# Records the playtest bot's run through a (generated) map as phone-sized videos (level generator pipeline, 2026-11-02;
# docs/design/levelgen/LEVEL_GENERATOR.md section 8). The game runs RENDERED offscreen (never a desktop capture): the bot's route mode
# (-CodexBot -BotRoute=1: the squad walks the level JSON's "bot_route" and fights the pressure enemies) and CodexTactics.ClipRecorder (every
# frame to a PNG, the director camera on the action); Scripts/Tools/clip_assemble.py encodes the frames block by block while the game runs
# (the PNGs deleted at once) and then cuts the videos.
#   powershell -ExecutionPolicy Bypass -File Scripts\levelgen_record_run.ps1 -Map /Game/Maps/Generated/L_Gen_Depot_1 -LevelJson L_Gen_Depot_1.json
#     -Name lgdepot [-Seconds 600] [-SpeedUp 4] [-Highlights 3] [-Seed 1] [-OutDir Saved\Screenshots\LevelGenRun] [-StatusFile <json>]
#     [-LockWaitMinutes 60] [-Keep]
# Output in -OutDir: <Name>_full_x<SpeedUp>.mp4 (the whole run sped up), <Name>_fight_<n>.mp4 (real-speed fight clips <= 20 s),
# clips.json (codex.levelgen_clips/1: the files with duration / size / brightness check, the logged events with their video time),
# <Name>_run.json (the bot's run record, codex.combatlab_run/1), <Name>.log (the game log).
# Status (Scripts/job_status.ps1, codex.job_status/1; default <OutDir>\<Name>.status.json): running / waiting_lock / done / failed / refused,
# result { map, level_json, name, seconds, speedup, frames_dir, frames_encoded, segments, manifest, files, video_sec, run_report, game_log,
# game_exit_code }. Exit codes: 0 done, 1 no video, 2 a clip failed its check, 3 the build lock is held / timeout, 4 the game failed to start.
param(
    [Parameter(Mandatory = $true)][string]$Map,
    [string]$LevelJson = "",
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Seconds = 600,
    [double]$SpeedUp = 4,
    [int]$Highlights = 3,
    [int]$Seed = 1,
    [string]$OutDir = "",
    [string]$StatusFile = "",
    [int]$LockWaitMinutes = 60,
    [int]$ResX = 1280,
    [int]$ResY = 720,
    [switch]$Keep
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
if ($Name -notmatch '^[A-Za-z0-9_-]{1,40}$') { Write-Host "-Name: letters, digits, _ and - only" -ForegroundColor Red; exit 2 }
if (-not $OutDir) { $OutDir = Join-Path $ProjectDir "Saved\Screenshots\LevelGenRun" }
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force $OutDir | Out-Null
if (-not $StatusFile) { $StatusFile = Join-Path $OutDir "$Name.status.json" }
$FramesDir = Join-Path $ProjectDir "Saved\Screenshots\Clips\$Name"
$SegDir = Join-Path $OutDir "_segments_$Name"
$GameLog = Join-Path $OutDir "$Name.log"
$RunReport = Join-Path $OutDir "${Name}_run.json"
$Assembler = Join-Path $PSScriptRoot "Tools\clip_assemble.py"

. (Join-Path $PSScriptRoot "agent_lock.ps1")
. (Join-Path $PSScriptRoot "job_status.ps1")
$Job = New-JobStatus $StatusFile "record_run" $GameLog
Set-JobStatus $Job @{ map = $Map; level_json = $LevelJson; name = $Name; seconds = $Seconds; speedup = $SpeedUp; frames_dir = $FramesDir
    run_report = $RunReport; game_log = $GameLog; out_dir = $OutDir }

# Old frames / segments of the same name would be mixed into this run.
foreach ($Dir in @($FramesDir, $SegDir)) { if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir } }
Remove-Item -Force -ErrorAction SilentlyContinue $RunReport, (Join-Path $OutDir "clips.json")

# The game loads the module DLLs: no build may run meanwhile (the shared agent lock), as for the bot batches.
$Owned = Wait-JobLock $Job "record run $Name" $LockWaitMinutes
$ExitCode = 0
try {
    $Level = if ($LevelJson) { " `"-LevelJson=$LevelJson`"" } else { "" }
    $GameArgs = "`"$Project`" $Map -game -RenderOffscreen -ResX=$ResX -ResY=$ResY -nosound -nosplash -unattended -NoTelemetry -Multiprocess -benchmark -fps=30" +
        "$Level -CodexBot -BotRoute=1 -BotDuration=$Seconds -BotTimeout=$([Math]::Max(7200, $Seconds * 20)) -BotSeed=$Seed -PressureSeed=$Seed" +
        " `"-CombatLabReport=$RunReport`" `"-abslog=$GameLog`" `"-ExecCmds=CodexTactics.ClipRecorder $Name seconds=$($Seconds + 3)`""
    Set-JobStatus $Job @{} "running" "recording: the game renders $Seconds s of the bot's run"
    Write-Host "record: $Map ($Seconds s game time) -> $FramesDir"
    $Game = Start-Process -FilePath $Editor -ArgumentList $GameArgs -PassThru -WindowStyle Hidden
    if (-not $Game) { Complete-JobStatus $Job "failed" 4 "the game did not start" "game_start"; exit 4 }
    $null = $Game.Handle
    Set-JobStatus $Job @{ game_pid = $Game.Id }
    # Encodes the frames block by block while the game records; returns when the game has exited and the last frames are encoded.
    & python $Assembler watch --frames $FramesDir --segments $SegDir --pid $Game.Id --fps 30 --seg-frames 900 --status $StatusFile
    $Game.WaitForExit()
    Set-JobStatus $Job @{ game_exit_code = $Game.ExitCode }
} finally {
    if ($Owned) { Exit-AgentLock }
}

$AsmArgs = @("assemble", "--segments", $SegDir, "--log", $GameLog, "--name", $Name, "--out", $OutDir, "--fps", "30", "--speedup", "$SpeedUp",
    "--highlights", "$Highlights", "--report", $RunReport, "--status", $StatusFile)
if ($Keep) { $AsmArgs += "--keep" }
& python $Assembler @AsmArgs
$ExitCode = $LASTEXITCODE
if (-not $Keep) {
    if (Test-Path $FramesDir) { Remove-Item -Recurse -Force $FramesDir }
    if (Test-Path $SegDir) { Remove-Item -Recurse -Force $SegDir -ErrorAction SilentlyContinue }
}
switch ($ExitCode) {
    0 { Complete-JobStatus $Job "done" 0 "videos ready: $(Join-Path $OutDir 'clips.json')" }
    1 { Complete-JobStatus $Job "failed" 1 "no video was recorded (see $GameLog)" "no_video" }
    default { Complete-JobStatus $Job "failed" $ExitCode "a video failed its check (see clips.json)" "clip_check" }
}
Write-Host ("record: exit {0}, manifest {1}" -f $ExitCode, (Join-Path $OutDir "clips.json"))
exit $ExitCode
