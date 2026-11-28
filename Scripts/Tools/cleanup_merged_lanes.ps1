# Removes agent lanes (C:\Work\Lanes\*) whose branch is already merged into main - designer 2026-11-26 (main PC disk full:
# 66 lanes x ~5 GB Binaries + Intermediate). Called by merge_gate.ps1 after a green gate, or by hand:
#   powershell -ExecutionPolicy Bypass -File Scripts/Tools/cleanup_merged_lanes.ps1 [-DryRun] [-IdleMinutes 20] [-MergeDays 30]
# A lane is removed only when ALL hold:
#   - it is a git worktree of this repo on a named branch (not detached);
#   - its branch tip came into main through a merge commit (tip reachable from the merge's 2nd parent, not from its 1st parent)
#     - a fresh lane cut from main with no own commits is never "merged" by this rule;
#   - no uncommitted tracked changes;
#   - no git activity in it for IdleMinutes (its worktree index / HEAD);
#   - no UnrealEditor runs from it.
# Removal = the same safe order as remove_agent_worktree.ps1: every junction / symlink inside is unlinked first (the link only,
# never its target), main Content file count checked before and after, then `git worktree remove`. The branch is kept.
param(
    [switch]$DryRun,
    [int]$IdleMinutes = 20,
    [int]$MergeDays = 30,
    [string]$LanesRoot = 'C:\Work\Lanes'
)
$ErrorActionPreference = 'Stop'

# Worker pass (designer 2026-11-28: no leftover copies on codex-worker either): its lane worktrees whose HEAD is already in
# origin/main and that run nothing are removed with the same safe order (Scripts/Tools/worker_lane_clean.ps1 runs there).
function Invoke-WorkerLanes {
  try {
    & ssh -o ConnectTimeout=6 -o BatchMode=yes codex-worker 'return' 2>$null
    if ($LASTEXITCODE -ne 0) { Write-Host '[worker-lanes] worker unreachable - skipped'; return }
    $script = Join-Path $PSScriptRoot 'worker_lane_clean.ps1'
    & scp -q $script 'codex-worker:C:/Users/Public/worker_lane_clean.ps1'
    $rows = @(& ssh codex-worker 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\Public\worker_lane_clean.ps1 -List')
    & git -C $Main fetch -q origin 2>$null
    $gone = @(foreach ($row in $rows) {
        $p = $row -split '\|'
        if ($p.Count -lt 3 -or $p[2] -ne '0' -or -not $p[1]) { continue }
        & git -C $Main merge-base --is-ancestor $p[1] origin/main 2>$null
        if ($LASTEXITCODE -eq 0) { $p[0] }
    })
    if (-not $gone.Count) { Write-Host "[worker-lanes] nothing merged to remove ($($rows.Count) lane(s) on the worker)"; return }
    if ($DryRun) { $gone | ForEach-Object { Write-Host "[worker-lanes] would remove $_" }; return }
    & ssh codex-worker "powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\Public\worker_lane_clean.ps1 -Remove $($gone -join ',')"
  } catch { Write-Host "[worker-lanes] skipped: $($_.Exception.Message)" }
}

$Main = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not (Test-Path $LanesRoot)) { Write-Host "[lanes] $LanesRoot missing - nothing to do"; exit 0 }
$LanesFull = (Resolve-Path $LanesRoot).Path.TrimEnd('\')

# Commits brought into main by its merges of the last MergeDays.
$Merged = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($line in @(& git -C $Main log --merges --since="$MergeDays.days" --format='%H %P' HEAD)) {
    $p = $line -split ' '
    if ($p.Count -lt 3) { continue }
    foreach ($c in @(& git -C $Main rev-list $p[2] "^$($p[1])")) { [void]$Merged.Add($c) }
}

$Editors = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction SilentlyContinue | ForEach-Object { $_.CommandLine })
function Count-Main { (Get-ChildItem (Join-Path $Main 'Content') -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object).Count }

