# Removes an agents' worktree made by new_agent_worktree.ps1 SAFELY: every junction / reparse point inside it is removed
# first (the link only, never its target), the main folder's Content is counted before and after (the run stops if it
# changed), then `git worktree remove`. The branch is kept. Refuses when the worktree has uncommitted tracked changes
# unless -Force.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts/Tools/remove_agent_worktree.ps1 -Name agents [-Force]
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [switch]$Force
)
$ErrorActionPreference = "Stop"
$Main = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tree = Join-Path (Split-Path $Main -Parent) "CodexTactics-$Name"
if (-not (Test-Path $Tree)) { throw "$Tree does not exist" }
if ((Resolve-Path $Tree).Path -eq (Resolve-Path $Main).Path) { throw "refusing to remove the main folder" }

$Dirty = @(& git -C $Tree status --porcelain | Where-Object { $_ -notmatch '^\?\?' })
if ($Dirty.Count -and -not $Force) { throw "$Tree has $($Dirty.Count) uncommitted tracked change(s); commit them or pass -Force" }

function Count-Main { (Get-ChildItem (Join-Path $Main "Content") -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object).Count }
$Before = Count-Main

for ($Pass = 0; $Pass -lt 5; $Pass++) {
    $LinkItems = @(Get-ChildItem $Tree -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue | Sort-Object { $_.FullName.Length })
    if ($LinkItems.Count -eq 0) { break }
    foreach ($Item in $LinkItems) {
        if (-not (Test-Path -LiteralPath $Item.FullName)) { continue }
        $Now = Get-Item -LiteralPath $Item.FullName -Force
        if (-not ($Now.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
        if ($Now.PSIsContainer) { [IO.Directory]::Delete($Now.FullName, $false) } else { [IO.File]::Delete($Now.FullName) }
    }
}
$Left = @(Get-ChildItem $Tree -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue).Count
if ($Left) { throw "$Left reparse point(s) left in $Tree - stopping before any delete" }
$After = Count-Main
if ($After -ne $Before) { throw "main Content changed while unlinking ($Before -> $After files) - stopping" }

& git -C $Main worktree remove --force $Tree
if (Test-Path $Tree) { cmd /c rmdir /s /q "`"$Tree`"" }
& git -C $Main worktree prune
$Final = Count-Main
if ($Final -ne $Before) { throw "main Content changed ($Before -> $Final files)" }
Write-Host "Removed $Tree; main Content intact ($Final files)."
