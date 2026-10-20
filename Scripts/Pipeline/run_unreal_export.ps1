# Character single-slot pipeline, stages 1-2: headless, read-only Unreal inventory + FBX / texture export.
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/run_unreal_export.ps1 `
#       -Mesh /Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a `
#       -Out ArtPipeline/Characters/Operatives/Post_Apo_Survivor_a [-AnimRegex "Crouch|Prone"] [-AnimRoot /Game/Characters]
# Reports (does not kill anything) if the commandlet cannot run, e.g. because the editor holds the project.
param(
    [Parameter(Mandatory = $true)][string]$Mesh,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$AnimRegex = "",
    [string]$AnimRoot = "/Game",
    [int]$AnimMax = 12,
    [switch]$SkipMesh,
    [switch]$SkipTextures,
    # The skeletal mesh FBX exporter builds a render proxy (asserts under -nullrhi): render offscreen by default.
    [switch]$NullRhi,
    [string]$Engine = "C:\Program Files\Epic Games\UE_5.8"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$project = Join-Path $root "CodexTactics.uproject"
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
New-Item -ItemType Directory -Force (Join-Path $Out "01_Source") | Out-Null
$script = Join-Path $PSScriptRoot "unreal_export_inventory.py"
$pyArgs = @("--mesh", $Mesh, "--out", $Out, "--anim-root", $AnimRoot, "--anim-max", "$AnimMax")
if ($AnimRegex) { $pyArgs += @("--anim-regex", $AnimRegex) }
if ($SkipMesh) { $pyArgs += "--skip-mesh" }
if ($SkipTextures) { $pyArgs += "--skip-textures" }
$rhi = if ($NullRhi) { "-nullrhi" } else { "-AllowCommandletRendering -RenderOffscreen" }
$env:CODEX_PIPELINE_ARGS = ConvertTo-Json -Compress $pyArgs
$log = Join-Path $Out "01_Source\unreal_export.log"
$cmd = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$argLine = "`"$project`" -run=pythonscript -script=`"$script`" -unattended $rhi -nosplash -nop4 -stdout -FullStdOutLogOutput"
$p = Start-Process -FilePath $cmd -ArgumentList $argLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log
$done = Select-String -Path $log -Pattern "PIPELINE_INVENTORY_DONE" | Select-Object -First 1
if ($done) { Write-Host $done.Line } else { Write-Host "Inventory did not finish (exit $($p.ExitCode)) - see $log"; exit 1 }
