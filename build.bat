@echo off
setlocal enabledelayedexpansion

echo 尋找 Visual Studio 編譯環境...

for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
  set InstallDir=%%i
)

if "%InstallDir%"=="" (
  echo 找不到 Visual Studio 安裝路徑！
  pause
  exit /b 1
)

echo 成功找到 VS 安裝路徑: %InstallDir%
echo 正在設定編譯環境...

call "%InstallDir%\VC\Auxiliary\Build\vcvars64.bat"

if not exist build_msvc\Release mkdir build_msvc\Release
cd build_msvc\Release

echo 正在編譯 C++ 原始碼...
cl.exe /O2 /MD /EHsc /W3 /Fe"DynamicWallpaper.exe" ..\..\src\main.cpp ..\..\src\VideoPlayer.cpp user32.lib shell32.lib mf.lib mfplat.lib mfuuid.lib shlwapi.lib ole32.lib gdi32.lib strmiids.lib evr.lib

if %ERRORLEVEL% equ 0 (
  echo.
  echo 編譯成功！
  echo 執行檔位置: build_msvc\Release\DynamicWallpaper.exe
  echo.
  DynamicWallpaper.exe
) else (
  echo.
  echo 編譯失敗！請檢查錯誤訊息。
)
