@echo off
rem Double-click launcher for the message board backend + Cloudflare Tunnel
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-server.ps1"
pause
