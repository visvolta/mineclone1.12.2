#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

#include "save/Nbt.hpp"
#include "save/RegionFile.hpp"
#include "world/Chunk.hpp"

namespace {

void testNbtRoundTrip() {
    nbt::Compound child;
    child["byte"] = nbt::Tag(static_cast<std::int8_t>(-4));
    child["short"] = nbt::Tag(static_cast<std::int16_t>(32000));
    child["int"] = nbt::Tag(static_cast<std::int32_t>(1234567));
    child["long"] = nbt::Tag(static_cast<std::int64_t>(-9876543210LL));
    child["float"] = nbt::Tag(1.25F);
    child["double"] = nbt::Tag(3.5);
    child["string"] = nbt::Tag(std::string("Blockcraft"));
    child["bytes"] = nbt::Tag(nbt::ByteArray{-1, 0, 1, 127});
    child["ints"] = nbt::Tag(nbt::IntArray{1, -2, 3});
    child["longs"] = nbt::Tag(nbt::LongArray{4, -5, 6});
    child["list"] = nbt::Tag(nbt::Type::Int,
        nbt::List{nbt::Tag(std::int32_t{7}), nbt::Tag(std::int32_t{8})});

    nbt::Document document;
    document.name = "root";
    document.root = nbt::Tag(std::move(child));
    const auto bytes = nbt::encode(document);
    const auto decoded = nbt::decode(bytes);
    assert(decoded.name == "root");
    assert(nbt::integer(decoded.root.compound(), "int") == 1234567);
    assert(nbt::integer(decoded.root.compound(), "long") == -9876543210LL);
    assert(nbt::string(decoded.root.compound(), "string") == "Blockcraft");

    const auto zlib = nbt::zlibCompress(bytes);
    assert(nbt::zlibDecompress(zlib) == bytes);
    const auto gzip = nbt::gzipCompress(bytes);
    assert(nbt::gzipDecompress(gzip) == bytes);
}

void testGzipFile(const std::filesystem::path& root) {
    nbt::Document document;
    document.root = nbt::Tag(nbt::Compound{{"Data", nbt::Tag(nbt::Compound{
        {"LevelName", nbt::Tag(std::string("Save Test"))},
        {"RandomSeed", nbt::Tag(std::int64_t{-123456789})}
    })}});
    const auto path = root / "level.dat";
    nbt::writeGzipFile(path, document);
    const auto loaded = nbt::readGzipFile(path);
    const auto& data = nbt::find(loaded.root.compound(), "Data")->compound();
    assert(nbt::string(data, "LevelName") == "Save Test");
    assert(nbt::integer(data, "RandomSeed") == -123456789);
}

void testRegionFile(const std::filesystem::path& root) {
    RegionFile region(root / "region" / "r.-1.0.mca");
    const std::vector<std::uint8_t> first{10, 0, 0, 0};
    const std::vector<std::uint8_t> second{10, 0, 0, 1, 0};
    region.writeChunk(31, 0, first);
    region.writeChunk(30, 1, second);
    assert(region.hasChunk(31, 0));
    assert(region.hasChunk(30, 1));
    assert(region.readChunk(31, 0).value() == first);
    assert(region.readChunk(30, 1).value() == second);

    const std::vector<std::uint8_t> replacement{10, 0, 0, 2, 0, 0};
    region.writeChunk(31, 0, replacement);
    assert(region.readChunk(31, 0).value() == replacement);
    assert(region.readChunk(30, 1).value() == second);
}

void testChunkStorage() {
    Chunk chunk(-3, 7);
    assert(chunk.set(2, 65, 9, makeBlockState(44, 3)));
    assert(blockId(chunk.get(2, 65, 9)) == 44);
    assert(blockMetadata(chunk.get(2, 65, 9)) == 3);
    chunk.setBiome(2, 9, 37);
    assert(chunk.biome(2, 9) == 37);
    chunk.setSaveMetadata(12345, 67890, false, true);
    assert(chunk.lastUpdate() == 12345);
    assert(chunk.inhabitedTime() == 67890);
    assert(!chunk.terrainPopulated());
    assert(chunk.lightPopulated());

    std::vector<std::uint8_t> sky(16 * 256 * 16, 15);
    std::vector<std::uint8_t> block(16 * 256 * 16, 0);
    const std::size_t index = static_cast<std::size_t>((65 * 16 + 9) * 16 + 2);
    sky[index] = 4;
    block[index] = 13;
    chunk.applyLighting(sky, block);
    assert(chunk.skyLight(2, 65, 9) == 4);
    assert(chunk.blockLight(2, 65, 9) == 13);

    nbt::Document tick;
    tick.root = nbt::Tag(nbt::Compound{
        {"i", nbt::Tag(std::string("minecraft:water"))},
        {"t", nbt::Tag(std::int32_t{12})},
        {"p", nbt::Tag(std::int32_t{0})},
        {"x", nbt::Tag(std::int32_t{-47})},
        {"y", nbt::Tag(std::int32_t{65})},
        {"z", nbt::Tag(std::int32_t{121})}
    });
    chunk.addScheduledTick(nbt::encode(tick));
    assert(chunk.scheduledTicks().size() == 1);
    assert(nbt::string(nbt::decode(chunk.scheduledTicks().front()).root.compound(), "i") == "minecraft:water");
}

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "blockcraft-stage8-save-tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);

    testNbtRoundTrip();
    testGzipFile(root);
    testRegionFile(root);
    testChunkStorage();

    std::filesystem::remove_all(root, error);
    std::cout << "Persistence tests passed.\n";
    return 0;
}
