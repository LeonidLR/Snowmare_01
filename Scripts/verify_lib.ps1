# Shared helpers for test.ps1 -Changed, smoke_parallel.ps1 and verify_all.ps1: the impact map (Scripts/test_map.json),
# glob matching and the changed-file list. Dot-source it: . (Join-Path $PSScriptRoot "verify_lib.ps1")
# No param() block (a dot-sourced param block would leak variables into the caller).

$script:VerifyProjectDir = Split-Path $PSScriptRoot -Parent
$script:TestMapPath = Join-Path $PSScriptRoot "test_map.json"

function Get-TestMap {
    return Get-Content $script:TestMapPath -Raw -Encoding UTF8 | ConvertFrom-Json
}

# Glob over repo-relative forward-slash paths: '**/' = zero or more directories, '**' = anything, '*' = inside one path part.
function Convert-GlobToRegex([string]$Glob) {
    $r = [regex]::Escape($Glob)
    $r = $r -replace '\\\*\\\*/', '(.*/)?' -replace '\\\*\\\*', '.*' -replace '\\\*', '[^/]*' -replace '\\\?', '[^/]'
    return "^$r$"
}

# Changed files against a base (default HEAD): tracked changes (worktree + index) plus untracked files, forward slashes.
function Get-ChangedFiles([string]$Base = "HEAD") {
    Push-Location $script:VerifyProjectDir
    try {
        $tracked = @(git -c core.quotepath=off diff --name-only $Base 2>$null)
        $untracked = @(git -c core.quotepath=off ls-files --others --exclude-standard 2>$null)
    } finally { Pop-Location }
    return @($tracked + $untracked | Where-Object { $_ } | ForEach-Object { $_ -replace '\\', '/' } | Sort-Object -Unique)
}

# Smoke command name from a Debug/<Name>Command.cpp path, if that file registers a smoke; $null otherwise.
function Get-SmokeOfDebugFile([string]$Path, $Map) {
    if ($Path -match '/Debug/(\w+Smoke)Command\.cpp$' -and ($Map.smokeRegistry -contains $Matches[1])) { return $Matches[1] }
    return $null
}

# Test filters of an edited test file (its own automation test names, shortened to CodexTactics.<A>.<B>).
function Get-FiltersOfTestFile([string]$Path) {
    $full = Join-Path $script:VerifyProjectDir $Path
    if (-not (Test-Path $full)) { return @() }
    $text = Get-Content $full -Raw -Encoding UTF8
    $names = [regex]::Matches($text, '"(CodexTactics\.[A-Za-z0-9_]+\.[A-Za-z0-9_]+)') | ForEach-Object { $_.Groups[1].Value }
    $macro = [regex]::Matches($text, '"((?:[A-Z][A-Za-z0-9]+)\.[A-Za-z0-9_]+)\.') | ForEach-Object { "CodexTactics." + $_.Groups[1].Value }
    return @($names + $macro | Sort-Object -Unique)
}

# Collapse a filter list to its shortest prefixes ("CodexTactics.Tactics" swallows "CodexTactics.Tactics.Cover").
function Compress-Filters([string[]]$Filters) {
    $sorted = @($Filters | Where-Object { $_ } | Sort-Object -Unique | Sort-Object Length)
    $out = @()
    foreach ($f in $sorted) {
        $covered = $false
        foreach ($p in $out) { if ($f -eq $p -or $f.StartsWith($p + ".")) { $covered = $true; break } }
        if (-not $covered) { $out += $f }
    }
    return $out
}

