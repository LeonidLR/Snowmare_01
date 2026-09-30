# Full verification: build, all automation tests, every headless in-game check. Prints one summary line per step.
# The Unreal Editor must be closed (it locks the module DLLs).
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\verify_all.ps1 [-SkipBuild]
param([switch]$SkipBuild)

$ProjectDir = Split-Path $PSScriptRoot -Parent
$Smokes = @(
    "MovementSmoke", "CameraZoneSmoke", "QuestChainSmoke", "CombatFlowSmoke", "WaveCombatSmoke", "ColdSmoke",
    "StanceSmoke", "BarrelSmoke", "RelocationSmoke", "DeployableSmoke", "LootSmoke", "TurretSmoke", "TargetedShotSmoke", "MissionSmoke", "MainMenuSmoke", "DialogueSmoke", "ActionBarSmoke", "BannersSmoke", "TurnBasedSmoke", "LevelWaveSmoke", "ExposedZonesSmoke", "TurnBasedPushSmoke", "TurnBasedBarricadeSmoke", "TurnBasedDeploySmoke", "WeaponSelectorSmoke", "GrenadeSmoke", "GuardSmoke", "InventorySmoke", "TransferSmoke", "SaveLoadSmoke", "PauseMenuSmoke", "RadiusRingSmoke", "FlankBreachSmoke", "SusaninSmoke", "FloatingTextSmoke", "SquadFireSmoke", "RageSmoke", "SquadControlSmoke", "AIGrenadeSmoke", "EnemyAISmoke", "CutterSmoke", "ClickRulesSmoke", "NarrativeSmoke", "HoldSphereSmoke", "ProgressionSmoke", "VictorySmoke", "TurnBasedCameraSmoke", "EventBusSmoke"
)
$Failed = @()

if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    Write-Host "Unreal Editor is running: close it first (it locks the DLLs)." -ForegroundColor Yellow
    exit 2
}

if (-not $SkipBuild) {
    powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; exit 1 }
}

$TestSummary = powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "test.ps1") | Select-Object -Last 1
$TestExit = $LASTEXITCODE
Write-Host "Tests: $TestSummary"
# A non-zero exit also catches an engine crash in the middle of the run (the summary then counts only the tests run).
if ($TestSummary -notmatch " 0 failed" -or $TestExit -ne 0) { $Failed += "tests (exit $TestExit)" }

foreach ($Smoke in $Smokes) {
    # Headless checks skip the start menu; MainMenuSmoke forces it.
    $SmokeArgs = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "smoke.ps1"), "-Command", "CodexTactics.$Smoke")
    if ($Smoke -in @("MainMenuSmoke", "DialogueSmoke")) { $SmokeArgs += @("-Extra", "-ForceMainMenu") }
    $null = powershell @SmokeArgs
    $Status = if ($LASTEXITCODE -eq 0) { "PASS" } else { "FAIL"; $Failed += $Smoke }
    Write-Host ("{0,-18} {1}" -f $Smoke, $Status)
}

if ($Failed.Count -gt 0) {
    Write-Host "FAILED: $($Failed -join ', ')  (details: Saved\Logs\Smoke.log of the last run, Saved\TestReport)" -ForegroundColor Red
    exit 1
}
Write-Host "ALL GREEN" -ForegroundColor Green
exit 0
