# Full verification: build, all automation tests, every headless in-game check. Prints one summary line per step.
# The checks run a few at a time (-Parallel, default 3; each writes Saved\Logs\Smoke-<Name>.log); the ones sharing save
# slots run alone afterwards, and a check that fails in the parallel batch is re-run alone once (load can slow it down).
# The Unreal Editor must be closed for the build (it locks the module DLLs); -SkipBuild runs alongside it.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\verify_all.ps1 [-SkipBuild] [-Parallel 3]
param([switch]$SkipBuild, [int]$Parallel = 3)

$ProjectDir = Split-Path $PSScriptRoot -Parent
$Smokes = @(
    "MovementSmoke", "CameraZoneSmoke", "QuestChainSmoke", "CombatFlowSmoke", "WaveCombatSmoke", "ColdSmoke",
    "StanceSmoke", "BarrelSmoke", "RelocationSmoke", "DeployableSmoke", "LootSmoke", "TurretSmoke", "TargetedShotSmoke", "MissionSmoke", "MainMenuSmoke", "DialogueSmoke", "ActionBarSmoke", "BannersSmoke", "TurnBasedSmoke", "LevelWaveSmoke", "ExposedZonesSmoke", "TurnBasedPushSmoke", "TurnBasedBarricadeSmoke", "TurnBasedDeploySmoke", "WeaponSelectorSmoke", "GrenadeSmoke", "GuardSmoke", "InventorySmoke", "TransferSmoke", "SaveLoadSmoke", "PauseMenuSmoke", "RadiusRingSmoke", "FlankBreachSmoke", "SusaninSmoke", "FloatingTextSmoke", "SquadFireSmoke", "RageSmoke", "SquadControlSmoke", "AIGrenadeSmoke", "EnemyAISmoke", "CutterSmoke", "ClickRulesSmoke", "NarrativeSmoke", "HoldSphereSmoke", "ProgressionSmoke", "VictorySmoke", "TurnBasedCameraSmoke", "EventBusSmoke", "SilhouetteSmoke", "AttackModeSmoke", "GroupSelectSmoke", "TurnWalkSmoke", "TurnBlastDeathSmoke", "EnemyHuntSmoke", "VaultSmoke", "BarricadeContactSmoke", "CombatMoveSmoke", "EnemyDeathSmoke", "TurnSelectSmoke", "PanicSmoke", "FacingSmoke", "MarksmanSmoke", "EnemyHitLayerSmoke", "RecruitDeathSmoke", "HitFlashOverlaySmoke", "BotMarksmanSmoke", "BarricadeTurnContactSmoke", "MarksmanAdvanceSmoke", "BotRepairSmoke", "PatrolSmoke", "AmbushSmoke"
)
# These write / read the save slots in Saved\SaveGames: never at the same time as each other.
$Exclusive = @("SaveLoadSmoke", "PauseMenuSmoke")
$Failed = @()

# Only an editor of this project locks our DLLs (another project's editor may stay open); -SkipBuild runs alongside it.
if (-not $SkipBuild -and (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object { $_.CommandLine -like "*CodexTactics.uproject*" })) {
    Write-Host "Unreal Editor is running: close it first (it locks the DLLs)." -ForegroundColor Yellow
    exit 2
}

# The shared build lock for the whole run (build, tests, every smoke); the child scripts inherit it.
. (Join-Path $PSScriptRoot "agent_lock.ps1")
$OwnedLock = Enter-AgentLock "verify_all"
Register-EngineEvent PowerShell.Exiting -Action { if ($OwnedLock) { Exit-AgentLock } } | Out-Null

if (-not $SkipBuild) {
    powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; if ($OwnedLock) { Exit-AgentLock }; exit 1 }
}

