@echo off
setlocal EnableDelayedExpansion

set "MODE=debug"
set "FORCE_RECONFIGURE=0"
set "VERBOSE=0"
set "UNIT_ONLY=0"

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
if /i "%~1"=="both" (
    set "MODE=both"
    shift
    goto :parse_args
)
if /i "%~1"=="--reconfigure" (
    set "FORCE_RECONFIGURE=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set "VERBOSE=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--unit" (
    set "UNIT_ONLY=1"
    shift
    goto :parse_args
)
echo Usage: %~nx0 [debug^|release^|both] [--reconfigure] [--verbose] [--unit]
exit /b 2

:args_done

if "%UNIT_ONLY%"=="1" if /i "%MODE%"=="both" (
    echo --unit supports one configuration at a time; choose debug or release
    exit /b 2
)

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
if exist "%SCRIPT_DIR%\CMakePresets.json" (
    set "ROOT=%SCRIPT_DIR%"
) else (
    for %%I in ("%SCRIPT_DIR%\..") do set "ROOT=%%~fI"
)
pushd "%ROOT%" >nul || exit /b 1

if /i "%MODE%"=="release" goto :run_release_only
if /i "%MODE%"=="both" goto :run_both

call :run_config Debug || goto :fail
goto :success

:run_release_only
call :run_config Release || goto :fail
goto :success

:run_both
call :run_config Debug || goto :fail
call :run_config Release || goto :fail
goto :success

:run_config
set "CONFIG=%~1"
set "PRESET=default"
if /i "%CONFIG%"=="Release" set "PRESET=release"
set "CACHE_FILE=build\CMakeCache.txt"
set "BUILD_LOG_ARGS=/v:minimal /nologo"
if "%VERBOSE%"=="1" set "BUILD_LOG_ARGS=/v:normal /nologo"
set "NEED_CONFIGURE=0"
if "%FORCE_RECONFIGURE%"=="1" set "NEED_CONFIGURE=1"
if not exist "%CACHE_FILE%" set "NEED_CONFIGURE=1"
if exist "%CACHE_FILE%" (
    call :cache_is_standard
    if errorlevel 1 set "NEED_CONFIGURE=1"
)
echo.
echo === %CONFIG% ===
if "!NEED_CONFIGURE!"=="1" (
    call :run cmake --preset %PRESET%
    if errorlevel 1 exit /b !errorlevel!
) else (
    echo.
    echo ^> using existing CMake cache: %CACHE_FILE%
)
if "%UNIT_ONLY%"=="1" (
    call :run cmake --build build --config %CONFIG% --target draxul-tests --parallel -- %BUILD_LOG_ARGS%
    if errorlevel 1 exit /b !errorlevel!
    if "%VERBOSE%"=="1" (
        call :run ctest --test-dir build --build-config %CONFIG% --label-regex unit --parallel 4 --verbose --timeout 120
    ) else (
        call :run ctest --test-dir build --build-config %CONFIG% --label-regex unit --parallel 4 --output-on-failure --timeout 120
    )
    if errorlevel 1 exit /b !errorlevel!
    exit /b 0
)
call :run cmake --build build --config %CONFIG% --parallel -- %BUILD_LOG_ARGS%
if errorlevel 1 exit /b !errorlevel!
set "VK_LOADER_LAYERS_DISABLE=~implicit~"
if "%VERBOSE%"=="1" (
    call :run ctest --test-dir build --build-config %CONFIG% --verbose
) else (
    call :run ctest --test-dir build --build-config %CONFIG% --progress --output-on-failure
)
if errorlevel 1 exit /b !errorlevel!
exit /b 0

:cache_is_standard
findstr /b /c:"DRAXUL_ENABLE_SANITIZERS:BOOL=ON" "%CACHE_FILE%" >nul && exit /b 1
findstr /b /c:"DRAXUL_ENABLE_TSAN:BOOL=ON" "%CACHE_FILE%" >nul && exit /b 1
findstr /b /c:"DRAXUL_ENABLE_COVERAGE:BOOL=ON" "%CACHE_FILE%" >nul && exit /b 1
findstr /b /c:"DRAXUL_ENABLE_RENDER_TESTS:BOOL=ON" "%CACHE_FILE%" >nul || exit /b 1
exit /b 0

:run
echo.
echo ^> %*
%*
exit /b %errorlevel%

:success
echo.
echo All requested tests passed.
popd >nul
exit /b 0

:fail
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%
