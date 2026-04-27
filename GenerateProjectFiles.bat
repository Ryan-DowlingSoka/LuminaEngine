@echo off
setlocal enableextensions

rem ============================================================================
rem  Lumina Engine - Regenerate Project Files
rem
rem  Run this whenever you add/remove source files or change premake5.lua.
rem  For first-time setup (downloads External, configures hooks), use Setup.bat.
rem ============================================================================

cd /d "%~dp0"
set "LUMINA_DIR=%CD%"

set "PREMAKE_EXE=%LUMINA_DIR%\Tools\premake5.exe"

if not exist "%PREMAKE_EXE%" (
    echo premake5.exe not found at %PREMAKE_EXE%.
    echo Run Setup.bat first.
    endlocal
    exit /b 1
)

"%PREMAKE_EXE%" vs2022
if errorlevel 1 (
    echo.
    echo Project generation failed.
    endlocal
    exit /b 1
)

echo.
echo Solution generated. Open Lumina.sln in Visual Studio 2022.
endlocal
exit /b 0
