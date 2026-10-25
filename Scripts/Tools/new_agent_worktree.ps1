# Creates an agents' git worktree next to the main folder that can build, test and run smokes on its own (docs/port/TANDEM.md
# "agents' worktree"): a new branch from main, a directory junction for every untracked top-level Content pack and for
# DerivedDataCache (read-only use of the main folder's copies), and a copy of the untracked Config/DefaultScalability.ini.
# Remove it ONLY with Scripts/Tools/remove_agent_worktree.ps1 (junctions first - deleting the folder through a junction
# deletes the main folder's files: the 2026-10-20 incident).
# Usage: powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name agents -Branch agents/<task>
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Branch,
    [string]$From = "main"
)
$ErrorActionPreference = "Stop"
$Main = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Tree = Join-Path (Split-Path $Main -Parent) "CodexTactics-$Name"
if (Test-Path $Tree) { throw "$Tree already exists" }

& git -C $Main worktree add $Tree -b $Branch $From
if ($LASTEXITCODE -ne 0) { throw "git worktree add failed" }

$Links = 0
# Untracked top-level Content packs (the user's asset packs, never committed).
foreach ($Line in (& git -C $Main status --porcelain Content)) {
    if ($Line -match '^\?\? (Content/[^/]+)/$') {
        $Rel = $Matches[1] -replace '/', '\'
        $Target = Join-Path $Main $Rel
        $Link = Join-Path $Tree $Rel
        if (-not (Test-Path $Link)) {
            New-Item -ItemType Junction -Path $Link -Target $Target | Out-Null
            $Links++
        }
    }
}
$Ddc = Join-Path $Main "DerivedDataCache"
if (Test-Path $Ddc) {
    New-Item -ItemType Junction -Path (Join-Path $Tree "DerivedDataCache") -Target $Ddc | Out-Null
    $Links++
}
$Scalability = Join-Path $Main "Config\DefaultScalability.ini"
if (Test-Path $Scalability) {
    Copy-Item $Scalability (Join-Path $Tree "Config\DefaultScalability.ini")
}
Write-Host "Worktree $Tree on $Branch from $From, $Links junctions."
