#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace nbt {

enum class Type : std::uint8_t {
    End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6,
    ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12
};

struct Tag;
using ByteArray = std::vector<std::int8_t>;
using IntArray = std::vector<std::int32_t>;
using LongArray = std::vector<std::int64_t>;
using List = std::vector<Tag>;
using Compound = std::map<std::string, Tag, std::less<>>;

struct Tag {
    using Value = std::variant<std::monostate, std::int8_t, std::int16_t, std::int32_t, std::int64_t,
                               float, double, ByteArray, std::string, List, Compound, IntArray, LongArray>;
    Type type = Type::End;
    Type listType = Type::End;
    Value value{};

    Tag() = default;
    explicit Tag(std::int8_t v) : type(Type::Byte), value(v) {}
    explicit Tag(std::int16_t v) : type(Type::Short), value(v) {}
    explicit Tag(std::int32_t v) : type(Type::Int), value(v) {}
    explicit Tag(std::int64_t v) : type(Type::Long), value(v) {}
    explicit Tag(float v) : type(Type::Float), value(v) {}
    explicit Tag(double v) : type(Type::Double), value(v) {}
    explicit Tag(ByteArray v) : type(Type::ByteArray), value(std::move(v)) {}
    explicit Tag(std::string v) : type(Type::String), value(std::move(v)) {}
    explicit Tag(Compound v) : type(Type::Compound), value(std::move(v)) {}
    explicit Tag(IntArray v) : type(Type::IntArray), value(std::move(v)) {}
    explicit Tag(LongArray v) : type(Type::LongArray), value(std::move(v)) {}
    Tag(Type elementType, List v) : type(Type::List), listType(elementType), value(std::move(v)) {}

    [[nodiscard]] Compound& compound();
    [[nodiscard]] const Compound& compound() const;
    [[nodiscard]] List& list();
    [[nodiscard]] const List& list() const;
};

struct Document {
    std::string name;
    Tag root{Compound{}};
};

[[nodiscard]] std::vector<std::uint8_t> encode(const Document& document);
[[nodiscard]] Document decode(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::vector<std::uint8_t> zlibCompress(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::vector<std::uint8_t> zlibDecompress(const std::vector<std::uint8_t>& bytes, std::size_t expectedHint = 65536);
[[nodiscard]] std::vector<std::uint8_t> gzipCompress(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::vector<std::uint8_t> gzipDecompress(const std::vector<std::uint8_t>& bytes);
void writeGzipFile(const std::filesystem::path& path, const Document& document);
[[nodiscard]] Document readGzipFile(const std::filesystem::path& path);

[[nodiscard]] const Tag* find(const Compound& compound, std::string_view key);
[[nodiscard]] Tag* find(Compound& compound, std::string_view key);
[[nodiscard]] std::int64_t integer(const Compound& compound, std::string_view key, std::int64_t fallback = 0);
[[nodiscard]] double number(const Compound& compound, std::string_view key, double fallback = 0.0);
[[nodiscard]] std::string string(const Compound& compound, std::string_view key, std::string fallback = {});
[[nodiscard]] bool boolean(const Compound& compound, std::string_view key, bool fallback = false);

} // namespace nbt
