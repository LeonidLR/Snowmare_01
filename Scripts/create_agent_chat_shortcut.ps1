# Creates "CodexTactics Chat.lnk" on the Desktop and in the Start Menu Programs folder.
$Repo = Split-Path -Parent $PSScriptRoot
$Vbs = Join-Path $PSScriptRoot 'AgentChatApp.vbs'
$Ico = Join-Path $Repo 'Tools\AgentChat\agent_chat.ico'
$sh = New-Object -ComObject WScript.Shell
$targets = @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))
foreach ($dir in $targets) {
    $lnk = $sh.CreateShortcut((Join-Path $dir 'CodexTactics Chat.lnk'))
    $lnk.TargetPath = "$env:WINDIR\System32\wscript.exe"
    $lnk.Arguments = "`"$Vbs`""
    $lnk.WorkingDirectory = $Repo
    $lnk.IconLocation = "$Ico,0"
    $lnk.Description = 'CodexTactics agent chat'
    $lnk.Save()
    Write-Host "Created: $dir\CodexTactics Chat.lnk"
}
