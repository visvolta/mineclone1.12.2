# Mineclone 1.12.2

C++20/OpenGL recreation targeting Minecraft Java Edition 1.12.2 behavior.

## Windows build

Requirements: Visual Studio 2022 C++ workload, CMake 3.24+, Git, and an OpenGL 3.3-capable driver.

Keep a legitimate `1.12.2.jar` in the project root, then run:

```powershell
.\build-release.bat
```

The script uses only `build\`, performs a clean Release build, runs the core and registry tests, and produces:

```text
build\Release\blockcraft.exe
```

Manual equivalent:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --target blockcraft blockcraft_tests blockcraft_registry_tests --parallel --clean-first
ctest --test-dir build -C Release --output-on-failure
.\build\Release\blockcraft.exe
```

CMake extracts the required 1.12.2 textures and structure templates from the local client JAR into the build directory; generated files are not stored in the source tree.

## Source layout

- `src/blocks` — block IDs, legacy state packing, registry/runtime block data
- `src/core` — worker/thread infrastructure
- `src/environment` — day/night, weather, fog state
- `src/lighting` — skylight and block-light solving
- `src/player` — player physics and block interaction
- `src/rendering` — camera, shaders, meshing, textures, world/environment rendering
- `src/world` — chunk/world storage and raycasting
- `src/worldgen` — biome, terrain, caves, population, structures, streaming
- `tests` — always-on core and 1.12.2 registry parity tests

## Controls

W/A/S/D move, Space jump, Left Ctrl sprint, Left Shift sneak/descend, mouse look, left/right mouse break/place, 1-9 or wheel select blocks, G toggles Survival/Creative, F3 toggles wireframe, Escape exits.
