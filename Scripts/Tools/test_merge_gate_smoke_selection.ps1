$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repo 'Scripts\verify_lib.ps1')
. (Join-Path $repo 'Scripts\Tools\merge_gate.ps1')

$map = Get-TestMap
$reg = @($map.smokeRegistry)

# 1. codex/103-low-cover-click repro: must map active smokes
$p103 = Resolve-Impact @(
    'Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp',
    'Source/CodexTacticsDev/Private/Debug/DepotVaultSmokeCommand.cpp',
    'docs/port/codex/outbox/103-low-cover-click-vaults.report.md'
) $map
if ($p103.Smokes.Count -eq 0) { throw '103 repro mapped 0 smokes!' }
Assert-GateWorkerSmokePlan @('Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp') $p103 $map
Write-Host ("PASS 103 repro smokes ({0}): {1}" -f $p103.Smokes.Count, ($p103.Smokes -join ', '))

# 2. grok/G014-pmc-grenade repro: must map active smokes
$pG014 = Resolve-Impact @(
    'Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp',
    'Source/CodexTactics/Public/AI/PmcCommanderRules.h',
    'Source/CodexTactics/Private/AI/PmcCommanderRules.cpp'
) $map
if ($pG014.Smokes.Count -eq 0) { throw 'G014 repro mapped 0 smokes!' }
Assert-GateWorkerSmokePlan @('Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp') $pG014 $map
Write-Host ("PASS G014 repro smokes ({0}): {1}" -f $pG014.Smokes.Count, ($pG014.Smokes -join ', '))

# 3. Guard throws when Source/ changed and 0 smokes mapped
$emptyPlan = [pscustomobject]@{ Smokes = @() }
$threw = $false
try { Assert-GateWorkerSmokePlan @('Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp') $emptyPlan $map } catch { $threw = $true }
if (-not $threw) { throw 'Assert-GateWorkerSmokePlan did not throw on Source/ with 0 smokes!' }
Assert-GateWorkerSmokePlan @('Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp') $emptyPlan $map -AllowNoWorkerSmokes
Assert-GateWorkerSmokePlan @('docs/port/TANDEM.md') $emptyPlan $map
Assert-GateWorkerSmokePlan @('Source/CodexTacticsTests/Private/AI/PmcPolicyRulesTest.cpp') $emptyPlan $map
Write-Host 'PASS Source/ empty-smoke guard'

# 4. Guard throws on archived / non-registry smoke
$badPlan = [pscustomobject]@{ Smokes = @('DepotVaultSmoke') }
$threwArchived = $false
try { Assert-GateWorkerSmokePlan @('Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp') $badPlan $map } catch { $threwArchived = $true }
if (-not $threwArchived) { throw 'Assert-GateWorkerSmokePlan did not throw on archived smoke!' }
Write-Host 'PASS archived-smoke guard'

# 5. worker_daily_smokes.json: all smokes active and cover all 5 kept maps
$daily = Get-Content (Join-Path $repo 'Scripts\Tools\worker_daily_smokes.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($daily.smokes.Count -eq 0) { throw 'worker_daily_smokes.json is empty!' }
$mapsSeen = @{}
foreach ($s in $daily.smokes) {
    if ($reg -notcontains $s) { throw "Daily smoke '$s' is not in smokeRegistry!" }
    $m = $map.smokeOptions.$s.map
    if (-not $m) { $m = '<default>' }
    $mapsSeen[$m] = $true
}
if ($mapsSeen.Count -lt 5) { throw "Daily smokes cover only $($mapsSeen.Count) maps (expected 5 kept maps)!" }
Write-Host ("PASS worker_daily_smokes.json ({0} smokes across {1} kept maps)" -f $daily.smokes.Count, $mapsSeen.Count)

# 6. PowerShell parse check on merge_gate.ps1 and worker_sync.ps1
$errs = $null
[void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'Scripts\Tools\merge_gate.ps1'), [ref]$null, [ref]$errs)
if ($errs.Count) { throw "Parse errors in merge_gate.ps1: $($errs[0].Message)" }
[void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'Scripts\Tools\worker_sync.ps1'), [ref]$null, [ref]$errs)
if ($errs.Count) { throw "Parse errors in worker_sync.ps1: $($errs[0].Message)" }
Write-Host 'PASS PowerShell AST parse checks'