$TestSummary = powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "test.ps1") | Select-Object -Last 1
$TestExit = $LASTEXITCODE
Write-Host "Tests: $TestSummary"
# A non-zero exit also catches an engine crash in the middle of the run (the summary then counts only the tests run).
if ($TestSummary -notmatch " 0 failed" -or $TestExit -ne 0) { $Failed += "tests (exit $TestExit)" }

function Get-SmokeArgs([string]$Smoke, [string]$Suffix = "") {
    # Headless checks skip the start menu; MainMenuSmoke / DialogueSmoke force it.
    $SmokeArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "smoke.ps1"),
        "-Command", "CodexTactics.$Smoke", "-Log", "Smoke-$Smoke$Suffix.log")
    if ($Smoke -in @("MainMenuSmoke", "DialogueSmoke")) { $SmokeArgs += @("-Extra", "-ForceMainMenu") }
    return $SmokeArgs
}

function Invoke-SmokeAlone([string]$Smoke) {
    # A re-run keeps the parallel run's log (Smoke-<Name>.log) for comparison.
    $Suffix = $(if ($Smoke -in $Exclusive) { "" } else { "-alone" })
    Remove-Item (Join-Path $ProjectDir "Saved\Logs\Smoke-$Smoke$Suffix.log") -ErrorAction SilentlyContinue
    $SmokeArgs = Get-SmokeArgs $Smoke $Suffix
    $null = powershell @SmokeArgs
    return $LASTEXITCODE -eq 0
}

# Parallel batch.
$Queue = [System.Collections.Generic.Queue[string]]::new()
$Smokes | Where-Object { $_ -notin $Exclusive } | ForEach-Object { $Queue.Enqueue($_) }
$Running = @{}
$Retry = @()
while ($Queue.Count -gt 0 -or $Running.Count -gt 0) {
    while ($Running.Count -lt [Math]::Max(1, $Parallel) -and $Queue.Count -gt 0) {
        $Smoke = $Queue.Dequeue()
        # A stale log must never pass for this run's result.
        Remove-Item (Join-Path $ProjectDir "Saved\Logs\Smoke-$Smoke.log") -ErrorAction SilentlyContinue
        # Start-Process joins the arguments with spaces: quote the ones with spaces (the project path has one).
        $QuotedArgs = Get-SmokeArgs $Smoke | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }
        $Running[$Smoke] = Start-Process powershell -ArgumentList $QuotedArgs -WindowStyle Hidden -PassThru
    }
    Start-Sleep -Milliseconds 500
    foreach ($Smoke in @($Running.Keys)) {
        $Process = $Running[$Smoke]
        if ($Process.HasExited) {
            $Process.WaitForExit()
            $Running.Remove($Smoke)
            # The verdict comes from the check's own log (a Start-Process exit code is not reliable in Windows PowerShell).
            $Log = Join-Path $ProjectDir "Saved\Logs\Smoke-$Smoke.log"
            if ((Test-Path $Log) -and (Select-String -Path $Log -Pattern "Smoke RESULT: PASS" -Quiet)) {
                Write-Host ("{0,-24} PASS" -f $Smoke)
            } else {
                Write-Host ("{0,-24} failed in parallel, re-run alone later" -f $Smoke)
                $Retry += $Smoke
            }
        }
    }
}

# Alone: the save-slot checks and the parallel failures.
foreach ($Smoke in ($Exclusive + $Retry)) {
    $Ok = Invoke-SmokeAlone $Smoke
    if (-not $Ok) { $Failed += $Smoke }
    Write-Host ("{0,-24} {1}" -f $Smoke, $(if ($Ok) { "PASS" } else { "FAIL" }))
}

if ($Failed.Count -gt 0) {
    Write-Host "FAILED: $($Failed -join ', ')  (details: Saved\Logs\Smoke-<Name>.log, Saved\TestReport)" -ForegroundColor Red
    if ($OwnedLock) { Exit-AgentLock }
    exit 1
}
Write-Host "ALL GREEN" -ForegroundColor Green
if ($OwnedLock) { Exit-AgentLock }
exit 0
