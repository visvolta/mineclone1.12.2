if(NOT DEFINED MC_JAR OR NOT EXISTS "${MC_JAR}")
    message(FATAL_ERROR "MC_JAR must point to an existing Minecraft 1.12.2 client JAR.")
endif()

if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "OUTPUT_DIR was not provided.")
endif()

set(required_textures
    bedrock.png cobblestone.png dirt.png glass.png grass_side.png
    grass_side_overlay.png grass_side_snowed.png grass_top.png gravel.png leaves_oak.png
    log_oak.png log_oak_top.png planks_oak.png sand.png stone.png
    water_still.png lava_still.png sandstone_normal.png sandstone_top.png
    sandstone_bottom.png coal_ore.png iron_ore.png gold_ore.png lapis_ore.png
    diamond_ore.png redstone_ore.png emerald_ore.png snow.png ice.png clay.png
    mycelium_side.png mycelium_top.png hardened_clay.png
    hardened_clay_stained_white.png hardened_clay_stained_orange.png
    hardened_clay_stained_magenta.png hardened_clay_stained_light_blue.png
    hardened_clay_stained_yellow.png hardened_clay_stained_lime.png
    hardened_clay_stained_pink.png hardened_clay_stained_gray.png
    hardened_clay_stained_silver.png hardened_clay_stained_cyan.png
    hardened_clay_stained_purple.png hardened_clay_stained_blue.png
    hardened_clay_stained_brown.png hardened_clay_stained_green.png
    hardened_clay_stained_red.png hardened_clay_stained_black.png
    red_sandstone_normal.png red_sandstone_top.png red_sandstone_bottom.png
    prismarine_rough.png sea_lantern.png
    stone_granite.png stone_granite_smooth.png stone_diorite.png
    stone_diorite_smooth.png stone_andesite.png stone_andesite_smooth.png
    coarse_dirt.png dirt_podzol_side.png dirt_podzol_top.png red_sand.png
    planks_spruce.png planks_birch.png planks_jungle.png planks_acacia.png
    planks_big_oak.png log_spruce.png log_spruce_top.png log_birch.png
    log_birch_top.png log_jungle.png log_jungle_top.png log_acacia.png
    log_acacia_top.png log_big_oak.png log_big_oak_top.png leaves_spruce.png
    leaves_birch.png leaves_jungle.png leaves_acacia.png leaves_big_oak.png
    tallgrass.png fern.png deadbush.png flower_dandelion.png flower_rose.png
    flower_blue_orchid.png flower_allium.png flower_houstonia.png
    flower_tulip_red.png flower_tulip_orange.png flower_tulip_white.png
    flower_tulip_pink.png flower_oxeye_daisy.png mushroom_brown.png
    mushroom_red.png cactus_side.png cactus_top.png cactus_bottom.png reeds.png
    vine.png waterlily.png pumpkin_side.png pumpkin_top.png pumpkin_face_off.png
    melon_side.png melon_top.png cobblestone_mossy.png bone_block_side.png
    bone_block_top.png web.png wool_colored_white.png brick.png bookshelf.png
    crafting_table_side.png crafting_table_top.png crafting_table_front.png
    furnace_side.png furnace_top.png
    sponge_wet.png obsidian.png tnt_side.png tnt_top.png tnt_bottom.png
    stonebrick.png stonebrick_mossy.png stonebrick_cracked.png stonebrick_carved.png
)

set(patterns)
foreach(texture IN LISTS required_textures)
    list(APPEND patterns "assets/minecraft/textures/blocks/${texture}")
endforeach()
list(APPEND patterns "assets/minecraft/textures/gui/icons.png")
list(APPEND patterns
    "assets/minecraft/textures/environment/sun.png"
    "assets/minecraft/textures/environment/moon_phases.png"
    "assets/minecraft/textures/environment/clouds.png"
    "assets/minecraft/textures/environment/rain.png"
    "assets/minecraft/textures/environment/snow.png"
)
# Fossils, igloos, and woodland mansions use the vanilla structure-template
# NBT files shipped by the client. Extract the originals instead of embedding
# or recreating Mojang assets in source.
list(APPEND patterns
    "assets/minecraft/structures/fossils/*"
    "assets/minecraft/structures/igloo/*"
    "assets/minecraft/structures/mansion/*"
)

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(ARCHIVE_EXTRACT INPUT "${MC_JAR}" DESTINATION "${OUTPUT_DIR}" PATTERNS ${patterns})

foreach(texture IN LISTS required_textures)
    set(extracted "${OUTPUT_DIR}/assets/minecraft/textures/blocks/${texture}")
    if(NOT EXISTS "${extracted}")
        message(FATAL_ERROR "The JAR is missing required texture: ${texture}")
    endif()
endforeach()

if(NOT EXISTS "${OUTPUT_DIR}/assets/minecraft/textures/gui/icons.png")
    message(FATAL_ERROR "The JAR is missing required GUI atlas: icons.png")
endif()

foreach(texture IN ITEMS sun.png moon_phases.png clouds.png rain.png snow.png)
    if(NOT EXISTS "${OUTPUT_DIR}/assets/minecraft/textures/environment/${texture}")
        message(FATAL_ERROR "The JAR is missing required environment texture: ${texture}")
    endif()
endforeach()

foreach(template IN ITEMS
        fossils/fossil_spine_01.nbt igloo/igloo_top.nbt mansion/entrance.nbt)
    if(NOT EXISTS "${OUTPUT_DIR}/assets/minecraft/structures/${template}")
        message(FATAL_ERROR "The JAR is missing required structure template: ${template}")
    endif()
endforeach()

message(STATUS "Extracted required Minecraft 1.12.2 textures from the local client JAR.")
