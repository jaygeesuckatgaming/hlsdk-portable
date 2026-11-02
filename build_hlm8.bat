@echo off
TITLE Build Half-Life m8
cd /d "%~dp0"
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release
pause
