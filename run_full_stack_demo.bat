@echo off
REM Full Stack Interactive Demo - Build and Run Script
REM Requires: BgfxRenderer, UIModule, InputModule

echo ================================================
echo   GroveEngine - Full Stack Interactive Demo
echo ================================================
echo.

REM Check if build directory exists
if not exist build (
    echo Error: build directory not found!
    echo Run: cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_INPUT_MODULE=ON
    pause
    exit /b 1
)

echo Step 1: Building modules...
cmake --build build --target BgfxRenderer UIModule InputModule test_full_stack_interactive -j4

if %ERRORLEVEL% neq 0 (
    echo.
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Step 2: Copying DLLs...
if not exist build\tests\modules mkdir build\tests\modules
copy build\modules\*.dll build\tests\modules\ >nul 2>&1
copy C:\SDL2\bin\SDL2.dll build\tests\ >nul 2>&1

REM Create aliases for modules (remove lib prefix)
cd build\tests\modules
copy libBgfxRenderer.dll BgfxRenderer.dll >nul 2>&1
copy libUIModule.dll UIModule.dll >nul 2>&1
copy libInputModule.dll InputModule.dll >nul 2>&1
cd ..\..\..

echo.
echo Step 3: Running demo...
echo.
echo Controls:
echo   - Click "Spawn" button to spawn sprites
echo   - Click "Clear" button to remove all sprites
echo   - Drag slider to change spawn speed
echo   - Press SPACE to spawn sprite from keyboard
echo   - Press ESC to exit
echo.
echo ================================================
echo.

cd build\tests
test_full_stack_interactive.exe

cd ..\..
echo.
echo Demo exited.
pause
