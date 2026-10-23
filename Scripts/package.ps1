# Packages a Windows build of CodexTactics via RunUAT BuildCookRun (tester build, game designer 2026-10-23).
#
# What is cooked comes from Config/DefaultGame.ini [/Script/UnrealEd.ProjectPackagingSettings]: MapsToCook (menu + the
# campaign levels) and DirectoriesToAlwaysCook (folders loaded by path), so the content packs nobody references stay out.
# Pak + IoStore + compression, no debug files, no prerequisites installer: the lightest build a tester can run.
#
# Takes the shared build lock (Scripts\agent_lock.ps1) and refuses while this project's Unreal Editor is open.
#
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\package.ps1 [-Config Shipping|Development] [-SkipCook] [-Zip]
#   -Config      Shipping (default, no console / logs) or Development (console + log, used to verify a cook).
#   -SkipCook    reuse the cooked content of the previous run (e.g. a Development check first, then Shipping).
#   -Zip         also writes Saved\Packages\CodexTactics-<Config>-<date>-<sha>.zip of the archived build.
param(
    [ValidateSet("Shipping", "Development")]
    [string]$Config = "Shipping",
    [switch]$SkipCook,
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectDir "CodexTactics.uproject"
$EngineDir = "C:\Program Files\Epic Games\UE_5.8\Engine"
$RunUAT = Join-Path $EngineDir "Build\BatchFiles\RunUAT.bat"
$ArchiveDir = Join-Path $ProjectDir "Saved\Packages\$Config"
$LogFile = Join-Path $ProjectDir "Saved\Logs\Package-$Config.log"
New-Item -ItemType Directory -Force (Split-Path $LogFile) | Out-Null

$ProjectFull = [IO.Path]::GetFullPath($Project)
if (Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor.exe'" | Where-Object {
        $_.CommandLine -and ($_.CommandLine.Replace('/', '') -like "*$ProjectFull*") -and $_.CommandLine -notmatch '-game|-run=' }) {
    Write-Host "Unreal Editor (CodexTactics) is running: close it first (the cook needs the project to itself)." -ForegroundColor Yellow
    exit 2
}

. (Join-Path $PSScriptRoot "agent_lock.ps1")
$Owned = Enter-AgentLock "package $Config" -Minutes 120
try {
    if (Test-Path $ArchiveDir) { Remove-Item -Recurse -Force $ArchiveDir }
    $UatArgs = @(
        "BuildCookRun", "-project=`"$Project`"", "-noP4", "-utf8output", "-unattended",
        "-platform=Win64", "-clientconfig=$Config", "-build", "-stage", "-pak", "-iostore", "-compressed",
        "-archive", "-archivedirectory=`"$ArchiveDir`"", "-nodebuginfo"
    )
    if ($SkipCook) { $UatArgs += "-skipcook" } else { $UatArgs += "-cook" }
    if ($Config -eq "Shipping") { $UatArgs += "-distribution" }

    Write-Host "RunUAT $($UatArgs -join ' ')"
    & cmd /c "`"$RunUAT`" $($UatArgs -join ' ') > `"$LogFile`" 2>&1"
    $Code = $LASTEXITCODE
    Get-Content $LogFile -Tail 8
    if ($Code -ne 0) { Write-Host "BuildCookRun failed (exit $Code). Full log: $LogFile" -ForegroundColor Red; exit $Code }

    $Build = Get-ChildItem $ArchiveDir -Directory | Select-Object -First 1
    $SizeMB = [math]::Round((Get-ChildItem $Build.FullName -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
    Write-Host "Build: $($Build.FullName) ($SizeMB MB)"

    if ($Zip) {
        $Sha = (& git -C $ProjectDir rev-parse --short HEAD).Trim()
        $ZipPath = Join-Path $ProjectDir ("Saved\Packages\CodexTactics-$Config-{0}-$Sha.zip" -f (Get-Date -Format "yyyy-MM-dd"))
        if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
        Compress-Archive -Path (Join-Path $Build.FullName "*") -DestinationPath $ZipPath -CompressionLevel Optimal
        Write-Host ("Zip: $ZipPath ({0} MB)" -f [math]::Round((Get-Item $ZipPath).Length / 1MB, 1))
    }
} finally {
    if ($Owned) { Exit-AgentLock }
}
