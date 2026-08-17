cmake_minimum_required(VERSION 3.24)

project(Blockcraft VERSION 0.13.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
find_package(Threads REQUIRED)

option(BLOCKCRAFT_REQUIRE_MINECRAFT_ASSETS
    "Build the client and asset-dependent parity tests (requires a legitimate Minecraft 1.12.2 client JAR)" ON)

if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# glm is the only fetched dependency required by the asset-free foundation tests.
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm)

set(BLOCKCRAFT_FOUNDATION_SOURCES
    src/blocks/BlockRegistry.cpp
    src/blocks/BlockShape.cpp
    src/core/ThreadPool.cpp
    src/player/Player.cpp
    src/player/PlayerInventory.cpp
    src/world/Chunk.cpp
    src/world/World.cpp
    src/world/Raycast.cpp
)

include(CTest)
if(BUILD_TESTING)
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

    add_executable(blockcraft_foundation_tests
        tests/FoundationTests.cpp
        ${BLOCKCRAFT_FOUNDATION_SOURCES}
    )
    target_include_directories(blockcraft_foundation_tests PRIVATE src)
    target_link_libraries(blockcraft_foundation_tests PRIVATE glm::glm Threads::Threads)
    if(MSVC)
        target_compile_options(blockcraft_foundation_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(blockcraft_foundation_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME blockcraft_foundation COMMAND blockcraft_foundation_tests)
endif()

if(NOT BLOCKCRAFT_REQUIRE_MINECRAFT_ASSETS)
    message(STATUS "Minecraft assets disabled: building asset-free registry/foundation tests only.")
    return()
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
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
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
)
# Pin ImGui to a known backend/API revision; never build imgui_demo.cpp into the game.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw glad stb zlib imgui)
glad_add_library(glad_gl_core_33 STATIC REPRODUCIBLE API gl:core=3.3)

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_backend PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_backend PUBLIC glfw)
if(MSVC)
    target_compile_options(imgui_backend PRIVATE /W3)
else()
    target_compile_options(imgui_backend PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

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
        "Minecraft 1.12.2 assets are required for the client. Place a legitimate 1.12.2.jar beside "
        "CMakeLists.txt, configure BLOCKCRAFT_MINECRAFT_JAR, or configure "
        "-DBLOCKCRAFT_REQUIRE_MINECRAFT_ASSETS=OFF for the asset-free test suite."
    )
endif()

set(blockcraft_minecraft_home "")
if(WIN32 AND DEFINED ENV{APPDATA})
    file(TO_CMAKE_PATH "$ENV{APPDATA}/.minecraft" blockcraft_minecraft_home)
elseif(DEFINED ENV{HOME})
    file(TO_CMAKE_PATH "$ENV{HOME}/.minecraft" blockcraft_minecraft_home)
endif()

set(blockcraft_launcher_assets_dir "${BLOCKCRAFT_MINECRAFT_ASSETS_DIR}")
if(NOT blockcraft_launcher_assets_dir AND blockcraft_minecraft_home AND
   EXISTS "${blockcraft_minecraft_home}/assets")
    set(blockcraft_launcher_assets_dir "${blockcraft_minecraft_home}/assets")
endif()

set(blockcraft_asset_index_file "${BLOCKCRAFT_MINECRAFT_ASSET_INDEX}")
if(NOT blockcraft_asset_index_file AND blockcraft_launcher_assets_dir)
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
    src/blocks/BlockShape.cpp
    src/blocks/PlacementRules.cpp
    src/lighting/LightSolver.cpp
    src/lighting/LightingEngine.cpp
    src/player/Player.cpp
    src/player/PlayerInventory.cpp
    src/items/ItemRegistry.cpp
    src/world/Chunk.cpp
    src/world/World.cpp
    src/world/BlockEntitySystem.cpp
    src/save/Nbt.cpp
    src/save/RegionFile.cpp
    src/save/WorldSave.cpp
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
    src/client/FrontEnd.cpp
    src/rendering/Camera.cpp
    src/player/BlockInteraction.cpp
    src/rendering/Shader.cpp
    src/rendering/DebugRenderer.cpp
    src/rendering/BlockEntityRenderer.cpp
    src/rendering/EnvironmentRenderer.cpp
    src/rendering/GameHud.cpp
    src/rendering/TextureAtlas.cpp
    src/rendering/WorldRenderer.cpp
    src/world/Raycast.cpp
    ${BLOCKCRAFT_CORE_SOURCES}
)

target_include_directories(blockcraft PRIVATE src ${stb_SOURCE_DIR})
target_compile_definitions(blockcraft PRIVATE
    BLOCKCRAFT_ASSET_ROOT="${BLOCKCRAFT_ASSET_ROOT}"
    BLOCKCRAFT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/world.cfg"
    BLOCKCRAFT_SAVES_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/saves"
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:BLOCKCRAFT_OPTIMIZED_DEBUG=1>
)
configure_file(world.cfg world.cfg COPYONLY)
target_link_libraries(blockcraft PRIVATE
    glfw glad_gl_core_33 glm::glm cubiomes Threads::Threads zlibstatic imgui_backend)

