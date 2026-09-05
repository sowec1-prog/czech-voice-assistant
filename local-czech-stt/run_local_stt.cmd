@echo off
cd /d "%~dp0"
".venv\Scripts\python.exe" server.py >> "server.log" 2>&1
