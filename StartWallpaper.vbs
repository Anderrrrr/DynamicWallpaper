' =========================================================================
' 動態桌布 (Dynamic Wallpaper) 自動啟動與背景執行腳本
' =========================================================================
' 使用說明：
' 1. 請將您想要播放的 MP4 影片路徑填入下方的 VideoPath1 變數中 (如果有雙螢幕且想分開播放，可填寫 VideoPath2)。
' 2. 選擇您的 Mode ( "span" 或 "monitors" )。
' 3. 設定完成後，您可以對本檔案點擊「右鍵 -> 建立捷徑」。
' 4. 將該「捷徑」剪下，並貼到您的 Windows 啟動資料夾內：
'    (按下 Win + R，輸入 shell:startup 並按下 Enter 即可開啟該資料夾)
' 
' 這樣每次開機時，動態桌布就會在背景默默幫您掛上，而不會彈出黑色的醜陋 CMD 視窗！
' =========================================================================

Dim WshShell
Set WshShell = CreateObject("WScript.Shell")

' 獲取當前腳本所在的目錄
Dim ScriptDir
ScriptDir = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName)

' -------------------------------------------------------------------------
' [設定區]
' -------------------------------------------------------------------------
' 1. 請在此輸入您的影片完整路徑 (記得保留雙引號)
Dim VideoPath1, VideoPath2
VideoPath1 = ScriptDir & "\pixel-rain-traffic.3840x2160.mp4" 
VideoPath2 = "D:\您的影片路徑\video2.mp4" ' 如果您沒有第二支影片，或者只有單螢幕，可以不用理會這個。

' 2. 請選擇播放模式 ( span 或 monitors )
Dim Mode
Mode = "span" 

' -------------------------------------------------------------------------
' 組合執行指令
' -------------------------------------------------------------------------
Dim ExePath, Command
ExePath = ScriptDir & "\build_msvc\Release\DynamicWallpaper.exe"

If Mode = "span" Then
    Command = """" & ExePath & """ --span """ & VideoPath1 & """"
Else
    ' 若您想要不同螢幕播放不同影片，可以把 VideoPath2 加進去，例如： & """ """ & VideoPath2 & """"
    Command = """" & ExePath & """ --monitors """ & VideoPath1 & """"
End If

' 以隱藏視窗模式 (0) 執行我們的動態桌布程式
WshShell.Run Command, 0, False
Set WshShell = Nothing
