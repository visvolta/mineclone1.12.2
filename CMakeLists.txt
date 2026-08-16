cmake_minimum_required(VERSION 3.24)

project(Blockcraft VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# Visual Studio's stock Debug flags use /Od and /RTC1. Runtime checks cannot
# coexist with optimization, so remove only /RTC1 and apply /O2 per project
# target below. Debug symbols, the debug CRT, and C++ assertions remain active.
if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG f58f558c120e9b32c217290b80bad1a0729fbb2c
)

FetchContent_MakeAvailable(glfw glm glad stb)
glad_add_library(glad_gl_core_33 STATIC REPRODUCIBLE API gl:core=3.3)

add_library(cubiomes STATIC
    third_party/cubiomes/blockcraft_bridge.c
    third_party/cubiomes/biomes.c
    third_party/cubiomes/layers.c
    third_party/cubiomes/generator.c
    third_party/cubiomes/noise.c
    third_party/cubiomes/biomenoise.c
)
target_include_directories(cubiomes PUBLIC third_party/cubiomes)
if(MSVC)
    target_compile_options(cubiomes PRIVATE $<$<CONFIG:Debug>:/O2>)
endif()

set(BLOCKCRAFT_MINECRAFT_JAR "" CACHE FILEPATH "Path to a legitimate Minecraft 1.12.2 client JAR")
set(BLOCKCRAFT_MINECRAFT_ASSETS_DIR "" CACHE PATH
    "Optional path to the Minecraft launcher's assets directory; used to materialize launcher-managed resources such as sounds")
set(BLOCKCRAFT_MINECRAFT_ASSET_INDEX "" CACHE FILEPATH
    "Optional path to the Minecraft 1.12.2 launcher asset index JSON")

if(NOT BLOCKCRAFT_MINECRAFT_JAR)
    set(BLOCKCRAFT_MINECRAFT_JAR "${CMAKE_CURRENT_SOURCE_DIR}/1.12.2.jar")
endif()

if(NOT EXISTS "${BLOCKCRAFT_MINECRAFT_JAR}")
    message(FATAL_ERROR
        "Minecraft 1.12.2 assets are required. Place a legitimate 1.12.2.jar beside "
        "CMakeLists.txt or configure BLOCKCRAFT_MINECRAFT_JAR with its path."
    )
endif()

# Auto-detect the standard launcher asset store. The cache variables above can
# still override these paths for Prism Launcher, MultiMC, portable installs, or
# any other non-standard Minecraft directory.
set(blockcraft_minecraft_home "")
if(WIN32 AND DEFINED ENV{APPDATA})
    file(TO_CMAKE_PATH "$ENV{APPDATA}/.minecraft" blockcraft_minecraft_home)
elseif(DEFINED ENV{HOME})
    file(TO_CMAKE_PATH "$ENV{HOME}/.minecraft" blockcraft_minecraft_home)
endif()

set(blockcraft_launcher_assets_dir "${BLOCKCRAFT_MINECRAFT_ASSETS_DIR}")
if(NOT blockcraft_launcher_assets_dir AND
   blockcraft_minecraft_home AND
   EXISTS "${blockcraft_minecraft_home}/assets")
    set(blockcraft_launcher_assets_dir "${blockcraft_minecraft_home}/assets")
endif()

set(blockcraft_asset_index_file "${BLOCKCRAFT_MINECRAFT_ASSET_INDEX}")
if(NOT blockcraft_asset_index_file AND blockcraft_launcher_assets_dir)
    # Prefer the exact asset-index id recorded by the local 1.12.2 launcher
    # version metadata when it is available.
    set(blockcraft_version_json "${blockcraft_minecraft_home}/versions/1.12.2/1.12.2.json")
    if(EXISTS "${blockcraft_version_json}")
        file(READ "${blockcraft_version_json}" blockcraft_version_metadata)
        string(JSON blockcraft_asset_index_id ERROR_VARIABLE blockcraft_asset_index_error
            GET "${blockcraft_version_metadata}" assetIndex id)
        if(blockcraft_asset_index_error STREQUAL "NOTFOUND")
            set(blockcraft_index_candidate
                "${blockcraft_launcher_assets_dir}/indexes/${blockcraft_asset_index_id}.json")
            if(EXISTS "${blockcraft_index_candidate}")
                set(blockcraft_asset_index_file "${blockcraft_index_candidate}")
            endif()
        endif()
    endif()

    # Standard 1.12.2 installations use the 1.12 asset index. Keep a second
    # candidate for launchers that store a version-named copy.
    if(NOT blockcraft_asset_index_file)
        foreach(blockcraft_index_name IN ITEMS 1.12 1.12.2)
            set(blockcraft_index_candidate
                "${blockcraft_launcher_assets_dir}/indexes/${blockcraft_index_name}.json")
            if(EXISTS "${blockcraft_index_candidate}")
                set(blockcraft_asset_index_file "${blockcraft_index_candidate}")
                break()
            endif()
        endforeach()
    endif()
