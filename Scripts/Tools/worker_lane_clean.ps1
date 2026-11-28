# Runs ON the worker (codex-worker); called by cleanup_merged_lanes.ps1 over SSH (designer 2026-11-28: no leftover copies on the worker).
#   -List           prints "<path>|<HEAD sha>|<busy 0/1>" for every worktree of C:\Work\CodexTactics under C:\Work\Lanes or a slot's Saved\wt-*
#   -Remove a,b     removes those worktrees: every junction / symlink inside is unlinked first (the link only), main Content file count
#                   checked before and after, then `git worktree remove`. Never touches C:\Work\CodexTactics or the slot roots themselves.
param([switch]$List, [string]$Remove = '')
$ErrorActionPreference = 'Stop'
$Main = 'C:\Work\CodexTactics'
function Get-Trees {
    $trees = @(); $cur = $null
    foreach ($line in @(& git -C $Main worktree list --porcelain)) {
        if ($line -like 'worktree *') { $cur = @{ Path = ($line.Substring(9) -replace '/', '\'); Head = '' }; $trees += $cur }
        elseif ($line -like 'HEAD *' -and $cur) { $cur.Head = $line.Substring(5) }
    }
    @($trees | Where-Object { $_.Path -like 'C:\Work\Lanes\*' -or $_.Path -like 'C:\Work\CodexTactics-slot*\Saved\wt-*' })
}
function Test-Busy([string]$Path) {
    [bool](Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\') -like "*$Path*" -and
        $_.Name -in 'UnrealEditor.exe','UnrealEditor-Cmd.exe','dotnet.exe','cl.exe','powershell.exe','python.exe' })
}
if ($List) { foreach ($t in Get-Trees) { '{0}|{1}|{2}' -f $t.Path, $t.Head, [int](Test-Busy $t.Path) }; exit 0 }
$wanted = @($Remove -split ',' | Where-Object { $_ } | ForEach-Object { $_ -replace '/', '\' })
$known = @(Get-Trees | ForEach-Object Path)
$count = { (Get-ChildItem "$Main\Content" -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count }
$before = & $count
foreach ($tree in $wanted) {
    if ($tree -notin $known) { Write-Host "[worker-lanes] skip $tree (not a lane worktree)"; continue }
    if (Test-Busy $tree) { Write-Host "[worker-lanes] skip $tree (busy)"; continue }
    $links = @(Get-ChildItem $tree -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue | Sort-Object { $_.FullName.Length })
    foreach ($l in $links) {
        $now = Get-Item -LiteralPath $l.FullName -Force -ErrorAction SilentlyContinue
        if ($now -and ($now.Attributes -band [IO.FileAttributes]::ReparsePoint)) { [IO.Directory]::Delete($now.FullName) }
    }
    & git -C $Main worktree remove --force $tree
    Write-Host "[worker-lanes] removed $tree ($($links.Count) links unlinked)"
}
& git -C $Main worktree prune
$after = & $count
if ($after -ne $before) { throw "worker main Content changed ($before -> $after files)" }
Write-Host ('[worker-lanes] main Content intact ({0} files); C: free {1:N0} GB' -f $after, ((Get-PSDrive C).Free / 1GB))
