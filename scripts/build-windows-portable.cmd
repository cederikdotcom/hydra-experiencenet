@echo off
setlocal enableextensions
cd /d "%~dp0.."
set "SOURCE_ROOT=%CD%"
if defined CI_VERSION (
    powershell -NoProfile -NonInteractive -Command "$v=$env:CI_VERSION -replace '^v',''; if($v -notmatch '^\d+\.\d+\.\d+$'){throw 'Expected release version vX.Y.Z'}; [IO.File]::WriteAllText('app/version.txt',$v)"
    if errorlevel 1 exit /b 1
)
for /f "usebackq delims=" %%i in (`scripts\vswhere.exe -latest -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvarsall.bat" AMD64
if errorlevel 1 exit /b 1
for /f "usebackq delims=" %%i in (`scripts\vswhere.exe -latest -find VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT`) do set "VC_REDIST_DLL_PATH=%%i"
if exist build\windows-kiosk rmdir /s /q build\windows-kiosk
if exist build\windows-kiosk-deploy rmdir /s /q build\windows-kiosk-deploy
if exist build\HydraExperienceNet-Windows-x64.zip del build\HydraExperienceNet-Windows-x64.zip
mkdir build\windows-kiosk
mkdir build\windows-kiosk-deploy
pushd build\windows-kiosk
qmake "%SOURCE_ROOT%\moonlight-qt.pro" CONFIG+=release
if errorlevel 1 exit /b 1
"%SOURCE_ROOT%\scripts\jom.exe" release
if errorlevel 1 exit /b 1
popd
set "DEPLOY=%SOURCE_ROOT%\build\windows-kiosk-deploy"
copy build\windows-kiosk\app\release\HydraExperienceNet.exe "%DEPLOY%\"
if errorlevel 1 exit /b 1
copy libs\windows\lib\x64\*.dll "%DEPLOY%\"
if errorlevel 1 exit /b 1
copy build\windows-kiosk\AntiHooking\release\AntiHooking.dll "%DEPLOY%\"
if errorlevel 1 exit /b 1
copy app\SDL_GameControllerDB\gamecontrollerdb.txt "%DEPLOY%\"
if errorlevel 1 exit /b 1
copy "%VC_REDIST_DLL_PATH%\*.dll" "%DEPLOY%\"
if errorlevel 1 exit /b 1
windeployqt --release --dir "%DEPLOY%" --qmldir app\gui --no-compiler-runtime --no-opengl-sw --no-ffmpeg "%DEPLOY%\HydraExperienceNet.exe"
if errorlevel 1 exit /b 1
7z a build\HydraExperienceNet-Windows-x64.zip "%DEPLOY%\*"
exit /b %ERRORLEVEL%
