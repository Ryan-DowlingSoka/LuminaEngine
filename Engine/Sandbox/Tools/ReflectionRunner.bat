@echo off
setlocal

rem Premake prebuild step: regenerate reflection code for this project.
rem Runs from the project root so premake picks up the project's premake5.lua.
cd /d "%~dp0.."

if not defined LUMINA_DIR (
    echo Reflection: LUMINA_DIR is not set. Run the engine's Setup.bat first.
    exit /b 1
)

set "PREMAKE=%LUMINA_DIR%\Tools\premake5.exe"

if not exist "%PREMAKE%" (
    echo Reflection: premake5.exe not found at "%PREMAKE%".
    exit /b 1
)

"%PREMAKE%" Reflection
if %errorlevel% neq 0 (
    echo Reflection generation failed!
    exit /b %errorlevel%
)
