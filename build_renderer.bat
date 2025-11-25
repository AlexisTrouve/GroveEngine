@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: GroveEngine - BgfxRenderer Build Script for Windows
:: ============================================================================

echo.
echo ============================================
echo   GroveEngine - BgfxRenderer Builder
echo ============================================
echo.

:: Check if cmake is available
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake not found in PATH
    echo         Install CMake from https://cmake.org/download/
    echo         Or install via: winget install Kitware.CMake
    pause
    exit /b 1
)

:: Set build directory
set BUILD_DIR=build-win

:: Parse arguments
set CONFIG=Release
set CLEAN=0
set OPEN_VS=0

:parse_args
if "%~1"=="" goto end_parse
if /i "%~1"=="debug" set CONFIG=Debug
if /i "%~1"=="release" set CONFIG=Release
if /i "%~1"=="clean" set CLEAN=1
if /i "%~1"=="vs" set OPEN_VS=1
if /i "%~1"=="--help" goto show_help
if /i "%~1"=="-h" goto show_help
shift
goto parse_args
:end_parse

:: Clean if requested
if %CLEAN%==1 (
    echo [INFO] Cleaning build directory...
    if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
)

:: Configure
echo [INFO] Configuring CMake (%CONFIG%)...
cmake -B %BUILD_DIR% -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_TESTS=OFF

if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed
    pause
    exit /b 1
)

:: Open VS if requested
if %OPEN_VS%==1 (
    echo [INFO] Opening Visual Studio...
    start "" "%BUILD_DIR%\GroveEngine.sln"
    exit /b 0
)

:: Build
echo.
echo [INFO] Building BgfxRenderer (%CONFIG%)...
cmake --build %BUILD_DIR% --config %CONFIG% --target BgfxRenderer -j

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed
    pause
    exit /b 1
)

:: Success
echo.
echo ============================================
echo   Build successful!
echo ============================================
echo.
echo   Output: %BUILD_DIR%\modules\%CONFIG%\libBgfxRenderer.dll
echo.
echo   Usage:
echo     build_renderer.bat           - Build Release
echo     build_renderer.bat debug     - Build Debug
echo     build_renderer.bat clean     - Clean and rebuild
echo     build_renderer.bat vs        - Open in Visual Studio
echo.

exit /b 0

:show_help
echo.
echo Usage: build_renderer.bat [options]
echo.
echo Options:
echo   debug     Build in Debug mode
echo   release   Build in Release mode (default)
echo   clean     Clean build directory before building
echo   vs        Generate and open Visual Studio solution
echo   --help    Show this help
echo.
echo Examples:
echo   build_renderer.bat
echo   build_renderer.bat debug
echo   build_renderer.bat clean release
echo   build_renderer.bat vs
echo.
exit /b 0
