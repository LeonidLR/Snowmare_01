# Opens the CodexTactics agent chat as a standalone desktop window (Edge app mode).
# Starts the chat server hidden if nothing listens on 127.0.0.1:8765.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$Port = 8765
$Url = "http://127.0.0.1:$Port"
$Repo = Split-Path -Parent $PSScriptRoot
$LogDir = Join-Path $Repo 'Saved\Logs'
$Log = Join-Path $LogDir 'AgentChatServer.log'
$Profile = Join-Path $Repo 'Saved\AgentChatApp\EdgeProfile'

function Test-Port {
    $c = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $c.BeginConnect('127.0.0.1', $Port, $null, $null)
        if ($iar.AsyncWaitHandle.WaitOne(500) -and $c.Connected) { return $true }
        return $false
    } catch { return $false } finally { $c.Close() }
}

function Test-Http {
    try { return (Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 2).StatusCode -eq 200 } catch { return $false }
}

if (-not (Test-Port)) {
    New-Item -ItemType Directory -Force $LogDir, $Profile | Out-Null
    $py = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source
    if (-not $py) { $py = (Get-Command python.exe -ErrorAction Stop).Source }
    $errLog = Join-Path $LogDir 'AgentChatServer.err.log'
    Start-Process -FilePath $py -ArgumentList @('-u', 'Scripts\Tools\agent_chat_server.py', '--port', "$Port") `
        -WorkingDirectory $Repo -WindowStyle Hidden -RedirectStandardOutput $Log -RedirectStandardError $errLog
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and -not (Test-Http)) { Start-Sleep -Milliseconds 300 }
    if (-not (Test-Http)) {
        [System.Windows.Forms.MessageBox]::Show("Agent chat server did not start within 15 s.`nLog: $Log`nErrors: $errLog", 'CodexTactics Chat') | Out-Null
        exit 1
    }
}

New-Item -ItemType Directory -Force $Profile | Out-Null
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if ($edge) {
    Start-Process -FilePath $edge -ArgumentList @("--app=$Url", "--user-data-dir=`"$Profile`"", '--window-size=1400,950')
} else {
    Start-Process $Url
}
