@echo off
setlocal enableextensions

rem ----------------------------------------------------------------------------
rem EnsureEngineBuilt.bat <Configuration> [Platform]
rem
rem Invoked as a prebuild step from external game projects. If the engine's
rem Runtime / Editor libraries for <Configuration>+<Platform> are missing
rem from %LUMINA_DIR%\Binaries\Windows64, this script transparently builds
rem the engine in the matching config so the game build can link.
rem
rem Args:
rem   %1   Configuration name: Debug | Development | Shipping
rem   %2   Platform name:      Editor | Game            (default: Editor)
rem
rem The engine's MSBuild solution defines two platforms (Workspace.lua:47):
rem   - Editor : full editor build, includes WITH_EDITOR + tool modules.
rem   - Game   : runtime-only, smaller surface, suitable for packaged games.
rem A packaged-game project building against Platform=Game needs Game libs
rem (not Editor), so the caller MUST pass its own platform through. We
rem default to Editor only for backwards compatibility with older prebuild
rem invocations that don't pass an arg.
rem ----------------------------------------------------------------------------

set "CONFIG=%~1"
if "%CONFIG%"=="" (
    echo [EnsureEngineBuilt] usage: EnsureEngineBuilt.bat ^<Configuration^> [Platform]
    exit /b 1
)

set "PLATFORM=%~2"
if "%PLATFORM%"=="" set "PLATFORM=Editor"
if /I not "%PLATFORM%"=="Editor" if /I not "%PLATFORM%"=="Game" (
    echo [EnsureEngineBuilt] Unknown platform "%PLATFORM%". Must be Editor or Game.
    exit /b 1
)

if not defined LUMINA_DIR (
    echo [EnsureEngineBuilt] LUMINA_DIR is not set. Run the engine's Setup.bat first.
    exit /b 1
)

set "ENGINE_BIN=%LUMINA_DIR%\Binaries\Windows64"
set "RUNTIME_LIB=%ENGINE_BIN%\Runtime-%CONFIG%.lib"

rem The Editor lib only exists in Editor-platform builds. Skip the check
rem under Platform=Game so we don't ping-pong rebuilding looking for it.
if /I "%PLATFORM%"=="Editor" (
    set "EDITOR_LIB=%ENGINE_BIN%\Editor-%CONFIG%.lib"
    if exist "%RUNTIME_LIB%" if exist "%EDITOR_LIB%" (
        exit /b 0
    )
) else (
    if exist "%RUNTIME_LIB%" (
        exit /b 0
    )
)

echo.
echo ===============================================================================
echo  [EnsureEngineBuilt] Engine libraries for "%CONFIG% ^| %PLATFORM%" are missing.
echo  Building Lumina.sln -- one-time cost; subsequent builds reuse the cached libs.
echo ===============================================================================
echo.

rem Locate MSBuild via vswhere. -latest -prerelease finds any VS2022 install
rem regardless of edition (Community/Pro/Enterprise/BuildTools).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [EnsureEngineBuilt] vswhere.exe not found. Install Visual Studio 2022 or build the engine manually.
    exit /b 1
)

rem Capture vswhere's stderr alongside stdout so a real failure (no MSBuild
rem component installed, install metadata corrupt) surfaces instead of
rem silently leaving MSBUILD undefined.
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2^>nul`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [EnsureEngineBuilt] MSBuild.exe not found via vswhere. Install the Visual Studio 2022 "Desktop development with C++" workload.
    exit /b 1
)

if not exist "%LUMINA_DIR%\Lumina.sln" (
    echo [EnsureEngineBuilt] %LUMINA_DIR%\Lumina.sln not found. Run GenerateProject.bat first.
    exit /b 1
)

"%MSBUILD%" "%LUMINA_DIR%\Lumina.sln" -p:Configuration=%CONFIG% -p:Platform=%PLATFORM% -m -v:minimal -nologo
if errorlevel 1 (
    echo.
    echo [EnsureEngineBuilt] Engine build failed for "%CONFIG% ^| %PLATFORM%". Resolve the errors above and retry.
    exit /b 1
)

echo.
echo [EnsureEngineBuilt] Engine "%CONFIG% ^| %PLATFORM%" build complete.
endlocal
exit /b 0
