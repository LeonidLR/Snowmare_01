# Opens the Unreal editor on the recommended worktree if editor_status.py says GREEN.
# YELLOW: asks "open anyway?"; RED: plain message. Never starts anything but the editor.
Add-Type -AssemblyName System.Windows.Forms
$Main = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Engine = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$raw = & python (Join-Path $PSScriptRoot "editor_status.py") --json
$st = $raw | ConvertFrom-Json
$v = $st.verdict
$rec = $st.recommend
$wt = $st.worktrees | Where-Object { $_.name -eq $v.recommended } | Select-Object -First 1
if (-not $wt -and $v.suggested) { $wt = $st.worktrees | Where-Object { $_.name -eq $v.suggested } | Select-Object -First 1 }
function Open-Wt($w) {
    if ($w.editor_open) {
        [System.Windows.Forms.MessageBox]::Show("Редактор уже открыт в $($w.name).", "CodexTactics", "OK", "Information") | Out-Null
        return
    }
    Start-Process -FilePath $Engine -ArgumentList ('"' + (Join-Path $w.path "CodexTactics.uproject") + '"')
}
switch ($v.level) {
    "GREEN" { Open-Wt $wt }
    "YELLOW" {
        $ans = [System.Windows.Forms.MessageBox]::Show("$($v.reason)`n`nОткрыть всё равно?", "CodexTactics", "YesNo", "Warning")
        if ($ans -eq "Yes" -and $wt) { Open-Wt $wt }
    }
    default { [System.Windows.Forms.MessageBox]::Show($v.reason, "CodexTactics", "OK", "Stop") | Out-Null }
}
