@echo off
setlocal EnableDelayedExpansion

set "MODE=debug"
set "FORCE_RECONFIGURE=0"
set "USE_CONSOLE=0"
set "BUILD_SYSTEM=ninja"
set "APP_ARGS="

:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="debug" (
    set "MODE=debug"
    shift
    goto :parse_args
)
if /i "%~1"=="release" (
    set "MODE=release"
    shift
    goto :parse_args
)
if /i "%~1"=="--reconfigure" (
    set "FORCE_RECONFIGURE=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--vs" (
    set "BUILD_SYSTEM=vs"
    shift
    goto :parse_args
)
if /i "%~1"=="--ninja" (
    set "BUILD_SYSTEM=ninja"
    shift
    goto :parse_args
)
if /i "%~1"=="--console" (
    set "USE_CONSOLE=1"
)
set "APP_ARGS=!APP_ARGS! "%~1""
shift
goto :parse_args

:args_done
set "CONFIG=Debug"
set "PRESET=win-ninja-debug"
set "BUILD_DIR=build-ninja"
if /i "%MODE%"=="release" (
    set "CONFIG=Release"
    set "PRESET=win-ninja-release"
)
if /i "%BUILD_SYSTEM%"=="vs" (
    set "BUILD_DIR=build"
    set "PRESET=default"
    if /i "%MODE%"=="release" (
        set "PRESET=release"
    )
)

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
if exist "%SCRIPT_DIR%\CMakePresets.json" (
    set "ROOT=%SCRIPT_DIR%"
) else (
    for %%I in ("%SCRIPT_DIR%\..") do set "ROOT=%%~fI"
)
pushd "%ROOT%" >nul || exit /b 1

set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
set "EXE=%BUILD_DIR%\%CONFIG%\draxul.exe"

echo.
echo === %CONFIG% / %BUILD_SYSTEM% ===
if /i "%BUILD_SYSTEM%"=="ninja" (
    call :ensure_msvc_env || goto :fail
)
if "%FORCE_RECONFIGURE%"=="1" (
    call :run cmake --preset %PRESET% || goto :fail
) else if not exist "%CACHE_FILE%" (
    call :run cmake --preset %PRESET% || goto :fail
) else (
    echo.
    echo ^> using existing CMake cache: %CACHE_FILE%
)

call :run cmake --build %BUILD_DIR% --config %CONFIG% --target draxul --parallel || goto :fail

if not exist "%EXE%" (
    echo.
    echo Missing executable: %EXE%
    goto :fail
)

echo.
echo ^> %EXE%%APP_ARGS%
if "%USE_CONSOLE%"=="1" (
    call "%EXE%"%APP_ARGS%
) else (
    start "" /wait "%EXE%"%APP_ARGS%
)
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:run
echo.
echo ^> %*
%*
exit /b %errorlevel%

:ensure_msvc_env
where cl >nul 2>nul
if not errorlevel 1 exit /b 0

for %%P in (
    "C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Preview\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
) do (
    if exist "%%~P" (
        echo.
        echo ^> call "%%~P" -arch=x64 -host_arch=x64
        call "%%~P" -arch=x64 -host_arch=x64 >nul
        where cl >nul 2>nul
        if not errorlevel 1 exit /b 0
    )
)

echo.
echo Failed to initialize the MSVC toolchain for Ninja builds.
echo Use --vs to fall back to the Visual Studio generator.
exit /b 1

:fail
set "STATUS=%errorlevel%"
if "%STATUS%"=="" set "STATUS=1"
popd >nul
exit /b %STATUS%