# Impact of a set of changed files. Returns tests (collapsed), smokes (ordered, unique), quickSmokes, per-file table and unmapped paths.
function Resolve-Impact([string[]]$Files, $Map, [int]$QuickCount = 0) {
    if ($QuickCount -le 0) { $QuickCount = [int]$Map.quickCount }
    $rules = @($Map.rules | ForEach-Object { [pscustomobject]@{ Rule = $_; Regexes = @($_.globs | ForEach-Object { Convert-GlobToRegex $_ }) } })
    $tests = [System.Collections.Generic.List[string]]::new()
    $smokes = [System.Collections.Generic.List[string]]::new()
    $quick = [System.Collections.Generic.List[string]]::new()
    $rows = @()
    $unmapped = @()
    foreach ($file in $Files) {
        $hit = @($rules | Where-Object { $rx = $_.Regexes; @($rx | Where-Object { $file -match $_ }).Count -gt 0 })
        $fileSmokes = @()
        $fileTests = @()
        foreach ($h in $hit) {
            $fileTests += @($h.Rule.tests)
            $fileSmokes += @($h.Rule.smokes)
            @($h.Rule.smokes) | Select-Object -First $QuickCount | ForEach-Object { $quick.Add($_) }
        }
        $own = Get-SmokeOfDebugFile $file $Map
        if ($own) { $fileSmokes = @($own) + $fileSmokes; $quick.Insert(0, $own) }
        if ($file -match '^Source/CodexTacticsTests/.*\.cpp$') { $fileTests += Get-FiltersOfTestFile $file }
        if ($hit.Count -eq 0 -and -not $own -and $file -notmatch '^Source/CodexTacticsTests/') {
            if ($file -match '^Source/') {
                $unmapped += $file
                $fileTests += @($Map.fallback.tests)
                $fileSmokes += @($Map.fallback.smokes)
                @($Map.fallback.smokes) | Select-Object -First $QuickCount | ForEach-Object { $quick.Add($_) }
            }
            # Anything else outside Source/ with no rule (stray files) verifies nothing.
        }
        $fileTests | Where-Object { $_ } | ForEach-Object { $tests.Add($_) }
        $fileSmokes | Where-Object { $_ } | ForEach-Object { $smokes.Add($_) }
        $rows += [pscustomobject]@{ File = $file; Rules = (($hit | ForEach-Object { $_.Rule.name }) -join ","); Tests = @($fileTests).Count; Smokes = @($fileSmokes | Select-Object -Unique).Count }
    }
    return [pscustomobject]@{
        Tests = @(Compress-Filters $tests)
        Smokes = @($smokes | Select-Object -Unique)
        QuickSmokes = @($quick | Select-Object -Unique)
        Rows = $rows
        Unmapped = $unmapped
    }
}

# Every Debug/*SmokeCommand.cpp smoke must be in the registry (or the non-smoke list) and every registry smoke must be reachable from a rule.
function Test-MapCoverage($Map) {
    $problems = @()
    $declared = @()
    Get-ChildItem (Join-Path $script:VerifyProjectDir "Source\CodexTactics\Private\Debug") -Filter *.cpp | ForEach-Object {
        [regex]::Matches((Get-Content $_.FullName -Raw -Encoding UTF8), 'TEXT\("CodexTactics\.(\w+)"\)') | ForEach-Object { $declared += $_.Groups[1].Value }
    }
    foreach ($name in ($declared | Sort-Object -Unique)) {
        if (($Map.smokeRegistry -notcontains $name) -and ($Map.nonSmokeDebugCommands -notcontains $name)) { $problems += "Debug command '$name' is in neither smokeRegistry nor nonSmokeDebugCommands" }
    }
    foreach ($name in $Map.smokeRegistry) {
        if ($declared -notcontains $name) { $problems += "smokeRegistry '$name' has no console command in Source/CodexTactics/Private/Debug" }
        $mapped = @($Map.rules | Where-Object { @($_.smokes) -contains $name }).Count + @($Map.fallback.smokes | Where-Object { $_ -eq $name }).Count
        if ($mapped -eq 0) { $problems += "smoke '$name' is mapped by no rule" }
    }
    $dups = $Map.smokeRegistry | Group-Object | Where-Object Count -gt 1 | ForEach-Object Name
    foreach ($d in $dups) { $problems += "smokeRegistry lists '$d' twice" }
    return $problems
}
