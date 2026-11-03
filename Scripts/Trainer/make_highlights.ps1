# (Re)makes the night's highlight clips of a trainer run and rewrites its report.html (docs/tools/night-trainer.md).
# Each highlight round is re-run deterministically (its seed, Rounds=1 Record=1) rendered offscreen, the frames are encoded
# by Scripts\Tools\clip_assemble.py and cut to a phone-friendly mp4 (960 px, crf ~30, <= 25 MB) in Saved\Trainer\<RunId>\highlights\.
#   powershell -ExecutionPolicy Bypass -File Scripts\Trainer\make_highlights.ps1 -RunId pmc-main [-Force]
# Needs ffmpeg on PATH. Holds nothing else: run it while no build is running (the game loads Binaries\).
param(
    [Parameter(Mandatory = $true)][string]$RunId,
    [switch]$Force
)
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$PyArgs = @((Join-Path $PSScriptRoot "night_trainer.py"), "--run-id", $RunId, "--make-highlights")
if ($Force) { $PyArgs += "--force" }
Push-Location $Repo
try { & python @PyArgs; $Code = $LASTEXITCODE } finally { Pop-Location }
exit $Code
