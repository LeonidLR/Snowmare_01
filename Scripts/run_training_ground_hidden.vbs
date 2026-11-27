' Starts Training Ground (Tools/TrainingGround, npm run dev on http://localhost:5173) WITHOUT a console window, then opens the browser.
' Designer 2026-11-25: no PowerShell / cmd windows. Stop it from Task Manager (node.exe ... vite.js) or Scripts\run_training_ground.bat shows the log.
Set sh = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
root = fs.GetParentFolderName(fs.GetParentFolderName(WScript.ScriptFullName))
sh.CurrentDirectory = root & "\Tools\TrainingGround"
' 2026-11-27: the fight replay reads the play copy's recordings (the designer plays there); Scripts\stop_training_ground.bat stops the server.
play = fs.GetParentFolderName(root) & "\CodexTactics-play\Saved\Demos"
If fs.FolderExists(play) Then sh.Environment("Process")("TG_DEMOS_DIR") = play
' A second click only opens the browser on the server that already runs (no second npm on a taken port).
up = (sh.Run("powershell.exe -NoProfile -Command ""if (Get-NetTCPConnection -State Listen -LocalPort 5173 -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }""", 0, True) = 0)
If Not up Then
    sh.Run "cmd /c npm run dev -- --port 5173 --strictPort > """ & root & "\Saved\Logs\TrainingGround.log"" 2>&1", 0, False
    WScript.Sleep 4000
End If
sh.Run "http://localhost:5173/?department=replay", 1, False