$Plan = @()
foreach ($dir in Get-ChildItem $LanesFull -Directory -Force) {
    $tree = $dir.FullName
    $gitFile = Join-Path $tree '.git'
    if (-not (Test-Path $gitFile -PathType Leaf)) { continue }                 # not a worktree
    $gitDir = ((Get-Content $gitFile -Raw) -replace '^gitdir:\s*', '').Trim()
    if (-not (Test-Path $gitDir)) { continue }
    $branch = (& git -C $tree rev-parse --abbrev-ref HEAD 2>$null)
    if (-not $branch -or $branch -eq 'HEAD') { Write-Host "[lanes] keep $($dir.Name): detached"; continue }
    $tip = (& git -C $tree rev-parse HEAD).Trim()
    if (-not $Merged.Contains($tip)) { continue }                               # not merged (or merged long ago - keep)
    $dirty = @(& git -C $tree status --porcelain --untracked-files=no)
    if ($dirty.Count) { Write-Host "[lanes] keep $($dir.Name): $($dirty.Count) uncommitted tracked change(s)"; continue }
    $last = @('index', 'HEAD') | ForEach-Object { Join-Path $gitDir $_ } | Where-Object { Test-Path $_ } |
        ForEach-Object { (Get-Item $_).LastWriteTime } | Sort-Object -Descending | Select-Object -First 1
    if ($last -and $last -gt (Get-Date).AddMinutes(-$IdleMinutes)) { Write-Host "[lanes] keep $($dir.Name): git activity $([int]((Get-Date) - $last).TotalMinutes) min ago"; continue }
    if ($Editors | Where-Object { $_ -and $_.ToLower().Contains($tree.ToLower()) }) { Write-Host "[lanes] keep $($dir.Name): editor open"; continue }
    $Plan += [pscustomobject]@{ Name = $dir.Name; Tree = $tree; Branch = $branch }
}

if (-not $Plan.Count) { Write-Host '[lanes] no merged idle lanes to remove'; Invoke-WorkerLanes; exit 0 }
Write-Host "[lanes] merged + idle: $($Plan.Count) - $(($Plan | ForEach-Object { $_.Name }) -join ', ')"
if ($DryRun) { Invoke-WorkerLanes; exit 0 }

$Before = Count-Main
$removed = 0
foreach ($lane in $Plan) {
    $tree = $lane.Tree
    if (-not $tree.ToLower().StartsWith($LanesFull.ToLower() + '\')) { throw "refusing $tree - outside $LanesFull" }
    for ($pass = 0; $pass -lt 5; $pass++) {
        $links = @(Get-ChildItem $tree -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue | Sort-Object { $_.FullName.Length })
        if (-not $links.Count) { break }
        foreach ($item in $links) {
            if (-not (Test-Path -LiteralPath $item.FullName)) { continue }
            $now = Get-Item -LiteralPath $item.FullName -Force
            if (-not ($now.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
            if ($now.PSIsContainer) { [IO.Directory]::Delete($now.FullName, $false) } else { [IO.File]::Delete($now.FullName) }
        }
    }
    $left = @(Get-ChildItem $tree -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue).Count
    if ($left) { throw "$left reparse point(s) left in $tree - stopping before any delete" }
    & git -C $Main worktree remove --force $tree
    if (Test-Path $tree) { cmd /c rmdir /s /q "`"$tree`"" }
    $removed++
    Write-Host "[lanes] removed $($lane.Name) ($($lane.Branch))"
    if (($removed % 10) -eq 0) {
        $now = Count-Main
        if ($now -ne $Before) { throw "main Content changed ($Before -> $now files) - stopping" }
    }
}
& git -C $Main worktree prune
$Final = Count-Main
if ($Final -ne $Before) { throw "main Content changed ($Before -> $Final files)" }
$free = [math]::Round((Get-PSDrive ($LanesFull.Substring(0, 1))).Free / 1GB, 1)
Write-Host "[lanes] removed $removed lane(s); main Content intact ($Final files); free on drive: $free GB"
Invoke-WorkerLanes
