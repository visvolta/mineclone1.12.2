#include "items/ItemRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "core/Json.hpp"

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string humanize(std::string_view value) {
    std::string result;
    bool upperNext = true;
    for (char c : value) {
        if (c == '_' || c == '.') {
            result.push_back(' ');
            upperNext = true;
        } else {
            result.push_back(upperNext ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
            upperNext = false;
        }
    }
    return result;
}

bool containsAny(std::string_view value, std::initializer_list<std::string_view> needles) {
    for (std::string_view needle : needles)
        if (value.find(needle) != std::string_view::npos) return true;
    return false;
}

std::uint16_t itemMetadataForBlock(BlockState state) {
    const BlockId id=static_cast<BlockId>(blockId(state)); const std::uint16_t meta=blockMetadata(state);
    switch(id){
        case BlockId::StoneSlab: case BlockId::WoodenSlab: return meta&7U;
        case BlockId::StoneSlab2: case BlockId::PurpurSlab: return 0;
        case BlockId::Log: case BlockId::Leaves: return meta&3U;
        case BlockId::Log2: case BlockId::Leaves2: return meta&1U;
        case BlockId::Anvil: return (meta>>2U)&3U;
        case BlockId::StandingBanner: case BlockId::WallBanner: return meta&15U;
        case BlockId::Bed: return meta&15U;
        case BlockId::WoodenDoor: case BlockId::IronDoor: case BlockId::SpruceDoor: case BlockId::BirchDoor:
        case BlockId::JungleDoor: case BlockId::AcaciaDoor: case BlockId::DarkOakDoor:
        case BlockId::Trapdoor: case BlockId::IronTrapdoor: case BlockId::FenceGate:
        case BlockId::SpruceFenceGate: case BlockId::BirchFenceGate: case BlockId::JungleFenceGate:
        case BlockId::DarkOakFenceGate: case BlockId::AcaciaFenceGate:
        case BlockId::OakStairs: case BlockId::StoneStairs: case BlockId::BrickStairs: case BlockId::StoneBrickStairs:
        case BlockId::NetherBrickStairs: case BlockId::SandstoneStairs: case BlockId::SpruceStairs: case BlockId::BirchStairs:
        case BlockId::JungleStairs: case BlockId::QuartzStairs: case BlockId::AcaciaStairs: case BlockId::DarkOakStairs:
        case BlockId::RedSandstoneStairs: case BlockId::PurpurStairs:
        case BlockId::Furnace: case BlockId::LitFurnace: case BlockId::Chest: case BlockId::TrappedChest:
        case BlockId::Dispenser: case BlockId::Dropper: case BlockId::Hopper: case BlockId::Observer:
            return 0;
        default:return meta;
    }
}

bool directBlockItemExcluded(BlockId id) {
    switch (id) {
        case BlockId::Air:
        case BlockId::FlowingWater: case BlockId::Water:
        case BlockId::FlowingLava: case BlockId::Lava:
        case BlockId::Bed:
        case BlockId::PistonHead: case BlockId::PistonExtension:
        case BlockId::Fire: case BlockId::MobSpawner:
        case BlockId::StandingSign: case BlockId::WallSign:
        case BlockId::WoodenDoor: case BlockId::IronDoor:
        case BlockId::RedstoneWire:
        case BlockId::Wheat:
        case BlockId::Portal:
        case BlockId::Cake:
        case BlockId::UnpoweredRepeater: case BlockId::PoweredRepeater:
        case BlockId::PumpkinStem: case BlockId::MelonStem:
        case BlockId::NetherWart:
        case BlockId::BrewingStand: case BlockId::Cauldron:
        case BlockId::EndPortal:
        case BlockId::Cocoa:
        case BlockId::Reeds:
        case BlockId::Tripwire:
        case BlockId::FlowerPot:
        case BlockId::Carrots: case BlockId::Potatoes:
        case BlockId::Skull:
        case BlockId::UnpoweredComparator: case BlockId::PoweredComparator:
        case BlockId::StandingBanner: case BlockId::WallBanner:
        case BlockId::SpruceDoor: case BlockId::BirchDoor: case BlockId::JungleDoor:
        case BlockId::AcaciaDoor: case BlockId::DarkOakDoor:
        case BlockId::Beetroots:
        case BlockId::EndGateway:
        case BlockId::FrostedIce:
            return true;
        default:
            return false;
    }
}

struct VanillaItemRow { std::uint16_t id; const char* name; };

constexpr VanillaItemRow vanillaItems[] = {
    {256,"iron_shovel"},{257,"iron_pickaxe"},{258,"iron_axe"},{259,"flint_and_steel"},
    {260,"apple"},{261,"bow"},{262,"arrow"},{263,"coal"},{264,"diamond"},{265,"iron_ingot"},
    {266,"gold_ingot"},{267,"iron_sword"},{268,"wooden_sword"},{269,"wooden_shovel"},
    {270,"wooden_pickaxe"},{271,"wooden_axe"},{272,"stone_sword"},{273,"stone_shovel"},
    {274,"stone_pickaxe"},{275,"stone_axe"},{276,"diamond_sword"},{277,"diamond_shovel"},
    {278,"diamond_pickaxe"},{279,"diamond_axe"},{280,"stick"},{281,"bowl"},{282,"mushroom_stew"},
    {283,"golden_sword"},{284,"golden_shovel"},{285,"golden_pickaxe"},{286,"golden_axe"},
    {287,"string"},{288,"feather"},{289,"gunpowder"},{290,"wooden_hoe"},{291,"stone_hoe"},
    {292,"iron_hoe"},{293,"diamond_hoe"},{294,"golden_hoe"},{295,"wheat_seeds"},{296,"wheat"},
    {297,"bread"},{298,"leather_helmet"},{299,"leather_chestplate"},{300,"leather_leggings"},
    {301,"leather_boots"},{302,"chainmail_helmet"},{303,"chainmail_chestplate"},
    {304,"chainmail_leggings"},{305,"chainmail_boots"},{306,"iron_helmet"},{307,"iron_chestplate"},
    {308,"iron_leggings"},{309,"iron_boots"},{310,"diamond_helmet"},{311,"diamond_chestplate"},
    {312,"diamond_leggings"},{313,"diamond_boots"},{314,"golden_helmet"},{315,"golden_chestplate"},
    {316,"golden_leggings"},{317,"golden_boots"},{318,"flint"},{319,"porkchop"},
    {320,"cooked_porkchop"},{321,"painting"},{322,"golden_apple"},{323,"sign"},
    {324,"wooden_door"},{325,"bucket"},{326,"water_bucket"},{327,"lava_bucket"},
    {328,"minecart"},{329,"saddle"},{330,"iron_door"},{331,"redstone"},{332,"snowball"},
    {333,"boat"},{334,"leather"},{335,"milk_bucket"},{336,"brick"},{337,"clay_ball"},
    {338,"reeds"},{339,"paper"},{340,"book"},{341,"slime_ball"},{342,"chest_minecart"},
    {343,"furnace_minecart"},{344,"egg"},{345,"compass"},{346,"fishing_rod"},{347,"clock"},
    {348,"glowstone_dust"},{349,"fish"},{350,"cooked_fish"},{351,"dye"},{352,"bone"},
    {353,"sugar"},{354,"cake"},{355,"bed"},{356,"repeater"},{357,"cookie"},{358,"filled_map"},
    {359,"shears"},{360,"melon"},{361,"pumpkin_seeds"},{362,"melon_seeds"},{363,"beef"},
    {364,"cooked_beef"},{365,"chicken"},{366,"cooked_chicken"},{367,"rotten_flesh"},
    {368,"ender_pearl"},{369,"blaze_rod"},{370,"ghast_tear"},{371,"gold_nugget"},
    {372,"nether_wart"},{373,"potion"},{374,"glass_bottle"},{375,"spider_eye"},
    {376,"fermented_spider_eye"},{377,"blaze_powder"},{378,"magma_cream"},{379,"brewing_stand"},
    {380,"cauldron"},{381,"ender_eye"},{382,"speckled_melon"},{383,"spawn_egg"},
    {384,"experience_bottle"},{385,"fire_charge"},{386,"writable_book"},{387,"written_book"},
    {388,"emerald"},{389,"item_frame"},{390,"flower_pot"},{391,"carrot"},{392,"potato"},
    {393,"baked_potato"},{394,"poisonous_potato"},{395,"map"},{396,"golden_carrot"},
    {397,"skull"},{398,"carrot_on_a_stick"},{399,"nether_star"},{400,"pumpkin_pie"},
    {401,"fireworks"},{402,"firework_charge"},{403,"enchanted_book"},{404,"comparator"},
    {405,"netherbrick"},{406,"quartz"},{407,"tnt_minecart"},{408,"hopper_minecart"},
    {409,"prismarine_shard"},{410,"prismarine_crystals"},{411,"rabbit"},{412,"cooked_rabbit"},
    {413,"rabbit_stew"},{414,"rabbit_foot"},{415,"rabbit_hide"},{416,"armor_stand"},
    {417,"iron_horse_armor"},{418,"golden_horse_armor"},{419,"diamond_horse_armor"},
    {420,"lead"},{421,"name_tag"},{422,"command_block_minecart"},{423,"mutton"},
    {424,"cooked_mutton"},{425,"banner"},{426,"end_crystal"},{427,"spruce_door"},
    {428,"birch_door"},{429,"jungle_door"},{430,"acacia_door"},{431,"dark_oak_door"},
    {432,"chorus_fruit"},{433,"chorus_fruit_popped"},{434,"beetroot"},{435,"beetroot_seeds"},
    {436,"beetroot_soup"},{437,"dragon_breath"},{438,"splash_potion"},{439,"spectral_arrow"},
    {440,"tipped_arrow"},{441,"lingering_potion"},{442,"shield"},{443,"elytra"},
    {444,"spruce_boat"},{445,"birch_boat"},{446,"jungle_boat"},{447,"acacia_boat"},
    {448,"dark_oak_boat"},{449,"totem_of_undying"},{450,"shulker_shell"},{452,"iron_nugget"},
    {453,"knowledge_book"},
    {2256,"record_13"},{2257,"record_cat"},{2258,"record_blocks"},{2259,"record_chirp"},
    {2260,"record_far"},{2261,"record_mall"},{2262,"record_mellohi"},{2263,"record_stal"},
    {2264,"record_strad"},{2265,"record_ward"},{2266,"record_11"},{2267,"record_wait"}
};


std::vector<std::uint16_t> creativeMetadata(const ItemDefinition& item) {
    std::vector<std::uint16_t> values{0};
    const std::string_view name = item.name;
    const auto range = [&](int count) {
        values.clear();
        for (int i = 0; i < count; ++i) values.push_back(static_cast<std::uint16_t>(i));
    };
    if (name == "stone") range(7);
    else if (name == "dirt") range(3);
    else if (name == "planks" || name == "sapling" || name == "wooden_slab") range(6);
    else if (name == "sand") range(2);
    else if (name == "log" || name == "log2" || name == "leaves" || name == "leaves2") range(name.ends_with("2") ? 2 : 4);
    else if (name == "sponge") range(2);
    else if (name == "sandstone" || name == "red_sandstone" || name == "prismarine" || name == "quartz_block") range(3);
    else if (name == "stonebrick") range(4);
    else if (name == "stone_slab") range(8);
    else if (name == "stone_slab2") range(1);
    else if (name == "monster_egg") range(6);
    else if (name == "red_flower") range(9);
    else if (name == "tallgrass") values = {1, 2};
    else if (name == "double_plant") range(6);
    else if (containsAny(name, {"wool", "stained_glass", "stained_hardened_clay", "stained_glass_pane",
                                "carpet", "concrete", "concrete_powder"})) range(16);
    else if (item.id == 263) range(2); // coal / charcoal
    else if (item.id == 322) range(2); // golden apple / enchanted golden apple
    else if (item.id == 349) range(4);
    else if (item.id == 350) range(2);
    else if (item.id == 351) range(16);
    else if (item.id == 355) range(16);
    else if (item.id == 397) range(6);
    else if (item.id == 425) range(16);
    return values;
}
} // namespace

