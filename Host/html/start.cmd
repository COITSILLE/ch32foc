@echo off
setlocal
cd /d "%~dp0"

set "PORT=5180"
set "URL=http://localhost:%PORT%/"

where node >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Node.js not found. Install Node.js 18+ first: https://nodejs.org/
  pause
  exit /b 1
)

if not exist "node_modules" (
  echo [1/2] Installing dependencies, first run only...
  call npm install
  if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
  )
)

echo [2/2] Starting dev server...
echo.
echo   URL  : %URL%
echo   Stop : press Ctrl+C in this window
echo.

start "" powershell -NoProfile -WindowStyle Hidden -Command "Start-Sleep -Seconds 3; Start-Process '%URL%'"
call npm run dev -- --port %PORT% --strictPort

echo.
echo Dev server stopped.
pause
