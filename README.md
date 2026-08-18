# Mineclone 1.12.2

C++20/OpenGL recreation targeting Minecraft Java Edition 1.12.2 behavior.

## Windows build

Requirements:

- Visual Studio 2022 with the Desktop development with C++ workload
- CMake 3.24+
- Git
- an OpenGL 3.3-capable driver
- a legitimate Minecraft Java Edition `1.12.2.jar` in the project root

Run:

```powershell
.\build-release.bat
```

The release script configures with the `Visual Studio 17 2022` generator, keeps fetched open-source dependencies in `.deps\`, builds the complete CMake `ALL` target, then runs every registered CTest suite. New test executables therefore do not need to be added manually to the batch file.

The executable is produced at:

```text
build\Release\blockcraft.exe
```

CMake extracts the required 1.12.2 resources from the local client JAR into the build directory. Minecraft assets are not stored in the repository.

## Asset-free CI / foundation tests

The project supports an asset-free configuration for CI and source-level foundation tests:

```powershell
cmake -S . -B build-ci -A x64 -DBUILD_TESTING=ON -DBLOCKCRAFT_REQUIRE_MINECRAFT_ASSETS=OFF
cmake --build build-ci --config Release --parallel
ctest --test-dir build-ci -C Release --output-on-failure
```

`.github/workflows/windows-ci.yml` runs this path on Windows without requiring a Minecraft JAR.

## Current test suites

The full asset-backed build currently registers focused suites for:

- player/survival foundation behavior
- gameplay/crafting parity
- persistence/NBT
- rendering models
- dynamic world ticking
- functional block entities
- generic entity world / chunk persistence

## Source layout

- `src/blocks` — legacy block IDs/states, shapes and placement rules
- `src/client` — front end and scaled client UI infrastructure
- `src/core` — JSON and worker/thread infrastructure
- `src/crafting` — 1.12.2 JSON recipe loading and matching
- `src/entity` — generic entity base, manager, movement types and entity NBT
- `src/environment` — total world time, daylight time, weather and fog state
- `src/items` — item registry and ItemStack data
- `src/lighting` — skylight and block-light solving
- `src/player` — player physics, survival state and block interaction
- `src/rendering` — camera, models, meshing, HUD, environment and entity/block-entity rendering
- `src/save` — NBT, Anvil region files, save migrations and world persistence
- `src/survival` — mining, food and furnace rules
- `src/world` — chunks, world storage, dynamic ticks, redstone, block entities and raycasting
- `src/worldgen` — biome, terrain, caves, population, structures and streaming
- `tests` — focused deterministic regression suites

## Stage 12.5 runtime ordering

A single simulation tick is intentionally ordered as:

1. player/input
2. block interaction
3. scheduled block updates
4. random block ticks
5. block entities
6. runtime entities
7. environment/time/weather
8. lighting propagation
9. renderer updates

This ordering is the baseline for the upcoming redstone stage.


## Stage 14 entity world

Runtime non-player objects are owned by `EntityManager`. Dropped items, falling blocks, primed TNT, XP orbs, arrows, boats and minecarts share identity, UUIDs, interpolation, chunk membership, ticking and Anvil `Entities` persistence. `ItemEntitySystem` remains only as a compatibility facade for older block/redstone call sites; it no longer owns dropped items.
