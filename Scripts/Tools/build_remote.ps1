# Second compile slot (2026-11-17, docs/port/SECOND_MACHINE.md "Remote build"): compiles THIS checkout on codex-worker instead of
# the main PC's single UBT queue, then copies the changed DLLs back here. Called by Scripts\build.ps1 -Remote worker.
#   1. ships the checkout's HEAD (unpushed commits as a git bundle), its uncommitted tracked changes (git diff HEAD --binary) and
#      its untracked files under Source / Config (zip) - the worker's tree then equals this one;
#   2. runs Scripts\build.ps1 on the worker (UBT there: VS Build Tools 2026, MSVC 14.51 like the main PC) under the worker's lock;
#   3. copies the changed Binaries\Win64 files back (no *.pdb unless -WithPdb), Build.log to Saved\Logs\Build.log here;
#   4. marks the worker's tree (Saved\Worker\tree.json): smoke_parallel -Remote auto then runs the smokes there on this very build.
# Timing: Saved\Logs\RemoteBuilds.jsonl here, Saved\Worker\builds.jsonl on the worker. No console windows (all in-process).
# Exit code: 0 built + DLLs here, 1 compile failed (log as usual), 2 worker / sync / copy error (nothing changed here).
param(
    [string]$Target = "CodexTacticsEditor",
    [string]$Config = "Development",
    [switch]$NoUnity,
    [switch]$FullUnity,
    [switch]$WithPdb,
    [string]$Worker = "codex-worker",
    [int]$WaitMinutes = 30,
    [switch]$ForceFrozen                    # lead only: build although the worker is frozen for the night trainer (else exit 2)
)
$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$LogDir = Join-Path $ProjectDir "Saved\Logs"
New-Item -ItemType Directory -Force $LogDir | Out-Null
function Say([string]$m, [string]$c = "Gray") { Write-Host "[build_remote] $m" -ForegroundColor $c }
$t0 = Get-Date
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$tmp = Join-Path ([IO.Path]::GetTempPath()) "build_remote-$PID-$stamp"
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    $head = (& git -C $ProjectDir rev-parse HEAD).Trim()
    # Uncommitted tracked changes (staged + unstaged) as one binary patch.
    $patch = Join-Path $tmp "ship.patch"
    # Only what the compile needs (2026-11-25): an uncommitted LFS .uasset (the designer's ABP) made "git apply" fail on the worker (exit 2).
    & git -C $ProjectDir diff HEAD --binary "--output=$patch" -- Source Config Plugins "*.uproject" "*.Build.cs" "*.Target.cs"   # git writes the bytes itself
    $patchArg = $(if ((Test-Path $patch) -and (Get-Item $patch).Length -gt 0) { $patch } else { "" })
    # Untracked files the build sees (new .cpp / .h, Build.cs, config) - repo-relative paths kept in the zip.
    $untracked = @(& git -C $ProjectDir ls-files --others --exclude-standard -- Source Config)
    $zipArg = ""
    if ($untracked.Count) {
        $zip = Join-Path $tmp "ship-extra.zip"
        Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
        $z = [IO.Compression.ZipFile]::Open($zip, "Create")
        foreach ($u in $untracked) { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, (Join-Path $ProjectDir $u), $u) }
        $z.Dispose()
        $zipArg = $zip
    }
    # Tree identity (same rule as smoke_remote.ps1): HEAD + a hash of the uncommitted part.
    $token = @(& (Join-Path $PSScriptRoot "tree_token.ps1") -ProjectDir $ProjectDir)[-1]
    $dirtyHash = ($token -split '\|')[1]
    Say ("{0} {1} of {2} @ {3} ({4}) -> {5}" -f $Target, $Config, (Split-Path $ProjectDir -Leaf), $head.Substring(0, 8),
        $(if ($dirtyHash -eq "clean") { "clean" } else { "+ uncommitted: $(if ($patchArg) { 'patch' }) $(if ($zipArg) { "$($untracked.Count) new files" })" }), $Worker)

    $buildArgs = "-Target $Target -Config $Config"
    if ($NoUnity) { $buildArgs += " -NoUnity" }
    if ($FullUnity) { $buildArgs += " -FullUnity" }
    $resultDir = Join-Path $LogDir "Worker\build-$stamp-$PID"
    $remoteProg = Join-Path $LogDir "remote-build-progress.json"
    $t0Epoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $writeRemoteProg = {
        param([string]$Stage, [int]$Index)
        try {
            if (-not $Stage) {
                if (Test-Path -LiteralPath $remoteProg) { Remove-Item -LiteralPath $remoteProg -Force -ErrorAction SilentlyContinue }
                return
            }
            $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
            @{ stage = $Stage; stage_index = $Index; stage_count = 3; started = $t0Epoch; updated = $now; worker = $Worker } |
                ConvertTo-Json -Compress | Set-Content -LiteralPath $remoteProg -Encoding UTF8
        } catch { }
    }
    & $writeRemoteProg "sync" 1
    & (Join-Path $PSScriptRoot "worker_sync.ps1") -Worker $Worker -ShipHead -BinariesFrom $ProjectDir -Patch $patchArg -ExtraZip $zipArg `
        -TreeToken $token -Build -BuildArgs $buildArgs -FetchBinaries -WithPdb:$WithPdb -SkipSmoke -ResultDir $resultDir -WaitEngineMinutes $WaitMinutes -ForceFrozen:$ForceFrozen
    $code = $LASTEXITCODE
} finally {
    try { if ($remoteProg -and (Test-Path -LiteralPath $remoteProg)) { Remove-Item -LiteralPath $remoteProg -Force -ErrorAction SilentlyContinue } } catch { }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

# Build.log where a local build leaves it (callers read it), the same summary lines as build.ps1.
$remoteLog = Join-Path $resultDir "Build.log"
if (Test-Path $remoteLog) {
    Copy-Item -Force $remoteLog (Join-Path $LogDir "Build.log")
    $lines = Get-Content $remoteLog
    $issues = $lines | Where-Object { $_ -match "(error|warning)" -and $_ -notmatch "Epic Games\\UE_5\.8\\Engine\\" -and $_ -notmatch "not a preferred version|has not been heavily tested" }
    $issues | Select-Object -First 40 | ForEach-Object { Write-Host $_ }
    $lines | Where-Object { $_ -match "^Result:|Total execution time" } | ForEach-Object { Write-Host $_ }
}
$total = [Math]::Round(((Get-Date) - $t0).TotalSeconds)
$exit = $(switch ($code) { 0 { 0 } 3 { 1 } default { 2 } })
$rec = [ordered]@{ at = (Get-Date -Format s); checkout = (Split-Path $ProjectDir -Leaf); head = $head.Substring(0, 8); dirty = $dirtyHash; target = "$Target $Config";
    exit = $exit; total_s = $total; worker = $Worker }
Add-Content -Encoding UTF8 (Join-Path $LogDir "RemoteBuilds.jsonl") ($rec | ConvertTo-Json -Compress)
Say ("{0} - {1:N1} min door to door (sync + compile + DLLs back){2}" -f $(switch ($exit) { 0 { "BUILT on $Worker, DLLs here" } 1 { "COMPILE FAILED on $Worker" } default { "WORKER ERROR - nothing changed here (exit $code)" } }),
    ($total / 60), $(if ($exit -eq 2) { "; build locally: Scripts\build.ps1" } else { "" })) $(if ($exit -eq 0) { "Green" } else { "Red" })
exit $exit
