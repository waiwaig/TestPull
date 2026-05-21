@echo off
call "D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b 1
msbuild "%~dp0ViewportFreezeCopyPaste.vcxproj" /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b 1
echo Build succeeded.
