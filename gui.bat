@echo off
cd /d "%~dp0"
start "Omni Pedal screen" py -3 gui\server.py
timeout /t 1 /nobreak >nul
start "" http://127.0.0.1:8080
