' Starts Training Ground (Tools/TrainingGround, npm run dev on http://localhost:5173) WITHOUT a console window, then opens the browser.
' Designer 2026-11-25: no PowerShell / cmd windows. Stop it from Task Manager (node.exe ... vite.js) or Scripts\run_training_ground.bat shows the log.
Set sh = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
root = fs.GetParentFolderName(fs.GetParentFolderName(WScript.ScriptFullName))
sh.CurrentDirectory = root & "\Tools\TrainingGround"
sh.Run "cmd /c npm run dev -- --port 5173 --strictPort > """ & root & "\Saved\Logs\TrainingGround.log"" 2>&1", 0, False
WScript.Sleep 4000
sh.Run "http://localhost:5173", 1, False
