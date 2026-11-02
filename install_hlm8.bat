@echo off
TITLE Install Half-Life m8
set HL=C:\Program Files (x86)\Steam\steamapps\common\Half-Life
set HERE=%~dp0

if not exist "%HL%\hlm8\dlls" mkdir "%HL%\hlm8\dlls"
if not exist "%HL%\hlm8\cl_dlls" mkdir "%HL%\hlm8\cl_dlls"

copy /Y "%HERE%build\dlls\Release\hl.dll" "%HL%\hlm8\dlls\hl.dll"
copy /Y "%HERE%build\cl_dll\Release\client.dll" "%HL%\hlm8\cl_dlls\client.dll"
copy /Y "%HERE%mod\liblist.gam" "%HL%\hlm8\liblist.gam"

echo Installed to "%HL%\hlm8".
pause
