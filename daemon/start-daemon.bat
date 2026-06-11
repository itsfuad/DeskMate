@echo off
setlocal

REM Launch the smalltv usage daemon silently (no console window) with the tray icon.
REM Double-click to start it now. For auto-start at login, run install.bat instead.
REM First run only:  py -m pip install -r requirements.txt
REM
REM pythonw.exe = windowless Python, resolved from PATH. If "where pythonw" shows a
REM "...\WindowsApps\pythonw.exe" entry, that is the Microsoft Store alias stub (it
REM opens the Store instead of running Python) - point this at your real interpreter:
REM   set SMALLTV_PYTHONW=C:\Python314\pythonw.exe

if defined SMALLTV_PYTHONW (
    set "PYW=%SMALLTV_PYTHONW%"
) else (
    set "PYW=pythonw.exe"
)

cd /d "%~dp0"
start "" "%PYW%" "smalltv_usage_daemon.py" --tray %*
