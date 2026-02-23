@echo off
setlocal
echo 尋找 Visual Studio 編譯器...
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > vspath2.txt
set /p VSPATH=<vspath2.txt
echo 找到 VS 安裝路徑: %VSPATH%

echo 正在設定編譯器環境...
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x64

echo 正在編譯 hwnd_debug2...
cl.exe hwnd_debug2.cpp user32.lib

echo 執行 hwnd_debug2...
hwnd_debug2.exe > hwnd_output2.txt
type hwnd_output2.txt
endlocal