ItemRegistry::ItemRegistry(const std::filesystem::path& assetRoot) {
    air_.id = 0;
    air_.name = "air";
    air_.displayName = "Air";
    const std::filesystem::path languagePath = assetRoot / "assets/minecraft/lang/en_us.lang";
    std::ifstream language(languagePath);
    if (!language) throw std::runtime_error("Missing Minecraft 1.12.2 language asset: " + languagePath.string());
    std::string line;
    while (std::getline(language, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        language_.emplace(line.substr(0, equals), line.substr(equals + 1));
    }
    registerBlockItems(assetRoot);
    registerStandaloneItems(assetRoot);
    for (auto& tab : creativeTabs_)
        std::sort(tab.begin(), tab.end(), [&](std::uint16_t a, std::uint16_t b) {
            return get(a).id < get(b).id;
        });
}

void ItemRegistry::add(ItemDefinition item) {
    if (item.id == 0 || item.name.empty()) return;
    if (byId_.contains(item.id) || byName_.contains(item.name))
        throw std::runtime_error("Duplicate Minecraft 1.12.2 item registration: " + item.name);
    const std::size_t index = items_.size();
    byId_[item.id] = index;
    byName_[item.name] = index;
    if (item.tab != CreativeTab::Search && item.tab != CreativeTab::Inventory &&
        item.tab != CreativeTab::Hotbar) {
        const std::size_t tabIndex = static_cast<std::size_t>(item.tab);
        if (tabIndex < creativeTabs_.size()) creativeTabs_[tabIndex].push_back(item.id);
    }
    items_.push_back(std::move(item));
}

void ItemRegistry::registerBlockItems(const std::filesystem::path& assetRoot) {
    const auto modelRoot = assetRoot / "assets/minecraft/models/item";
    for (std::uint16_t numericId = 1; numericId < 256; ++numericId) {
        if (!BlockRegistry::isRegisteredId(numericId)) continue;
        const BlockId block = static_cast<BlockId>(numericId);
        if (directBlockItemExcluded(block)) continue;
        const std::string name(BlockRegistry::legacyName(numericId));
        if (!std::filesystem::exists(modelRoot / (name + ".json"))) continue;
        ItemDefinition definition;
        definition.id = numericId;
        definition.name = name;
        definition.displayName = displayNameFor(name, true);
        definition.iconResource = resolveItemTexture(assetRoot, name);
        definition.tab = inferTab(name, block);
        definition.maxStackSize = 64;
        definition.hasSubtypes = true;
        definition.placedBlock = block;
        add(definition);
        for (std::uint8_t metadata = 0; metadata < 16; ++metadata)
            blockStateToItem_[makeBlockState(numericId, metadata)] = numericId;
    }
}

void ItemRegistry::registerStandaloneItems(const std::filesystem::path& assetRoot) {
    for (const VanillaItemRow& row : vanillaItems) {
        ItemDefinition definition;
        definition.id = row.id;
        definition.name = row.name;
        definition.displayName = displayNameFor(row.name, false);
        definition.iconResource = resolveItemTexture(assetRoot, row.name);
        definition.tab = inferTab(row.name, std::nullopt);
        definition.maxStackSize = inferMaxStack(row.name);
        definition.hasSubtypes = containsAny(row.name, {"fish", "dye", "potion", "spawn_egg", "skull", "banner"});
        definition.placedBlock = specialPlacedBlock(row.id);
        add(std::move(definition));
    }
}

const ItemDefinition& ItemRegistry::get(std::uint16_t id) const {
    const auto found = byId_.find(id);
    return found == byId_.end() ? air_ : items_[found->second];
}

const ItemDefinition* ItemRegistry::find(std::string_view name) const {
    const auto found = byName_.find(std::string(name));
    return found == byName_.end() ? nullptr : &items_[found->second];
}

ItemStack ItemRegistry::stackForBlock(BlockState state, int count) const {
    const auto direct = blockStateToItem_.find(state);
    if (direct != blockStateToItem_.end())
        return {direct->second, count, itemMetadataForBlock(state), {}};
    const auto base = blockStateToItem_.find(makeBlockState(blockId(state), 0));
    if (base != blockStateToItem_.end())
        return {base->second, count, itemMetadataForBlock(state), {}};
    const BlockId wanted = static_cast<BlockId>(blockId(state));
    for (const ItemDefinition& item : items_)
        if (item.placedBlock && *item.placedBlock == wanted)
            return {item.id, count, itemMetadataForBlock(state), {}};
    return {};
}

const std::vector<std::uint16_t>& ItemRegistry::itemsForTab(CreativeTab tab) const {
    static const std::vector<std::uint16_t> empty;
    const std::size_t index = static_cast<std::size_t>(tab);
    return index < creativeTabs_.size() ? creativeTabs_[index] : empty;
}

std::vector<std::uint16_t> ItemRegistry::search(std::string_view text) const {
    const std::string needle = lower(std::string(text));
    std::vector<std::uint16_t> result;
    for (const ItemDefinition& item : items_) {
        if (needle.empty() || lower(item.name).find(needle) != std::string::npos ||
            lower(item.displayName).find(needle) != std::string::npos)
            result.push_back(item.id);
    }
    return result;
}

std::vector<ItemStack> ItemRegistry::creativeStacks(CreativeTab tab) const {
    std::vector<ItemStack> result;
    for (std::uint16_t id : itemsForTab(tab)) {
        const ItemDefinition& item = get(id);
        for (std::uint16_t meta : creativeMetadata(item))
            result.push_back({id, item.maxStackSize, meta, {}});
    }
    return result;
}

std::vector<ItemStack> ItemRegistry::searchStacks(std::string_view text) const {
    const std::string needle = lower(std::string(text));
    std::vector<ItemStack> result;
    for (const ItemDefinition& item : items_) {
        for (std::uint16_t meta : creativeMetadata(item)) {
            ItemStack stack{item.id, item.maxStackSize, meta, {}};
            if (!needle.empty() && lower(item.name).find(needle) == std::string::npos &&
                lower(item.displayName).find(needle) == std::string::npos &&
                lower(stackDisplayName(stack)).find(needle) == std::string::npos) continue;
            result.push_back(std::move(stack));
        }
    }
    return result;
}

std::string ItemRegistry::stackDisplayName(const ItemStack& stack) const {
    if (stack.empty()) return "";
    const ItemDefinition& item = get(stack.itemId);
    const std::uint16_t meta = stack.damage & 15U;
    const auto color = [](std::uint16_t value) -> std::string_view {
        constexpr std::array<std::string_view, 16> names = {"Black","Red","Green","Brown","Blue","Purple","Cyan","Light Gray",
            "Gray","Pink","Lime","Yellow","Light Blue","Magenta","Orange","White"};
        return names[std::min<std::size_t>(value, 15)];
    };
    const auto localized = [&](std::string_view key, std::string_view fallback) {
        const auto found = language_.find(std::string(key));
        return found != language_.end() ? found->second : std::string(fallback);
    };
    if (item.id == 263) return meta == 1 ? "Charcoal" : "Coal";
    if (item.id == 322) return meta == 1 ? "Enchanted Golden Apple" : "Golden Apple";
    if (item.id == 355) {
        constexpr std::array<std::string_view,16> keys={"white","orange","magenta","lightBlue","yellow","lime","pink","gray","silver","cyan","purple","blue","brown","green","red","black"};
        constexpr std::array<std::string_view,16> fallback={"White Bed","Orange Bed","Magenta Bed","Light Blue Bed","Yellow Bed","Lime Bed","Pink Bed","Gray Bed","Light Gray Bed","Cyan Bed","Purple Bed","Blue Bed","Brown Bed","Green Bed","Red Bed","Black Bed"};
        const std::size_t i=std::min<std::size_t>(meta,15);
        const std::string key="item.bed."+std::string(keys[i])+".name";
        return localized(key, fallback[i]);
    }
    if (item.id == 397) {
        constexpr std::array<std::string_view, 6> skulls = {
            "Skeleton Skull", "Wither Skeleton Skull", "Zombie Head",
            "Player Head", "Creeper Head", "Dragon Head"
        };
        return std::string(skulls[std::min<std::size_t>(meta, 5)]);
    }
    if (item.id == 425) return std::string(color(15U-meta)) + " Banner";
    if (item.id == 349) {
        constexpr std::array<std::string_view, 4> names = {"Raw Fish", "Raw Salmon", "Clownfish", "Pufferfish"};
        return std::string(names[std::min<std::size_t>(meta, 3)]);
    }
    if (item.id == 350) return meta == 1 ? "Cooked Salmon" : "Cooked Fish";
    if (item.id == 351) {
        constexpr std::array<std::string_view,16> keys={"black","red","green","brown","blue","purple","cyan","silver","gray","pink","lime","yellow","lightBlue","magenta","orange","white"};
        const std::string key="item.dyePowder."+std::string(keys[std::min<std::size_t>(meta,15)])+".name";
        return localized(key, std::string(color(meta))+" Dye");
    }
    if (!item.placedBlock) return item.displayName;
    switch (*item.placedBlock) {
        case BlockId::Stone: {
            constexpr std::array<std::string_view,7> n={"Stone","Granite","Polished Granite","Diorite","Polished Diorite","Andesite","Polished Andesite"};
            return std::string(n[std::min<std::size_t>(meta,6)]);
        }
        case BlockId::Dirt: {
            constexpr std::array<std::string_view,3> n={"Dirt","Coarse Dirt","Podzol"};
            return std::string(n[std::min<std::size_t>(meta,2)]);
        }
        case BlockId::Planks: case BlockId::Sapling: case BlockId::WoodenSlab: {
            constexpr std::array<std::string_view,6> wood={"Oak","Spruce","Birch","Jungle","Acacia","Dark Oak"};
            const std::size_t i=std::min<std::size_t>(meta,5);
            if (*item.placedBlock == BlockId::WoodenSlab) {
                constexpr std::array<std::string_view,6> keys={"oak","spruce","birch","jungle","acacia","big_oak"};
                return localized("tile.woodSlab."+std::string(keys[i])+".name", std::string(wood[i])+" Wood Slab");
            }
            const std::string suffix = *item.placedBlock == BlockId::Sapling ? " Sapling" : " Wood Planks";
            return std::string(wood[i]) + suffix;
        }
        case BlockId::Sand: return meta == 1 ? "Red Sand" : "Sand";
        case BlockId::Sponge: return meta == 1 ? "Wet Sponge" : "Sponge";
        case BlockId::Sandstone: {
            constexpr std::array<std::string_view,3> n={"Sandstone","Chiseled Sandstone","Smooth Sandstone"};
            return std::string(n[std::min<std::size_t>(meta,2)]);
        }
        case BlockId::RedSandstone: {
            constexpr std::array<std::string_view,3> n={"Red Sandstone","Chiseled Red Sandstone","Smooth Red Sandstone"};
            return std::string(n[std::min<std::size_t>(meta,2)]);
        }
        case BlockId::Prismarine: {
            constexpr std::array<std::string_view,3> n={"Prismarine","Prismarine Bricks","Dark Prismarine"};
            return std::string(n[std::min<std::size_t>(meta,2)]);
        }
        case BlockId::QuartzBlock: {
            constexpr std::array<std::string_view,3> n={"Block of Quartz","Chiseled Quartz Block","Pillar Quartz Block"};
            return std::string(n[std::min<std::size_t>(meta,2)]);
        }
        case BlockId::StoneBrick: {
            constexpr std::array<std::string_view,4> n={"Stone Bricks","Mossy Stone Bricks","Cracked Stone Bricks","Chiseled Stone Bricks"};
            return std::string(n[std::min<std::size_t>(meta,3)]);
        }
        case BlockId::StoneSlab: {
            constexpr std::array<std::string_view,8> suffix={"stone","sand","wood","cobble","brick","smoothStoneBrick","netherBrick","quartz"};
            constexpr std::array<std::string_view,8> fallback={"Stone Slab","Sandstone Slab","Wooden Slab","Cobblestone Slab","Bricks Slab","Stone Bricks Slab","Nether Brick Slab","Quartz Slab"};
            const std::size_t i=std::min<std::size_t>(meta,7);
            return localized("tile.stoneSlab."+std::string(suffix[i])+".name", fallback[i]);
        }
        case BlockId::StoneSlab2: return localized("tile.stoneSlab2.red_sandstone.name", "Red Sandstone Slab");
        case BlockId::Log: case BlockId::Leaves: {
            constexpr std::array<std::string_view,4> wood={"Oak","Spruce","Birch","Jungle"};
            return std::string(wood[std::min<std::size_t>(meta&3U,3)]) + (*item.placedBlock==BlockId::Log ? " Wood" : " Leaves");
        }
        case BlockId::Log2: case BlockId::Leaves2: {
            constexpr std::array<std::string_view,2> wood={"Acacia","Dark Oak"};
            return std::string(wood[std::min<std::size_t>(meta&1U,1)]) + (*item.placedBlock==BlockId::Log2 ? " Wood" : " Leaves");
        }
        case BlockId::Anvil: {
            constexpr std::array<std::string_view,3> n={"Anvil","Slightly Damaged Anvil","Very Damaged Anvil"};
            return std::string(n[std::min<std::size_t>(meta,2)]);
        }
        case BlockId::MonsterEgg: {
            constexpr std::array<std::string_view,6> n={"Stone Monster Egg","Cobblestone Monster Egg","Stone Brick Monster Egg","Mossy Stone Brick Monster Egg","Cracked Stone Brick Monster Egg","Chiseled Stone Brick Monster Egg"};
            return std::string(n[std::min<std::size_t>(meta,5)]);
        }
        case BlockId::RedFlower: {
            constexpr std::array<std::string_view,9> n={"Poppy","Blue Orchid","Allium","Azure Bluet","Red Tulip","Orange Tulip","White Tulip","Pink Tulip","Oxeye Daisy"};
            return std::string(n[std::min<std::size_t>(meta,8)]);
        }
        case BlockId::TallGrass: return meta == 2 ? "Fern" : "Grass";
        case BlockId::DoublePlant: {
            constexpr std::array<std::string_view,6> n={"Sunflower","Lilac","Double Tallgrass","Large Fern","Rose Bush","Peony"};
            return std::string(n[std::min<std::size_t>(meta,5)]);
        }
        case BlockId::Wool: return std::string(color(15U-meta)) + " Wool";
        case BlockId::StainedGlass: return std::string(color(15U-meta)) + " Stained Glass";
        case BlockId::StainedGlassPane: return std::string(color(15U-meta)) + " Stained Glass Pane";
        case BlockId::StainedHardenedClay: return std::string(color(15U-meta)) + " Terracotta";
        case BlockId::Carpet: return std::string(color(15U-meta)) + " Carpet";
        case BlockId::Concrete: return std::string(color(15U-meta)) + " Concrete";
        case BlockId::ConcretePowder: return std::string(color(15U-meta)) + " Concrete Powder";
        default: return item.displayName;
    }
}

std::string ItemRegistry::displayNameFor(std::string_view resourceName, bool block) const {
    const std::array<std::string, 4> candidates = {
        std::string(block ? "tile." : "item.") + std::string(resourceName) + ".name",
        std::string("item.") + std::string(resourceName) + ".name",
        std::string("tile.") + std::string(resourceName) + ".name",
        std::string(resourceName) + ".name"
    };
    for (const std::string& key : candidates) {
        const auto found = language_.find(key);
        if (found != language_.end()) return found->second;
    }
    return humanize(resourceName);
}

std::string ItemRegistry::resolveItemTexture(const std::filesystem::path& assetRoot,
                                             std::string_view resourceName) const {
    std::string modelName(resourceName);
    if (modelName == "wooden_door") modelName = "oak_door";
    else if (modelName == "boat") modelName = "oak_boat";
    else if (modelName == "fish") modelName = "cod";
    else if (modelName == "cooked_fish") modelName = "cooked_cod";
    else if (modelName == "totem_of_undying") modelName = "totem";
    else if (modelName == "potion") return "minecraft:items/potion_bottle_drinkable";
    else if (modelName == "splash_potion") return "minecraft:items/potion_bottle_splash";
    else if (modelName == "lingering_potion") return "minecraft:items/potion_bottle_lingering";
    else if (modelName == "dye") return "minecraft:items/dye_powder_black";

    // 1.12.2 shulker-box item models use builtin/entity and therefore have no
    // layer0 sprite. Use the exact per-colour block particle/top texture from
    // the JAR for the 2D inventory icon rather than falling back to missingno.
    if (modelName.ends_with("_shulker_box")) {
        const std::string color = modelName.substr(0, modelName.size() - std::string("_shulker_box").size());
        return "minecraft:blocks/shulker_top_" + color;
    }

    std::unordered_map<std::string, std::string> textures;
    std::unordered_set<std::string> visited;
    std::function<void(std::string, bool)> load = [&](std::string model, bool itemDefault) {
        if (model.starts_with("minecraft:")) model.erase(0, 10);
        if (model == "builtin/entity" || model == "item/generated" || model == "item/handheld") return;
        if (model.find('/') == std::string::npos) model.insert(0, itemDefault ? "item/" : "block/");
        if (!visited.insert(model).second) return;
        const auto path = assetRoot / "assets/minecraft/models" / (model + ".json");
        if (!std::filesystem::exists(path)) return;
        const JsonValue root = JsonValue::parseFile(path);
        const std::string parent = root.stringOr("parent", "");
        if (!parent.empty()) load(parent, model.starts_with("item/"));
        if (const JsonValue* object = root.find("textures"); object && object->isObject())
            for (const auto& [key, value] : object->asObject())
                if (value.isString()) textures[key] = value.asString();
    };
    load("item/" + modelName, true);

    const auto resolve = [&](std::string value) {
        std::unordered_set<std::string> aliases;
        while (!value.empty() && value.front() == '#') {
            if (!aliases.insert(value).second) return std::string{};
            const auto alias = textures.find(value.substr(1));
            if (alias == textures.end()) return std::string{};
            value = alias->second;
        }
        if (value.empty()) return std::string{};
        if (value.find(':') == std::string::npos) value.insert(0, "minecraft:");
        const std::size_t colon = value.find(':');
        std::string path = value.substr(colon + 1);
        if (!path.starts_with("items/") && !path.starts_with("blocks/")) path.insert(0, "items/");
        return value.substr(0, colon + 1) + path;
    };

    for (std::string_view key : {"layer0", "particle", "all", "top", "side", "texture"}) {
        const auto found = textures.find(std::string(key));
        if (found == textures.end()) continue;
        std::string resolved = resolve(found->second);
        if (!resolved.empty()) return resolved;
    }
    return "minecraft:items/" + modelName;
}

CreativeTab ItemRegistry::inferTab(std::string_view name, std::optional<BlockId> block) {
    if (containsAny(name, {"rail", "minecart", "boat", "saddle", "carrot_on_a_stick"})) return CreativeTab::Transportation;
    if (containsAny(name, {"redstone", "repeater", "comparator", "lever", "button", "pressure_plate",
                           "piston", "observer", "daylight", "tripwire", "dispenser", "dropper"})) return CreativeTab::Redstone;
    if (containsAny(name, {"sword", "bow", "arrow", "helmet", "chestplate", "leggings", "boots",
                           "shield", "totem", "elytra", "horse_armor"})) return CreativeTab::Combat;
    if (containsAny(name, {"pickaxe", "shovel", "_axe", "hoe", "shears", "fishing_rod", "flint_and_steel",
                           "compass", "clock", "lead", "name_tag"})) return CreativeTab::Tools;
    if (containsAny(name, {"potion", "brewing", "blaze_powder", "magma_cream", "ghast_tear", "spider_eye",
                           "dragon_breath", "speckled_melon", "golden_carrot", "nether_wart", "glass_bottle"})) return CreativeTab::Brewing;
    if (containsAny(name, {"apple", "bread", "stew", "cookie", "melon", "beef", "chicken", "porkchop",
                           "mutton", "rabbit", "potato", "carrot", "beetroot", "fish", "rotten_flesh", "pumpkin_pie"})) return CreativeTab::Food;
    if (containsAny(name, {"ingot", "nugget", "diamond", "emerald", "coal", "quartz", "dust", "shard", "crystals",
                           "stick", "string", "feather", "gunpowder", "leather", "brick", "clay_ball", "paper",
                           "slime_ball", "bone", "sugar", "blaze_rod", "shulker_shell", "chorus_fruit_popped"})) return CreativeTab::Misc;
    if (block) {
        if (containsAny(name, {"sapling", "leaves", "flower", "mushroom", "grass", "fern", "deadbush", "torch",
                               "vine", "lily", "cactus", "web", "pane", "carpet", "banner", "skull",
                               "flower_pot", "bookshelf", "ladder", "snow_layer"})) return CreativeTab::Decorations;
        return CreativeTab::BuildingBlocks;
    }
    return CreativeTab::Misc;
}

int ItemRegistry::inferMaxStack(std::string_view name) {
    if (name == "bucket" || name == "sign" || name == "snowball" || name == "egg" ||
        name == "ender_pearl" || name == "armor_stand" || name == "written_book") return 16;
    if (containsAny(name, {"water_bucket", "lava_bucket", "milk_bucket", "stew", "beetroot_soup",
                           "sword", "pickaxe", "shovel", "_axe", "hoe", "helmet", "chestplate", "leggings", "boots",
                           "bow", "fishing_rod", "flint_and_steel", "shears", "filled_map", "carrot_on_a_stick",
                           "shield", "elytra", "horse_armor", "saddle", "enchanted_book", "bed", "potion",
                           "totem_of_undying", "knowledge_book", "record_", "minecart", "boat"})) return 1;
    return 64;
}

std::optional<BlockId> ItemRegistry::specialPlacedBlock(std::uint16_t itemId) {
    switch (itemId) {
        case 287: return BlockId::Tripwire;
        case 295: return BlockId::Wheat;
        case 323: return BlockId::StandingSign;
        case 324: return BlockId::WoodenDoor;
        case 330: return BlockId::IronDoor;
        case 331: return BlockId::RedstoneWire;
        case 338: return BlockId::Reeds;
        case 354: return BlockId::Cake;
        case 355: return BlockId::Bed;
        case 356: return BlockId::UnpoweredRepeater;
        case 361: return BlockId::PumpkinStem;
        case 362: return BlockId::MelonStem;
        case 372: return BlockId::NetherWart;
        case 379: return BlockId::BrewingStand;
        case 380: return BlockId::Cauldron;
        case 390: return BlockId::FlowerPot;
        case 391: return BlockId::Carrots;
        case 392: return BlockId::Potatoes;
        case 397: return BlockId::Skull;
        case 404: return BlockId::UnpoweredComparator;
        case 425: return BlockId::StandingBanner;
        case 427: return BlockId::SpruceDoor;
        case 428: return BlockId::BirchDoor;
        case 429: return BlockId::JungleDoor;
        case 430: return BlockId::AcaciaDoor;
        case 431: return BlockId::DarkOakDoor;
        case 435: return BlockId::Beetroots;
        default: return std::nullopt;
    }
}

std::string_view ItemRegistry::tabName(CreativeTab tab) {
    switch (tab) {
        case CreativeTab::BuildingBlocks: return "Building Blocks";
        case CreativeTab::Decorations: return "Decoration Blocks";
        case CreativeTab::Redstone: return "Redstone";
        case CreativeTab::Transportation: return "Transportation";
        case CreativeTab::Misc: return "Miscellaneous";
        case CreativeTab::Food: return "Foodstuffs";
        case CreativeTab::Tools: return "Tools";
        case CreativeTab::Combat: return "Combat";
        case CreativeTab::Brewing: return "Brewing";
        case CreativeTab::Hotbar: return "Saved Toolbars";
        case CreativeTab::Search: return "Search Items";
        case CreativeTab::Inventory: return "Survival Inventory";
    }
    return "Items";
}
