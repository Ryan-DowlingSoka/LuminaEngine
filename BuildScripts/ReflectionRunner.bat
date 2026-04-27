@echo off
setlocal

rem Invoked as a prebuild step. Resolves the engine root via LUMINA_DIR if set,
rem otherwise walks up from this script's directory.

if defined LUMINA_DIR (
    set "ENGINE_ROOT=%LUMINA_DIR%"
) else (
    set "ENGINE_ROOT=%~dp0.."
)

set "PREMAKE=%ENGINE_ROOT%\Tools\premake5.exe"

if not exist "%PREMAKE%" (
    echo Reflection: premake5.exe not found at "%PREMAKE%".
    echo Run Setup.bat at the engine root first.
    exit /b 1
)

"%PREMAKE%" --file="%ENGINE_ROOT%\premake5.lua" Reflection
exit /b %errorlevel%
