# Prints the identity of a checkout's build-relevant tree: "<HEAD sha>|clean" or "<HEAD sha>|<12-hex hash>" of its uncommitted
# tracked changes (git diff HEAD --binary) + its untracked files under Source / Config. Shared by Tools\build_remote.ps1 (marks
# the worker's tree after a remote build) and Tools\smoke_remote.ps1 (runs smokes on that build when the tokens match).
#   powershell -File Scripts\Tools\tree_token.ps1 [-ProjectDir <checkout>]
param([string]$ProjectDir = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference = "Stop"
$head = (& git -C $ProjectDir rev-parse HEAD).Trim()
$patch = Join-Path ([IO.Path]::GetTempPath()) "tree_token-$PID.patch"
& git -C $ProjectDir diff HEAD --binary "--output=$patch"
# .NET SHA1 instead of Get-FileHash: under pwsh 7 a child powershell.exe can inherit a PSModulePath that hides
# Microsoft.PowerShell.Utility ("Get-FileHash not found", Codex 134 2026-11-26).
$sha = [Security.Cryptography.SHA1]::Create()
function Get-Sha1Hex([string]$Path) { ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))) -replace '-', '') }
$parts = @()
if ((Test-Path $patch) -and (Get-Item $patch).Length -gt 0) { $parts += (Get-Sha1Hex $patch) }
Remove-Item $patch -ErrorAction SilentlyContinue
foreach ($u in @(& git -C $ProjectDir ls-files --others --exclude-standard -- Source Config)) {
    $parts += "$u=" + (Get-Sha1Hex (Join-Path $ProjectDir $u))
}
if (-not $parts.Count) { "$head|clean"; exit 0 }
"$head|" + ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($parts -join ";"))) -replace '-', '').Substring(0, 12)
