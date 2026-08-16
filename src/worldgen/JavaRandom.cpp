#include "worldgen/JavaRandom.hpp"

#include <charconv>
#include <chrono>
#include <limits>
#include <random>
#include <stdexcept>

namespace {

std::uint32_t decodeUtf8(std::string_view text, std::size_t& index) {
    const auto first = static_cast<unsigned char>(text[index++]);
    if (first < 0x80U) return first;

    int continuationCount = 0;
    std::uint32_t value = 0;
    if ((first & 0xE0U) == 0xC0U) { continuationCount = 1; value = first & 0x1FU; }
    else if ((first & 0xF0U) == 0xE0U) { continuationCount = 2; value = first & 0x0FU; }
    else if ((first & 0xF8U) == 0xF0U) { continuationCount = 3; value = first & 0x07U; }
    else return 0xFFFDU;

    for (int i = 0; i < continuationCount; ++i) {
        if (index >= text.size()) return 0xFFFDU;
        const auto next = static_cast<unsigned char>(text[index]);
        if ((next & 0xC0U) != 0x80U) return 0xFFFDU;
        ++index;
        value = (value << 6U) | (next & 0x3FU);
    }
    return value <= 0x10FFFFU ? value : 0xFFFDU;
}

std::int64_t randomSeed() {
    std::random_device device;
    const std::uint64_t time = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(device()) << 32U) ^ device() ^ time);
}

} // namespace

void JavaRandom::setSeed(std::int64_t seed) {
    seed_ = (static_cast<std::uint64_t>(seed) ^ multiplier_) & mask_;
}

std::uint32_t JavaRandom::next(int bits) {
    seed_ = (seed_ * multiplier_ + addend_) & mask_;
    return static_cast<std::uint32_t>(seed_ >> (48 - bits));
}

std::int32_t JavaRandom::nextInt() {
    return static_cast<std::int32_t>(next(32));
}

std::int32_t JavaRandom::nextInt(std::int32_t bound) {
    if (bound <= 0) throw std::invalid_argument("JavaRandom bound must be positive");
    if ((bound & -bound) == bound)
        return static_cast<std::int32_t>((static_cast<std::int64_t>(bound) * next(31)) >> 31);

    std::int32_t bits = 0;
    std::int32_t value = 0;
    do {
        bits = static_cast<std::int32_t>(next(31));
        value = bits % bound;
    } while (static_cast<std::int64_t>(bits) - value + (bound - 1) > std::numeric_limits<std::int32_t>::max());
    return value;
}

std::int64_t JavaRandom::nextLong() {
    const std::int64_t high = static_cast<std::int32_t>(next(32));
    const std::int64_t low = static_cast<std::int32_t>(next(32));
    const std::uint64_t wrapped = static_cast<std::uint64_t>(high) * 0x100000000ULL + static_cast<std::uint64_t>(low);
    return static_cast<std::int64_t>(wrapped);
}

bool JavaRandom::nextBoolean() { return next(1) != 0; }
float JavaRandom::nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1U << 24U); }
double JavaRandom::nextDouble() {
    const std::uint64_t value = (static_cast<std::uint64_t>(next(26)) << 27U) + next(27);
    return static_cast<double>(value) / static_cast<double>(1ULL << 53U);
}

std::int32_t javaStringHash(std::string_view utf8) {
    std::uint32_t hash = 0;
    std::size_t index = 0;
    const auto addUnit = [&](std::uint16_t unit, std::uint32_t& current) { current = current * 31U + unit; };
    while (index < utf8.size()) {
        const std::uint32_t point = decodeUtf8(utf8, index);
        if (point <= 0xFFFFU) {
            addUnit(static_cast<std::uint16_t>(point), hash);
        } else {
            const std::uint32_t adjusted = point - 0x10000U;
            addUnit(static_cast<std::uint16_t>(0xD800U + (adjusted >> 10U)), hash);
            addUnit(static_cast<std::uint16_t>(0xDC00U + (adjusted & 0x3FFU)), hash);
        }
    }
    return static_cast<std::int32_t>(hash);
}

std::int64_t parseMinecraftSeed(std::string_view text) {
    if (text.empty()) return randomSeed();
    std::int64_t numeric = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), numeric, 10);
    if (result.ec == std::errc{} && result.ptr == text.data() + text.size())
        return numeric == 0 ? randomSeed() : numeric;
    return static_cast<std::int64_t>(javaStringHash(text));
}
