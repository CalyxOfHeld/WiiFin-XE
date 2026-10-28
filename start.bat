@echo off
setlocal
cd /d "%~dp0"

echo Building WiiFin-XE documentation site...
where python >nul 2>&1
if %ERRORLEVEL% equ 0 (
    python tools\make_site.py
) else (
    echo Python not found on PATH. Using existing build if available...
)

if not exist "build\site\index.html" (
    echo.
    echo Error: Site build not found at build\site\index.html
    echo Please ensure Python and required packages are installed:
    echo   pip install markdown-it-py mdit-py-plugins
    pause
    exit /b 1
)

if "%1"=="--server" goto server
if "%1"=="-s" goto server
if "%1"=="server" goto server

echo Opening WiiFin-XE site in default browser...
start "" "%~dp0build\site\index.html"
goto end

:server
echo Starting local server at http://localhost:8000 ...
start "" "http://localhost:8000"
python -m http.server 8000 --directory "%~dp0build\site"

:end
