@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
set "CTEST_EXE=C:\Program Files\CMake\bin\ctest.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
if not exist "%CTEST_EXE%" set "CTEST_EXE=ctest"

where git >nul 2>nul
if errorlevel 1 (
    echo ERROR: Git is required to fetch the pinned open-source build dependencies.
    exit /b 1
)

set "GENERATOR=Visual Studio 17 2022"
set "BUILD_DIR=build"
set "DEPS_DIR=.deps"
set "CONFIG=Release"

echo Using CMake: %CMAKE_EXE%
echo Using generator: %GENERATOR%
echo Using build directory: %CD%\%BUILD_DIR%
echo Dependency cache: %CD%\%DEPS_DIR%
echo.

set /a ATTEMPT=1
:configure
"%CMAKE_EXE%" -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A x64 ^
    -DBUILD_TESTING=ON ^
    -DBLOCKCRAFT_REQUIRE_MINECRAFT_ASSETS=ON ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.10

if not errorlevel 1 goto configured
if %ATTEMPT% GEQ 4 goto failed

echo.
echo Configure attempt %ATTEMPT% failed. Preserving valid %DEPS_DIR% source checkouts and clearing transient build state...
for %%D in (glm glfw glad stb zlib imgui) do (
    if exist "%DEPS_DIR%\%%D-subbuild" rmdir /s /q "%DEPS_DIR%\%%D-subbuild"
    if exist "%DEPS_DIR%\%%D-build" rmdir /s /q "%DEPS_DIR%\%%D-build"
)
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
set /a ATTEMPT+=1
timeout /t 3 /nobreak >nul
goto configure

:configured
echo.
echo Configuration succeeded. Building %CONFIG%...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config %CONFIG% --clean-first --parallel --target ^
    blockcraft ^
    blockcraft_foundation_tests ^
    blockcraft_gameplay_tests ^
    blockcraft_persistence_tests ^
    blockcraft_rendering_model_tests
if errorlevel 1 goto failed

echo.
echo Running tests...
"%CTEST_EXE%" --test-dir "%BUILD_DIR%" -C %CONFIG% --output-on-failure
if errorlevel 1 goto failed

echo.
echo Release build and tests completed successfully.
echo Executable: %CD%\%BUILD_DIR%\%CONFIG%\blockcraft.exe
exit /b 0

:failed
echo.
echo Release build failed. Review the first error shown above.
exit /b 1
