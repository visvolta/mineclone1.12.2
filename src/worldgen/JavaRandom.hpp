#pragma once

#include <cstdint>
#include <string_view>

class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed = 0) { setSeed(seed); }

    void setSeed(std::int64_t seed);
    [[nodiscard]] std::int32_t nextInt();
    [[nodiscard]] std::int32_t nextInt(std::int32_t bound);
    [[nodiscard]] std::int64_t nextLong();
    [[nodiscard]] bool nextBoolean();
    [[nodiscard]] float nextFloat();
    [[nodiscard]] double nextDouble();

private:
    [[nodiscard]] std::uint32_t next(int bits);

    static constexpr std::uint64_t multiplier_ = 0x5DEECE66DULL;
    static constexpr std::uint64_t addend_ = 0xBULL;
    static constexpr std::uint64_t mask_ = (1ULL << 48U) - 1ULL;
    std::uint64_t seed_ = 0;
};

[[nodiscard]] std::int32_t javaStringHash(std::string_view utf8);
[[nodiscard]] std::int64_t parseMinecraftSeed(std::string_view text);
