# Prints the identity of a checkout's build-relevant tree: "<HEAD sha>|clean" or "<HEAD sha>|<12-hex hash>" of its uncommitted
# tracked changes (git diff HEAD --binary) + its untracked files under Source / Config. Shared by Tools\build_remote.ps1 (marks
# the worker's tree after a remote build) and Tools\smoke_remote.ps1 (runs smokes on that build when the tokens match).
#   powershell -File Scripts\Tools\tree_token.ps1 [-ProjectDir <checkout>]
param([string]$ProjectDir = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference = "Stop"
$head = (& git -C $ProjectDir rev-parse HEAD).Trim()
$patch = Join-Path ([IO.Path]::GetTempPath()) "tree_token-$PID.patch"
& git -C $ProjectDir diff HEAD --binary "--output=$patch"
$parts = @()
if ((Test-Path $patch) -and (Get-Item $patch).Length -gt 0) { $parts += (Get-FileHash $patch -Algorithm SHA1).Hash }
Remove-Item $patch -ErrorAction SilentlyContinue
foreach ($u in @(& git -C $ProjectDir ls-files --others --exclude-standard -- Source Config)) {
    $parts += "$u=" + (Get-FileHash (Join-Path $ProjectDir $u) -Algorithm SHA1).Hash
}
if (-not $parts.Count) { "$head|clean"; exit 0 }
$sha = [Security.Cryptography.SHA1]::Create()
"$head|" + ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($parts -join ";"))) -replace '-', '').Substring(0, 12)
