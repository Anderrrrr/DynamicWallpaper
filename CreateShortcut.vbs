Set oWS = WScript.CreateObject("WScript.Shell")
desktopFolder = oWS.SpecialFolders("Desktop")
sLinkFile = desktopFolder & "\Dynamic Wallpaper UI.lnk"
Set oLink = oWS.CreateShortcut(sLinkFile)
oLink.TargetPath = "C:\DynamicWallpaperSource\DynamicWallpaperUI.bat"
oLink.WorkingDirectory = "C:\DynamicWallpaperSource"
oLink.IconLocation = "shell32.dll, 116"
oLink.Save
