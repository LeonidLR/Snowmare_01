# Builds the CodexTactics editor target (Development, Win64) via UnrealBuildTool.
# Full UBT output goes to Saved\Logs\Build.log; the console gets the result line,
# project-code warnings/errors (engine-header deprecation noise excluded), and the log path.
# Takes the shared build lock (Scripts\agent_lock.ps1) and refuses while this project's Unreal Editor is open.
# Before UBT it runs Scripts\Tools\find_unity_clashes.py (file-local names defined in two .cpp files of one module: they
# break a unity blob on main although each branch built - HANDOFF section 2) and prints its findings as yellow warnings.
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\build.ps1 [-Target CodexTacticsEditor] [-Config Development]
#        [-NoUnity]     every .cpp compiled alone (UBT -DisableUnity): proves each file includes what it uses
#        [-FullUnity]   unity blobs also for the files changed in git (UBT -DisableAdaptiveUnity): what main's build sees
param(
    [string]$Target = "CodexTacticsEditor",
    [string]$Config = "Development",
    [switch]$NoUnity,
    [switch]$FullUnity
)

$ErrorActionPreference = "Stop"
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$LogFile = Join-Path $ProjectDir "Saved\Logs\Build.log"
New-Item -ItemType Directory -Force (Split-Path $LogFile) | Out-Null

# Only an editor of THIS checkout locks our DLLs (another project's editor, or the user's editor on the main folder while we
# build in the agents' worktree ../CodexTactics-agents, may stay open): match the full .uproject path.
$ProjectFull = [IO.Path]::GetFullPath($Project)
if (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object {
        $_.CommandLine -and ($_.CommandLine.Replace('/', '') -like "*$ProjectFull*") }) {
    Write-Host "Unreal Editor (CodexTactics) is running: close it first (it locks the DLLs)." -ForegroundColor Yellow
    exit 2
}

# Unity-build name clashes (a few seconds, no compiler): warn, never block the build.
$ClashScript = Join-Path (Join-Path $PSScriptRoot "Tools") "find_unity_clashes.py"
if ((Test-Path $ClashScript) -and (Get-Command python -ErrorAction SilentlyContinue)) {
    try { $clashOut = & python $ClashScript --root $ProjectDir --quiet --warn-only } catch { $clashOut = @() }
    $clashOut | ForEach-Object { Write-Host "WARNING (unity clash): $_" -ForegroundColor Yellow }
}

$UbtExtra = @()
if ($FullUnity) { $UbtExtra += "-DisableAdaptiveUnity" }
# Both proof builds re-plan the target: the cached UBT makefile ignores changed unity settings ("Target is up to date", also
# with -NoUBTMakefiles), so it is deleted (UBT writes a new one; the next normal build re-plans once more).
if ($FullUnity -or $NoUnity) {
    Get-ChildItem (Join-Path (Join-Path $ProjectDir "Intermediate") "Build") -Recurse -Filter "Makefile.bin" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match [regex]::Escape($Target) } | Remove-Item -Force -ErrorAction SilentlyContinue
}
# -NoUnity: UBT 5.8 ignores -DisableUnity for this editor target (it still compiled the Module.*.cpp blobs, 2026-10-19), so the
# project's modules are listed in the project-scope BuildConfiguration.xml (ModuleConfiguration / DisableUnityBuild) for this
# build only; the file is restored (or removed) afterwards.
$UbtConfig = Join-Path (Join-Path (Join-Path $ProjectDir "Saved") "UnrealBuildTool") "BuildConfiguration.xml"
$UbtConfigBefore = $null
$UbtConfigExisted = Test-Path $UbtConfig
if ($NoUnity) {
    if ($UbtConfigExisted) { $UbtConfigBefore = [IO.File]::ReadAllText($UbtConfig) }
    $modules = Get-ChildItem (Join-Path $ProjectDir "Source") -Directory | Where-Object { Get-ChildItem $_.FullName -Filter "*.Build.cs" } |
        ForEach-Object { "      <Item>$($_.Name)</Item>" }
    $xml = @(
        '<?xml version="1.0" encoding="utf-8" ?>',
        '<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">',
        '  <ModuleConfiguration>',
        '    <DisableUnityBuild>') + $modules + @(
        '    </DisableUnityBuild>',
        '  </ModuleConfiguration>',
        '</Configuration>')
    New-Item -ItemType Directory -Force (Split-Path $UbtConfig) | Out-Null
    [IO.File]::WriteAllText($UbtConfig, ($xml -join "`r`n"))
}
. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock "build $Target"
try {
    & "$EngineRoot\Engine\Build\BatchFiles\Build.bat" $Target Win64 $Config "-Project=$Project" -WaitMutex -NoHotReloadFromIDE @UbtExtra *> $LogFile
    $code = $LASTEXITCODE
} finally {
    if ($Owned) { Exit-AgentLock }
    if ($NoUnity) {
        if ($UbtConfigExisted) { [IO.File]::WriteAllText($UbtConfig, $UbtConfigBefore) } else { Remove-Item $UbtConfig -ErrorAction SilentlyContinue }
    }
}

$lines = Get-Content $LogFile
$issues = $lines | Where-Object { $_ -match "(error|warning)" -and $_ -notmatch "Epic Games\\UE_5\.8\\Engine\\" -and $_ -notmatch "not a preferred version|has not been heavily tested" }
$issues | Select-Object -First 40 | ForEach-Object { Write-Host $_ }
if (@($issues).Count -gt 40) { Write-Host ("... {0} more issue lines" -f (@($issues).Count - 40)) }
$lines | Where-Object { $_ -match "^Result:|Total execution time" } | ForEach-Object { Write-Host $_ }
Write-Host "Exit code $code. Full log: $LogFile ($((Get-Item $LogFile).Length) bytes)"
exit $code
