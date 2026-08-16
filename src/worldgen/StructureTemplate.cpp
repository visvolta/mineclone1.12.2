#include "worldgen/StructureTemplate.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <zlib.h>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/JavaRandom.hpp"

namespace {

struct Nbt {
    std::int64_t number = 0;
    std::string text;
    std::vector<Nbt> list;
    std::map<std::string, Nbt, std::less<>> compound;
};

class Reader {
public:
    explicit Reader(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    Nbt root() {
        if (u8() != 10) throw std::runtime_error("Structure template root is not a compound");
        static_cast<void>(string());
        return payload(10);
    }

private:
    std::uint8_t u8() {
        if (position_ >= bytes_.size()) throw std::runtime_error("Truncated structure NBT");
        return bytes_[position_++];
    }
    std::uint16_t u16() { return static_cast<std::uint16_t>((u8() << 8U) | u8()); }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value = (value << 8U) | u8();
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value = (value << 8U) | u8();
        return value;
    }
    std::string string() {
        const std::size_t length = u16();
        if (length > bytes_.size() - position_) throw std::runtime_error("Truncated structure NBT string");
        std::string value(reinterpret_cast<const char*>(bytes_.data() + position_), length);
        position_ += length;
        return value;
    }
    Nbt payload(std::uint8_t type) {
        Nbt value;
        switch (type) {
            case 1: value.number = static_cast<std::int8_t>(u8()); break;
            case 2: value.number = static_cast<std::int16_t>(u16()); break;
            case 3: value.number = static_cast<std::int32_t>(u32()); break;
            case 4: value.number = static_cast<std::int64_t>(u64()); break;
            case 5: static_cast<void>(u32()); break;
            case 6: static_cast<void>(u64()); break;
            case 7: {
                const int length = static_cast<std::int32_t>(u32());
                if (length < 0) throw std::runtime_error("Negative NBT byte-array length");
                for (int i = 0; i < length; ++i) static_cast<void>(u8());
                break;
            }
            case 8: value.text = string(); break;
            case 9: {
                const std::uint8_t child = u8();
                const int length = static_cast<std::int32_t>(u32());
                if (length < 0) throw std::runtime_error("Negative NBT list length");
                value.list.reserve(static_cast<std::size_t>(length));
                for (int i = 0; i < length; ++i) value.list.push_back(payload(child));
                break;
            }
            case 10:
                while (true) {
                    const std::uint8_t child = u8();
                    if (child == 0) break;
                    const std::string name = string();
                    Nbt childValue = payload(child);
                    value.compound.emplace(name, std::move(childValue));
                }
                break;
            case 11: {
                const int length = static_cast<std::int32_t>(u32());
                if (length < 0) throw std::runtime_error("Negative NBT int-array length");
                value.list.reserve(static_cast<std::size_t>(length));
                for (int i = 0; i < length; ++i) { Nbt item; item.number = static_cast<std::int32_t>(u32()); value.list.push_back(item); }
                break;
            }
            case 12: {
                const int length = static_cast<std::int32_t>(u32());
                if (length < 0) throw std::runtime_error("Negative NBT long-array length");
                for (int i = 0; i < length; ++i) static_cast<void>(u64());
                break;
            }
            default: throw std::runtime_error("Unsupported NBT tag in structure template: " +
                std::to_string(type));
        }
        return value;
    }

    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

const Nbt& member(const Nbt& nbt, std::string_view name) {
    const auto found = nbt.compound.find(name);
    if (found == nbt.compound.end()) throw std::runtime_error("Missing structure NBT field: " + std::string(name));
    return found->second;
}

BlockState paletteState(const Nbt& entry) {
    const std::string& name = member(entry, "Name").text;
    if (name == "minecraft:bone_block") {
        std::uint8_t metadata = 0;
        const auto properties = entry.compound.find("Properties");
        if (properties != entry.compound.end()) {
            const auto axis = properties->second.compound.find("axis");
            if (axis != properties->second.compound.end()) {
                if (axis->second.text == "x") metadata = 4;
                else if (axis->second.text == "z") metadata = 8;
            }
        }
        return makeBlockState(static_cast<std::uint16_t>(BlockId::BoneBlock), metadata);
    }
    if (name == "minecraft:coal_ore")
        return makeBlockState(static_cast<std::uint16_t>(BlockId::CoalOre));
    if (name == "minecraft:air") return makeBlockState(static_cast<std::uint16_t>(BlockId::Air));
    if (name == "minecraft:structure_void")
        return makeBlockState(static_cast<std::uint16_t>(BlockId::StructureVoid));
    throw std::runtime_error("Unsupported template palette state: " + name);
}

std::vector<std::uint8_t> inflateFile(const std::string& path) {
    gzFile file = gzopen(path.c_str(), "rb");
    if (file == nullptr) throw std::runtime_error("Could not open structure template: " + path);
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[4096];
    while (true) {
        const int count = gzread(file, buffer, sizeof(buffer));
        if (count < 0) { gzclose(file); throw std::runtime_error("Could not inflate structure template: " + path); }
        if (count == 0) break;
        bytes.insert(bytes.end(), buffer, buffer + count);
    }
    gzclose(file);
    return bytes;
}

} // namespace

StructureTemplate StructureTemplate::load(const std::string& path) {
    const Nbt root = Reader(inflateFile(path)).root();
    const Nbt& size = member(root, "size");
    if (size.list.size() != 3) throw std::runtime_error("Invalid structure template size");
    StructureTemplate result;
    result.sizeX_ = static_cast<int>(size.list[0].number);
    result.sizeY_ = static_cast<int>(size.list[1].number);
    result.sizeZ_ = static_cast<int>(size.list[2].number);
    std::vector<BlockState> palette;
    for (const Nbt& entry : member(root, "palette").list) palette.push_back(paletteState(entry));
    for (const Nbt& entry : member(root, "blocks").list) {
        const Nbt& position = member(entry, "pos");
        const int state = static_cast<int>(member(entry, "state").number);
        if (position.list.size() != 3 || state < 0 || state >= static_cast<int>(palette.size()))
            throw std::runtime_error("Invalid block in structure template");
        result.blocks_.push_back({static_cast<int>(position.list[0].number),
            static_cast<int>(position.list[1].number), static_cast<int>(position.list[2].number),
            palette[static_cast<std::size_t>(state)]});
    }
    return result;
}

int StructureTemplate::rotatedSizeX(int rotation) const { return (rotation & 1) != 0 ? sizeZ_ : sizeX_; }
int StructureTemplate::rotatedSizeZ(int rotation) const { return (rotation & 1) != 0 ? sizeX_ : sizeZ_; }

void StructureTemplate::place(World& world, JavaRandom& random, int originX, int originY,
                              int originZ, int rotation, float integrity,
                              int clipChunkX, int clipChunkZ) const {
    for (const TemplateBlock& source : blocks_) {
        if (integrity < 1.0F && random.nextFloat() > integrity) continue;
        int x = source.x;
        int z = source.z;
        switch (rotation & 3) {
            case 1: x = -source.z; z = source.x; break;
            case 2: x = -source.x; z = -source.z; break;
            case 3: x = source.z; z = -source.x; break;
            default: break;
        }
        const int worldX = originX + x;
        const int worldZ = originZ + z;
        if (World::floorDiv16(worldX) != clipChunkX || World::floorDiv16(worldZ) != clipChunkZ) continue;
        if (static_cast<BlockId>(blockId(source.state)) == BlockId::StructureVoid) continue;
        BlockState state = source.state;
        if (static_cast<BlockId>(blockId(state)) == BlockId::BoneBlock && (rotation & 1) != 0) {
            const std::uint8_t metadata = blockMetadata(state);
            if (metadata == 4) state = makeBlockState(blockId(state), 8);
            else if (metadata == 8) state = makeBlockState(blockId(state), 4);
        }
        world.setGeneratedBlock(worldX, originY + source.y, worldZ, state);
    }
}
