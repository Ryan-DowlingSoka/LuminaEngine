@echo off

REM Run from the game project root so premake picks up this project's premake5.lua
cd /d "%~dp0.."

set PREMAKE="%CD%\Tools\premake5.exe"

if not exist %PREMAKE% (
    echo Premake5 not found at %PREMAKE%
    exit /b 1
)

%PREMAKE% Reflection

if %errorlevel% neq 0 (
    echo Reflection generation failed!
    exit /b %errorlevel%
)
