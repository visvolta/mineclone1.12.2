@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "CMAKE_EXE="
set "VS_ROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "delims=" %%I in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_ROOT=%%I"
)
if not defined CMAKE_EXE if defined VS_ROOT if exist "%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_EXE=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if not defined CMAKE_EXE (
    echo.
    echo ERROR: CMake was not found.
    echo Install "C++ CMake tools for Windows" from Visual Studio Installer,
    echo or run: winget install --id Kitware.CMake -e
    echo.
    pause
    exit /b 1
)
if not exist "1.12.2.jar" (
    echo.
    echo ERROR: 1.12.2.jar was not found beside CMakeLists.txt.
    echo Place your legitimate Minecraft 1.12.2 client JAR in this project root.
    echo.
    pause
    exit /b 1
)
for %%I in ("%CMAKE_EXE%") do set "CTEST_EXE=%%~dpIctest.exe"
if not exist "%CTEST_EXE%" (
    echo.
    echo ERROR: ctest.exe was not found beside CMake.
    echo.
    pause
    exit /b 1
)

echo Using CMake: %CMAKE_EXE%
echo Using build directory: %CD%\build
echo.
rem CMake 4.x no longer supports some policy levels used by older third-party
rem projects. The project handles GLM as header-only, and this cache setting keeps
rem any remaining legacy FetchContent projects on a safe modern policy floor.
"%CMAKE_EXE%" -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DBLOCKCRAFT_REQUIRE_MINECRAFT_ASSETS=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.10
if errorlevel 1 (
    echo.
    echo Initial configure failed. Clearing stale FetchContent sub-build state and retrying once...
    if exist "build\_deps\glm-subbuild" rmdir /s /q "build\_deps\glm-subbuild"
    if exist "build\_deps\glm-build" rmdir /s /q "build\_deps\glm-build"
    if exist "build\_deps\glm-src" rmdir /s /q "build\_deps\glm-src"
    "%CMAKE_EXE%" -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DBLOCKCRAFT_REQUIRE_MINECRAFT_ASSETS=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.10
    if errorlevel 1 goto :failed
)
"%CMAKE_EXE%" --build build --config Release --target blockcraft blockcraft_tests blockcraft_registry_tests blockcraft_foundation_tests blockcraft_rendering_parity_tests blockcraft_item_inventory_tests blockcraft_placement_rules_tests blockcraft_block_entity_tests blockcraft_save_format_tests blockcraft_survival_tests --parallel --clean-first
if errorlevel 1 goto :failed
"%CTEST_EXE%" --test-dir build -C Release --output-on-failure
if errorlevel 1 goto :failed
if not exist "build\Release\blockcraft.exe" (
    echo.
    echo ERROR: Build completed without producing build\Release\blockcraft.exe.
    goto :failed
)
echo.
echo Release build and tests completed successfully.
echo Game: %CD%\build\Release\blockcraft.exe
echo.
pause
exit /b 0

:failed
echo.
echo Release build failed. Review the error shown above.
echo.
pause
exit /b 1