endif()

set(BLOCKCRAFT_ASSET_ROOT "${CMAKE_BINARY_DIR}/minecraft-assets")
message(STATUS "Using Minecraft 1.12.2 client JAR: ${BLOCKCRAFT_MINECRAFT_JAR}")
if(blockcraft_asset_index_file)
    message(STATUS "Using Minecraft launcher asset index: ${blockcraft_asset_index_file}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DMC_JAR=${BLOCKCRAFT_MINECRAFT_JAR}"
        "-DOUTPUT_DIR=${BLOCKCRAFT_ASSET_ROOT}"
        "-DLAUNCHER_ASSETS_DIR=${blockcraft_launcher_assets_dir}"
        "-DASSET_INDEX_FILE=${blockcraft_asset_index_file}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractAssets.cmake"
    RESULT_VARIABLE asset_result
)
if(NOT asset_result EQUAL 0)
    message(FATAL_ERROR "Could not extract the complete Minecraft 1.12.2 asset set.")
endif()

set(BLOCKCRAFT_CORE_SOURCES
    src/core/Json.cpp
    src/core/ThreadPool.cpp
    src/worldgen/ChunkGeneratorSettings.cpp
    src/worldgen/FlatGeneratorSettings.cpp
    src/worldgen/StructureTemplate.cpp
    src/environment/Environment.cpp
    src/blocks/BlockRegistry.cpp
    src/lighting/LightSolver.cpp
    src/lighting/LightingEngine.cpp
    src/player/Player.cpp
    src/world/Chunk.cpp
    src/world/World.cpp
    src/rendering/ChunkMesher.cpp
    src/rendering/TextureAtlasData.cpp
    src/rendering/ModelLoader.cpp
    src/rendering/BlockStateModelMap.cpp
    src/rendering/CloudGeometry.cpp
    src/rendering/ImageLoader.cpp
    src/rendering/BiomeColors.cpp
    src/worldgen/JavaRandom.cpp
    src/worldgen/WorldConfig.cpp
    src/worldgen/BiomeProvider.cpp
    src/worldgen/CaveGenerator.cpp
    src/worldgen/ChunkStreamer.cpp
    src/worldgen/TerrainGenerator.cpp
    src/worldgen/WorldPopulator.cpp
    src/worldgen/StructureGenerator.cpp
)

add_executable(blockcraft
    src/main.cpp
    src/rendering/Camera.cpp
    src/player/BlockInteraction.cpp
    src/rendering/Shader.cpp
    src/rendering/DebugRenderer.cpp
    src/rendering/EnvironmentRenderer.cpp
    src/rendering/TextureAtlas.cpp
    src/rendering/WorldRenderer.cpp
    src/world/Raycast.cpp
    ${BLOCKCRAFT_CORE_SOURCES}
)

target_include_directories(blockcraft PRIVATE src ${stb_SOURCE_DIR})
target_compile_definitions(blockcraft PRIVATE
    BLOCKCRAFT_ASSET_ROOT="${BLOCKCRAFT_ASSET_ROOT}"
    BLOCKCRAFT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/world.cfg"
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:BLOCKCRAFT_OPTIMIZED_DEBUG=1>
)
configure_file(world.cfg world.cfg COPYONLY)
target_link_libraries(blockcraft PRIVATE glfw glad_gl_core_33 glm::glm cubiomes Threads::Threads ZLIB::ZLIB)

if(MSVC)
    target_compile_options(blockcraft PRIVATE /W4 /permissive- $<$<CONFIG:Debug>:/O2>)
else()
    target_compile_options(blockcraft PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(blockcraft_tests tests/CoreTests.cpp ${BLOCKCRAFT_CORE_SOURCES})
    target_include_directories(blockcraft_tests PRIVATE src ${stb_SOURCE_DIR})
    target_compile_definitions(blockcraft_tests PRIVATE
        BLOCKCRAFT_ASSET_ROOT="${BLOCKCRAFT_ASSET_ROOT}"
    )
    target_link_libraries(blockcraft_tests PRIVATE glm::glm cubiomes Threads::Threads ZLIB::ZLIB)
    if(MSVC)
        target_compile_options(blockcraft_tests PRIVATE $<$<CONFIG:Debug>:/O2> /UNDEBUG)
    else()
        target_compile_options(blockcraft_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME blockcraft_core COMMAND blockcraft_tests)

    # Stage 1 source-parity guard: this target is deliberately independent of
    # rendering/worldgen so the complete 1.12.2 numeric registry can be checked
    # directly, including the 253/254 gaps and legacy state packing.
    add_executable(blockcraft_registry_tests
        tests/BlockRegistryParityTests.cpp
        src/blocks/BlockRegistry.cpp
    )
    target_include_directories(blockcraft_registry_tests PRIVATE src)
    if(MSVC)
        target_compile_options(blockcraft_registry_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(blockcraft_registry_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME blockcraft_registry COMMAND blockcraft_registry_tests)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT blockcraft)
