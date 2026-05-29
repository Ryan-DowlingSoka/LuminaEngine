@echo off
setlocal enableextensions

rem ----------------------------------------------------------------------------
rem EnsureEngineBuilt.bat <Configuration>
rem
rem Invoked as a prebuild step from external game projects. If the engine's
rem Runtime / Editor libraries for <Configuration> are missing from
rem %LUMINA_DIR%\Binaries\Windows64, this script transparently builds the
rem engine in the matching config so the game build can link.
rem
rem Args:
rem   %1   Configuration name: Debug | Development | Shipping
rem ----------------------------------------------------------------------------

set "CONFIG=%~1"
if "%CONFIG%"=="" (
    echo [EnsureEngineBuilt] usage: EnsureEngineBuilt.bat ^<Configuration^>
    exit /b 1
)

if not defined LUMINA_DIR (
    echo [EnsureEngineBuilt] LUMINA_DIR is not set. Run the engine's Setup.bat first.
    exit /b 1
)

set "ENGINE_BIN=%LUMINA_DIR%\Binaries\Windows64"
set "RUNTIME_LIB=%ENGINE_BIN%\Runtime-%CONFIG%.lib"
set "EDITOR_LIB=%ENGINE_BIN%\Editor-%CONFIG%.lib"

if exist "%RUNTIME_LIB%" if exist "%EDITOR_LIB%" (
    rem Engine libs already exist for this configuration. Nothing to do.
    exit /b 0
)

echo.
echo ===============================================================================
echo  [EnsureEngineBuilt] Engine libraries for "%CONFIG%" are missing.
echo  Building Lumina.sln (%CONFIG% ^| Editor) -- one-time cost; subsequent builds
echo  will reuse the cached libraries.
echo ===============================================================================
echo.

rem Locate MSBuild via vswhere. -latest -prerelease finds any VS2022 install
rem regardless of edition (Community/Pro/Enterprise/BuildTools).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [EnsureEngineBuilt] vswhere.exe not found. Install Visual Studio 2022 or build the engine manually.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [EnsureEngineBuilt] MSBuild.exe not found via vswhere. Install the Visual Studio C++ workload.
    exit /b 1
)

if not exist "%LUMINA_DIR%\Lumina.sln" (
    echo [EnsureEngineBuilt] %LUMINA_DIR%\Lumina.sln not found. Has the engine been generated?
    exit /b 1
)

"%MSBUILD%" "%LUMINA_DIR%\Lumina.sln" -p:Configuration=%CONFIG% -p:Platform=Editor -m -v:minimal -nologo
if errorlevel 1 (
    echo.
    echo [EnsureEngineBuilt] Engine build failed for "%CONFIG%". Resolve the errors above and retry.
    exit /b 1
)

echo.
echo [EnsureEngineBuilt] Engine "%CONFIG%" build complete.
exit /b 0
