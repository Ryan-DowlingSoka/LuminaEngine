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

rem --- Advisory prerequisite check (non-blocking) ----------------------------
rem Regen doesn't need the toolchain, but warn early if the build won't compile
rem (missing .NET 10 SDK / VS < 18.0). Set SKIP_PREREQ_CHECKS=1 to silence.
if not defined SKIP_PREREQ_CHECKS (
    where powershell.exe >nul 2>&1
    if not errorlevel 1 (
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%LUMINA_DIR%\BuildScripts\CheckPrerequisites.ps1" -NonBlocking
    )
)

rem  Extra args pass straight through to premake, so feature toggles work here:
rem    GenerateProjectFiles.bat --tracy=off --validation=on --aftermath=on
rem  (persistent defaults live in BuildScripts\BuildConfig.lua)
"%PREMAKE_EXE%" vs2022 %*
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
