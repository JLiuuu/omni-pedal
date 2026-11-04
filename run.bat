@echo off
cd /d "%~dp0"
for /f "delims=" %%i in ('C:\cygwin64\bin\cygpath.exe -u "%CD%"') do set "UDIR=%%i"
C:\cygwin64\bin\bash.exe -lc "cd '%UDIR%' && make test demo"
echo.
echo Report: out\report.txt
echo Wavs:   out\input.wav  out\output.wav  out\output_no_mute.wav
pause
