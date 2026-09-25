@echo off
setlocal

set SCRIPT_DIR=%~dp0
set PATH=%SCRIPT_DIR%tools\w64devkit\bin;%SCRIPT_DIR%tools\cmake\bin;%PATH%

set DEBUG_FLAG=OFF
set BUILD_TYPE=Release

if "%~1"=="--debug" goto set_debug
if "%~1"=="-debug" goto set_debug
if "%~1"=="-d" goto set_debug

echo [BUILD] Configuring in PRODUCTION mode (lean, silent, high-performance)
echo [BUILD] Pass '--debug' to build with telemetry, logging, and diagnostics enabled.
goto run_cmake

:set_debug
set DEBUG_FLAG=ON
set BUILD_TYPE=Debug
echo [BUILD] Configuring in DEBUG mode (--debug enabled)

:run_cmake
cmake -B build_app -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DENABLE_DEBUG=%DEBUG_FLAG%
if errorlevel 1 goto error

cmake --build build_app
if errorlevel 1 goto error

echo.
echo [BUILD] Build successful! Game executables deployed to game/
goto end

:error
echo.
echo [BUILD] Build failed with error %errorlevel%
exit /b %errorlevel%

:end
