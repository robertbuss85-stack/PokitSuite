@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -WindowStyle Hidden -Command "Start-Process -FilePath 'python.exe' -ArgumentList 'PokitSuite.py' -WorkingDirectory '%~dp0' -WindowStyle Hidden"