if(MSVC)
    target_compile_options(blockcraft PRIVATE /W4 /permissive- $<$<CONFIG:Debug>:/O2>)
else()
    target_compile_options(blockcraft PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(blockcraft_tests tests/CoreTests.cpp ${BLOCKCRAFT_CORE_SOURCES})
    target_include_directories(blockcraft_tests PRIVATE src ${stb_SOURCE_DIR})
    target_compile_definitions(blockcraft_tests PRIVATE BLOCKCRAFT_ASSET_ROOT="${BLOCKCRAFT_ASSET_ROOT}")
    target_link_libraries(blockcraft_tests PRIVATE glm::glm cubiomes Threads::Threads zlibstatic)
    if(MSVC)
        target_compile_options(blockcraft_tests PRIVATE $<$<CONFIG:Debug>:/O2> /UNDEBUG)
    else()
        target_compile_options(blockcraft_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME blockcraft_core COMMAND blockcraft_tests)

    add_executable(blockcraft_rendering_parity_tests
        tests/RenderingParityTests.cpp
        src/blocks/BlockRegistry.cpp
        src/core/Json.cpp
        src/rendering/TextureAtlasData.cpp
        src/rendering/ModelLoader.cpp
        src/rendering/BlockStateModelMap.cpp
        src/rendering/ImageLoader.cpp
    )
    target_include_directories(blockcraft_rendering_parity_tests PRIVATE src ${stb_SOURCE_DIR})
    target_compile_definitions(blockcraft_rendering_parity_tests PRIVATE
        BLOCKCRAFT_ASSET_ROOT="${BLOCKCRAFT_ASSET_ROOT}")
    target_link_libraries(blockcraft_rendering_parity_tests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(blockcraft_rendering_parity_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(blockcraft_rendering_parity_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME blockcraft_rendering_parity COMMAND blockcraft_rendering_parity_tests)

    add_executable(blockcraft_item_inventory_tests
        tests/ItemInventoryTests.cpp
        src/blocks/BlockRegistry.cpp
        src/core/Json.cpp
        src/items/ItemRegistry.cpp
        src/player/PlayerInventory.cpp
    )
    target_include_directories(blockcraft_item_inventory_tests PRIVATE src)
    target_compile_definitions(blockcraft_item_inventory_tests PRIVATE
        BLOCKCRAFT_ASSET_ROOT="${BLOCKCRAFT_ASSET_ROOT}")
    if(MSVC)
        target_compile_options(blockcraft_item_inventory_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(blockcraft_item_inventory_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME blockcraft_item_inventory COMMAND blockcraft_item_inventory_tests)

    add_executable(blockcraft_placement_rules_tests
        tests/PlacementRulesTests.cpp
        src/blocks/BlockRegistry.cpp
        src/blocks/BlockShape.cpp
        src/blocks/PlacementRules.cpp
        src/core/Json.cpp
        src/items/ItemRegistry.cpp
        src/player/Player.cpp
        src/player/PlayerInventory.cpp
        src/world/Chunk.cpp
        src/world/World.cpp
        src/world/Raycast.cpp
    )
    target_include_directories(blockcraft_placement_rules_tests PRIVATE src)
    target_compile_definitions(blockcraft_placement_rules_tests PRIVATE
        BLOCKCRAFT_ASSET_ROOT="${BLOCKCRAFT_ASSET_ROOT}")
    target_link_libraries(blockcraft_placement_rules_tests PRIVATE glm::glm)
    if(MSVC)
        target_compile_options(blockcraft_placement_rules_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(blockcraft_placement_rules_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME blockcraft_placement_rules COMMAND blockcraft_placement_rules_tests)

    add_executable(blockcraft_block_entity_tests
        tests/BlockEntityTests.cpp
        src/blocks/BlockRegistry.cpp
        src/world/Chunk.cpp
        src/world/World.cpp
        src/world/BlockEntitySystem.cpp
        src/save/Nbt.cpp
    )
    target_include_directories(blockcraft_block_entity_tests PRIVATE src)
    target_link_libraries(blockcraft_block_entity_tests PRIVATE glm::glm zlibstatic)
    if(MSVC)
        target_compile_options(blockcraft_block_entity_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(blockcraft_block_entity_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME blockcraft_block_entities COMMAND blockcraft_block_entity_tests)

    add_executable(blockcraft_save_format_tests
        tests/SaveFormatTests.cpp
        src/save/Nbt.cpp
        src/save/RegionFile.cpp
        src/world/Chunk.cpp
    )
    target_include_directories(blockcraft_save_format_tests PRIVATE src)
    target_link_libraries(blockcraft_save_format_tests PRIVATE zlibstatic)
    if(MSVC)
        target_compile_options(blockcraft_save_format_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(blockcraft_save_format_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME blockcraft_save_formats COMMAND blockcraft_save_format_tests)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT blockcraft)
