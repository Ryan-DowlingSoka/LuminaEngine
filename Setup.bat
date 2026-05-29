@echo off
setlocal enableextensions

rem ============================================================================
rem  Lumina Engine - Setup
rem
rem  One-shot setup for a fresh clone:
rem    1. Verifies (and bootstraps if missing) Tools\premake5.exe
rem    2. Runs `premake5 setup` -- downloads External, configures git hooks
rem    3. Runs `premake5 vs2022` -- generates Lumina.sln
rem
rem  No Python required. Uses curl.exe and tar.exe (bundled with Windows 10
rem  1803+). For .7z dependency archives, drop 7zr.exe into Tools\ first.
rem ============================================================================

cd /d "%~dp0"
set "LUMINA_DIR=%CD%"

set "PREMAKE_DIR=%LUMINA_DIR%\Tools"
set "PREMAKE_EXE=%PREMAKE_DIR%\premake5.exe"
set "PREMAKE_VERSION=5.0.0-beta2"
set "PREMAKE_URL=https://github.com/premake/premake-core/releases/download/v%PREMAKE_VERSION%/premake-%PREMAKE_VERSION%-windows.zip"

echo.
echo ============================================================
echo  LUMINA ENGINE SETUP
echo ============================================================
echo  Working directory: %LUMINA_DIR%
echo.

rem --- Detect an existing LUMINA_DIR that points elsewhere ------------------
rem A stale value from a previous install is the #1 source of "missing
rem reflection symbol" / "wrong engine version linked" reports. Read the
rem persisted user-env value (not %LUMINA_DIR%, which is the in-process
rem variable we just set on the line above) and surface the migration.
set "PRIOR_LUMINA_DIR="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v LUMINA_DIR 2^>nul ^| findstr /R "LUMINA_DIR"') do (
    set "PRIOR_LUMINA_DIR=%%B"
)
if defined PRIOR_LUMINA_DIR (
    if /I not "%PRIOR_LUMINA_DIR%"=="%LUMINA_DIR%" (
        echo [setup] NOTE: LUMINA_DIR was previously set to:
        echo            %PRIOR_LUMINA_DIR%
        echo        It will be overwritten with:
        echo            %LUMINA_DIR%
        echo        Tooling that runs outside this shell ^(VS, Rider, taskbar^) keeps the
        echo        old value until it is restarted.
        echo.
    )
)

rem --- Bootstrap premake5 if missing ----------------------------------------
if not exist "%PREMAKE_EXE%" (
    echo [bootstrap] premake5.exe not found, downloading %PREMAKE_VERSION%...
    if not exist "%PREMAKE_DIR%" mkdir "%PREMAKE_DIR%"

    where curl.exe >nul 2>&1
    if errorlevel 1 (
        echo [bootstrap] ERROR: curl.exe is not on PATH. Windows 10 1803+ required.
        goto :fail
    )
    where tar.exe >nul 2>&1
    if errorlevel 1 (
        echo [bootstrap] ERROR: tar.exe is not on PATH. Windows 10 1803+ required.
        goto :fail
    )

    curl.exe --fail --location --silent --show-error --output "%PREMAKE_DIR%\premake.zip" "%PREMAKE_URL%"
    if errorlevel 1 (
        echo [bootstrap] ERROR: failed to download premake5.
        goto :fail
    )

    tar.exe -xf "%PREMAKE_DIR%\premake.zip" -C "%PREMAKE_DIR%"
    if errorlevel 1 (
        echo [bootstrap] ERROR: failed to extract premake5.zip.
        goto :fail
    )

    del /q "%PREMAKE_DIR%\premake.zip" >nul 2>&1

    if not exist "%PREMAKE_EXE%" (
        echo [bootstrap] ERROR: premake5.exe still missing after extraction.
        goto :fail
    )
    echo [bootstrap] premake5 ready.
    echo.
)

rem --- Run setup action ------------------------------------------------------
"%PREMAKE_EXE%" setup %*
if errorlevel 1 goto :fail

rem --- Verify setx actually persisted LUMINA_DIR ----------------------------
rem setx silently truncates at 1024 chars and can fail under permission-
rem locked profiles; downstream tools then break with cryptic "engine
rem binaries not found" errors. Read the persisted value back and confirm
rem before claiming success.
set "PERSISTED_LUMINA_DIR="
for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v LUMINA_DIR 2^>nul ^| findstr /R "LUMINA_DIR"') do (
    set "PERSISTED_LUMINA_DIR=%%B"
)
if not defined PERSISTED_LUMINA_DIR (
    echo [setup] ERROR: LUMINA_DIR was not persisted to the user environment.
    echo         The current shell still sees it ^(set by Setup.bat^), but a fresh
    echo         shell will not. Possible causes: roaming-profile restrictions,
    echo         setx hit its 1024-char limit, or HKCU\Environment is locked down.
    goto :fail
)
if /I not "%PERSISTED_LUMINA_DIR%"=="%LUMINA_DIR%" (
    echo [setup] ERROR: LUMINA_DIR persisted with the wrong value.
    echo         Expected: %LUMINA_DIR%
    echo         Got:      %PERSISTED_LUMINA_DIR%
    goto :fail
)

rem --- Generate Visual Studio solution ---------------------------------------
echo.
echo ============================================================
echo  Generating Visual Studio 2022 solution
echo ============================================================
"%PREMAKE_EXE%" vs2022
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  ALL DONE - open Lumina.sln in Visual Studio 2022
echo ============================================================
echo  LUMINA_DIR persisted: %PERSISTED_LUMINA_DIR%
echo  Note: already-running Visual Studio / Rider instances must be
echo  restarted to pick up the new LUMINA_DIR.
echo.
endlocal
exit /b 0

:fail
echo.
echo Setup failed. See messages above.
echo.
endlocal
exit /b 1
