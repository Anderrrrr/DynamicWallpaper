#Requires AutoHotkey v2.0
; Simple screenshot script
CoordMode("Mouse", "Screen")
CoordMode("Pixel", "Screen")
CoordMode("ToolTip", "Screen")

FileDelete("c:\DynamicWallpaperSource\screenshot.png")
RunWait("powershell -c `"$Host.UI.RawUI.WindowTitle = 'Screenshot'; Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; `$bmp = New-Object System.Drawing.Bitmap([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width, [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Height); `$gfx = [System.Drawing.Graphics]::FromImage(`$bmp); `$gfx.CopyFromScreen(0, 0, 0, 0, `$bmp.Size); `$bmp.Save('c:\DynamicWallpaperSource\screenshot.png', [System.Drawing.Imaging.ImageFormat]::Png); `$gfx.Dispose(); `$bmp.Dispose();`"", , "Hide")
ExitApp
