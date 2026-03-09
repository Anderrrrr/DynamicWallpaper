' =========================================================================
' 讓 Python UI 在背景接管開機啟動
' =========================================================================
Dim WshShell, ScriptDir, Command
Set WshShell = CreateObject("WScript.Shell")

' 獲取當前腳本所在的目錄 (專案根目錄)
ScriptDir = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName)

' 切換工作目錄到專案根目錄 (非常重要，這樣 pythonw 才能找到 WebUI\launcher.py)
WshShell.CurrentDirectory = ScriptDir

' 組合指令：使用 pythonw 執行 launcher.py 並加上 --bg (背景執行)
Command = "pythonw WebUI\launcher.py --bg"

' 以隱藏模式 (0) 執行 Python 背景常駐與 API 伺服器
WshShell.Run Command, 0, False
Set WshShell = Nothing