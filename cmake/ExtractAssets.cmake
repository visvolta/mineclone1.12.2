if(NOT DEFINED MC_JAR OR NOT EXISTS "${MC_JAR}")
    message(FATAL_ERROR "MC_JAR must point to an existing Minecraft 1.12.2 client JAR.")
endif()

if(NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
    message(FATAL_ERROR "OUTPUT_DIR was not provided.")
endif()

# The client JAR is the authoritative source for Minecraft 1.12.2 resource-pack
# data. Extract the complete namespace: no hand-made, generated, or placeholder
# game assets are accepted by the client.
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(ARCHIVE_EXTRACT
    INPUT "${MC_JAR}"
    DESTINATION "${OUTPUT_DIR}"
    PATTERNS
        "assets/*"
        "pack.png"
)

# Launcher-managed resources such as sounds live in the content-addressed
# assets/objects store. Materialize every indexed object when the launcher store
# is available so runtime resources still resolve through the same exact tree.
set(launcher_object_count 0)
if(DEFINED LAUNCHER_ASSETS_DIR AND NOT LAUNCHER_ASSETS_DIR STREQUAL "" AND
   DEFINED ASSET_INDEX_FILE AND NOT ASSET_INDEX_FILE STREQUAL "" AND
   EXISTS "${LAUNCHER_ASSETS_DIR}/objects" AND EXISTS "${ASSET_INDEX_FILE}")
    file(READ "${ASSET_INDEX_FILE}" asset_index_json)
    string(JSON asset_object_count ERROR_VARIABLE asset_index_error
        LENGTH "${asset_index_json}" objects)

    if(NOT asset_index_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR
            "Could not parse Minecraft launcher asset index: ${ASSET_INDEX_FILE}\n"
            "${asset_index_error}"
        )
    endif()

    if(asset_object_count GREATER 0)
        math(EXPR last_asset_index "${asset_object_count} - 1")
        foreach(asset_number RANGE 0 ${last_asset_index})
            string(JSON logical_name MEMBER "${asset_index_json}" objects ${asset_number})
            string(JSON object_hash GET "${asset_index_json}" objects "${logical_name}" hash)
            string(SUBSTRING "${object_hash}" 0 2 hash_prefix)

            set(object_source "${LAUNCHER_ASSETS_DIR}/objects/${hash_prefix}/${object_hash}")
            if(NOT EXISTS "${object_source}")
                message(FATAL_ERROR
                    "Minecraft launcher asset object is missing:\n"
                    "  logical name: ${logical_name}\n"
                    "  expected: ${object_source}\n"
                    "Repair/reinstall the Minecraft 1.12.2 assets in the launcher, or configure "
                    "BLOCKCRAFT_MINECRAFT_ASSETS_DIR/BLOCKCRAFT_MINECRAFT_ASSET_INDEX to a complete asset store."
                )
            endif()

            set(object_destination "${OUTPUT_DIR}/assets/${logical_name}")
            get_filename_component(object_destination_directory "${object_destination}" DIRECTORY)
            file(MAKE_DIRECTORY "${object_destination_directory}")
            file(COPY_FILE "${object_source}" "${object_destination}" ONLY_IF_DIFFERENT)
            math(EXPR launcher_object_count "${launcher_object_count} + 1")
        endforeach()
    endif()
endif()

# Fail immediately on a wrong/corrupt JAR. The Stage 1 HUD requirements are
# included explicitly here so there is never an internal fallback image/font.
set(required_jar_resources
    "assets/.mcassetsroot"
    "assets/minecraft/advancements/story/root.json"
    "assets/minecraft/blockstates/stone.json"
    "assets/minecraft/font/glyph_sizes.bin"
    "assets/minecraft/lang/en_us.lang"
    "assets/minecraft/loot_tables/chests/simple_dungeon.json"
    "assets/minecraft/models/block/stone.json"
    "assets/minecraft/models/item/apple.json"
    "assets/minecraft/models/item/iron_pickaxe.json"
    "assets/minecraft/recipes/acacia_planks.json"
    "assets/minecraft/shaders/program/blur.json"
    "assets/minecraft/structures/mansion/entrance.nbt"
    "assets/minecraft/textures/blocks/stone.png"
    "assets/minecraft/textures/blocks/destroy_stage_0.png"
    "assets/minecraft/textures/blocks/destroy_stage_9.png"
    "assets/minecraft/textures/colormap/foliage.png"
    "assets/minecraft/textures/colormap/grass.png"
    "assets/minecraft/textures/entity/steve.png"
    "assets/minecraft/textures/environment/clouds.png"
    "assets/minecraft/textures/font/ascii.png"
    "assets/minecraft/textures/gui/icons.png"
    "assets/minecraft/textures/gui/container/inventory.png"
    "assets/minecraft/textures/gui/container/creative_inventory/tabs.png"
    "assets/minecraft/textures/gui/container/creative_inventory/tab_items.png"
    "assets/minecraft/textures/gui/container/creative_inventory/tab_item_search.png"
    "assets/minecraft/textures/gui/container/creative_inventory/tab_inventory.png"
    "assets/minecraft/textures/gui/container/generic_54.png"
    "assets/minecraft/textures/gui/widgets.png"
    "assets/minecraft/textures/gui/options_background.png"
    "assets/minecraft/textures/gui/world_selection.png"
    "assets/minecraft/textures/gui/title/minecraft.png"
    "assets/minecraft/textures/gui/title/edition.png"
    "assets/minecraft/textures/gui/title/background/panorama_0.png"
    "assets/minecraft/textures/gui/title/background/panorama_1.png"
    "assets/minecraft/textures/gui/title/background/panorama_2.png"
    "assets/minecraft/textures/gui/title/background/panorama_3.png"
    "assets/minecraft/textures/gui/title/background/panorama_4.png"
    "assets/minecraft/textures/gui/title/background/panorama_5.png"
    "assets/minecraft/texts/splashes.txt"
    "assets/minecraft/textures/entity/chest/normal.png"
    "assets/minecraft/textures/entity/chest/normal_double.png"
    "assets/minecraft/textures/entity/chest/trapped.png"
    "assets/minecraft/textures/entity/chest/trapped_double.png"
    "assets/minecraft/textures/entity/sign.png"
    "assets/minecraft/textures/entity/bed/red.png"
    "assets/minecraft/textures/entity/bed/white.png"
    "assets/minecraft/textures/entity/shulker/shulker_white.png"
    "assets/minecraft/textures/entity/shulker/shulker_black.png"
    "assets/minecraft/textures/items/apple.png"
)

foreach(resource IN LISTS required_jar_resources)
    if(NOT EXISTS "${OUTPUT_DIR}/${resource}")
        message(FATAL_ERROR
            "Minecraft 1.12.2 JAR resource extraction is incomplete. Missing: ${resource}"
        )
    endif()
endforeach()

file(GLOB_RECURSE extracted_resource_files LIST_DIRECTORIES FALSE "${OUTPUT_DIR}/assets/*")
list(LENGTH extracted_resource_files extracted_resource_count)

if(launcher_object_count GREATER 0)
    message(STATUS
        "Extracted complete Minecraft 1.12.2 JAR assets and materialized "
        "${launcher_object_count} launcher-managed assets (${extracted_resource_count} total resource files)."
    )
else()
    message(STATUS "Extracted complete Minecraft 1.12.2 JAR assets (${extracted_resource_count} resource files).")
    message(STATUS
        "Launcher asset objects were not imported. This does not affect JAR resources such as textures, "
        "models, blockstates, colormaps, structures, recipes, loot tables, advancements, shaders, fonts, or language files."
    )
endif()
