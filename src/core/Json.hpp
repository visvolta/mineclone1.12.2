#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;

    JsonValue() = default;
    explicit JsonValue(std::nullptr_t) : value_(nullptr) {}
    explicit JsonValue(bool value) : value_(value) {}
    explicit JsonValue(double value) : value_(value) {}
    explicit JsonValue(std::string value) : value_(std::move(value)) {}
    explicit JsonValue(Array value) : value_(std::move(value)) {}
    explicit JsonValue(Object value) : value_(std::move(value)) {}

    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(value_); }
    [[nodiscard]] bool isNumber() const { return std::holds_alternative<double>(value_); }
    [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(value_); }
    [[nodiscard]] bool isArray() const { return std::holds_alternative<Array>(value_); }
    [[nodiscard]] bool isObject() const { return std::holds_alternative<Object>(value_); }

    [[nodiscard]] bool asBool() const;
    [[nodiscard]] double asNumber() const;
    [[nodiscard]] int asInt() const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array& asArray() const;
    [[nodiscard]] const Object& asObject() const;

    [[nodiscard]] const JsonValue* find(std::string_view key) const;
    [[nodiscard]] const JsonValue& at(std::string_view key) const;
    [[nodiscard]] std::string stringOr(std::string_view key, std::string fallback = {}) const;
    [[nodiscard]] int intOr(std::string_view key, int fallback) const;
    [[nodiscard]] bool boolOr(std::string_view key, bool fallback) const;

    [[nodiscard]] static JsonValue parse(std::string_view text);
    [[nodiscard]] static JsonValue parseFile(const std::filesystem::path& path);

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_{nullptr};
};

class JsonParseError : public std::runtime_error {
public:
    JsonParseError(std::string message, std::size_t offset)
        : std::runtime_error(std::move(message)), offset_(offset) {}
    [[nodiscard]] std::size_t offset() const { return offset_; }
private:
    std::size_t offset_ = 0;
};
