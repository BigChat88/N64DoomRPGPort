@echo off
rem  build.cmd -- one-click Doom RPG N64 ROM build.
rem
rem  1. put your Doom RPG BREW archive (doomrpg.zip or doomrpg.bar) in  input\
rem  2. run this file
rem  3. the ROM appears in  output\doomrpg.z64
rem
rem  Extra options are passed straight through, e.g.:
rem     build.cmd --no-audio
rem     build.cmd --soundfont "C:\path\bank.sf2"
setlocal
cd /d "%~dp0"

set "PY=python"
where python >nul 2>&1 || set "PY=py -3"

%PY% app\build_rom.py %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo Build FAILED ^(exit %RC%^).
) else (
    echo Done.  ROM: output\doomrpg.z64
)
pause
exit /b %RC%
