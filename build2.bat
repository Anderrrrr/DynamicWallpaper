@echo off
for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set InstallDir=%%i
call "%InstallDir%\VC\Auxiliary\Build\vcvars64.bat" > NUL 2>&1
if not exist build_msvc\Release mkdir build_msvc\Release
cd build_msvc\Release
del /F /Q DW_new.exe 2>NUL
cl.exe /O2 /MD /EHsc /W3 /Fe"DW_new.exe" ..\..\src\main.cpp ..\..\src\VideoPlayer.cpp user32.lib shell32.lib mf.lib mfplat.lib mfuuid.lib shlwapi.lib ole32.lib gdi32.lib strmiids.lib evr.lib
echo BUILD_EXIT=%ERRORLEVEL%
