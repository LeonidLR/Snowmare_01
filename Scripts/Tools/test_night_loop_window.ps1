$ErrorActionPreference = 'Stop'
$path = Join-Path $PSScriptRoot 'worker_night_loop.ps1'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
# Execute the production time resolver alone; never start the live night loop.
$function = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'TrainerAt' }, $true)
Invoke-Expression $function.Extent.Text
$deadline = [datetime]'2026-11-28T08:00:00'
foreach ($case in @(
    @('16:30', [datetime]'2026-11-27T16:30:00'),
    @('05:30', [datetime]'2026-11-28T05:30:00'),
    @('07:30', [datetime]'2026-11-28T07:30:00'),
    @('08:00', [datetime]'2026-11-28T08:00:00')
)) {
    if ((TrainerAt $case[0]) -ne $case[1]) { throw "Wrong night boundary for $($case[0])" }
}
$deadline = [datetime]'2026-11-27T23:59:00'
if ((TrainerAt '16:30') -ne [datetime]'2026-11-27T16:30:00') { throw 'Same-day window changed' }
Write-Host 'PASS night-loop window: evening start, morning end, legacy 05:30 window, same-day window'
