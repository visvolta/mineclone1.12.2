#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

class RegionFile {
public:
    explicit RegionFile(std::filesystem::path path) : path_(std::move(path)) {}

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> readChunk(int localX, int localZ) const;
    void writeChunk(int localX, int localZ, const std::vector<std::uint8_t>& uncompressedNbt);
    [[nodiscard]] bool hasChunk(int localX, int localZ) const;

private:
    struct StoredChunk {
        std::vector<std::uint8_t> compressed;
        std::uint32_t timestamp = 0;
    };
    [[nodiscard]] std::vector<std::optional<StoredChunk>> readAll() const;
    void rewrite(const std::vector<std::optional<StoredChunk>>& chunks) const;
    [[nodiscard]] static int index(int localX, int localZ);

    std::filesystem::path path_;
};
