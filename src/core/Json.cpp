#include "core/Json.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();
        if (position_ != text_.size()) fail("Unexpected trailing JSON data");
        return value;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw JsonParseError(message, position_);
    }

    void skipWhitespace() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) fail("Unexpected JSON token");
    }

    JsonValue parseValue() {
        if (position_ >= text_.size()) fail("Unexpected end of JSON");
        switch (text_[position_]) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return JsonValue(parseString());
            case 't': return parseLiteral("true", JsonValue(true));
            case 'f': return parseLiteral("false", JsonValue(false));
            case 'n': return parseLiteral("null", JsonValue(nullptr));
            default:
                if (text_[position_] == '-' ||
                    (text_[position_] >= '0' && text_[position_] <= '9')) return parseNumber();
                fail("Invalid JSON value");
        }
    }

    JsonValue parseLiteral(std::string_view literal, JsonValue value) {
        if (text_.substr(position_, literal.size()) != literal) fail("Invalid JSON literal");
        position_ += literal.size();
        return value;
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue::Object object;
        skipWhitespace();
        if (consume('}')) return JsonValue(std::move(object));
        while (true) {
            skipWhitespace();
            if (position_ >= text_.size() || text_[position_] != '"') fail("Expected JSON object key");
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            object.insert_or_assign(std::move(key), parseValue());
            skipWhitespace();
            if (consume('}')) break;
            expect(',');
        }
        return JsonValue(std::move(object));
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue::Array array;
        skipWhitespace();
        if (consume(']')) return JsonValue(std::move(array));
        while (true) {
            skipWhitespace();
            array.push_back(parseValue());
            skipWhitespace();
            if (consume(']')) break;
            expect(',');
        }
        return JsonValue(std::move(array));
    }

    static void appendUtf8(std::string& output, unsigned int codePoint) {
        if (codePoint <= 0x7FU) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else if (codePoint <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
    }

    unsigned int parseHex4() {
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            if (position_ >= text_.size()) fail("Incomplete JSON unicode escape");
            const char c = text_[position_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else fail("Invalid JSON unicode escape");
        }
        return value;
    }

    std::string parseString() {
        expect('"');
        std::string value;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') return value;
            if (static_cast<unsigned char>(c) < 0x20U) fail("Control character in JSON string");
            if (c != '\\') {
                value.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) fail("Incomplete JSON escape");
            const char escaped = text_[position_++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u': {
                    unsigned int codePoint = parseHex4();
                    if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
                        if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                            text_[position_ + 1] != 'u') fail("Invalid JSON surrogate pair");
                        position_ += 2;
                        const unsigned int low = parseHex4();
                        if (low < 0xDC00U || low > 0xDFFFU) fail("Invalid JSON surrogate pair");
                        codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) + (low - 0xDC00U);
                    }
                    appendUtf8(value, codePoint);
                    break;
                }
                default: fail("Invalid JSON escape");
            }
        }
        fail("Unterminated JSON string");
    }

    JsonValue parseNumber() {
        const std::size_t start = position_;
        if (consume('-')) {}
        if (consume('0')) {
            // Leading zero is complete unless followed by a fraction/exponent.
        } else {
            if (position_ >= text_.size() || text_[position_] < '1' || text_[position_] > '9')
                fail("Invalid JSON number");
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
        }
        if (consume('.')) {
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9')
                fail("Invalid JSON number fraction");
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9')
                fail("Invalid JSON number exponent");
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
        }
        const std::string number(text_.substr(start, position_ - start));
        char* end = nullptr;
        const double value = std::strtod(number.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value)) fail("Invalid JSON number");
        return JsonValue(value);
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

[[noreturn]] void wrongType(const char* expected) {
    throw std::runtime_error(std::string("JSON value is not ") + expected);
}

} // namespace

bool JsonValue::asBool() const {
    if (const bool* value = std::get_if<bool>(&value_)) return *value;
    wrongType("a boolean");
}

double JsonValue::asNumber() const {
    if (const double* value = std::get_if<double>(&value_)) return *value;
    wrongType("a number");
}

int JsonValue::asInt() const {
    const double value = asNumber();
    if (value < static_cast<double>(std::numeric_limits<int>::min()) ||
        value > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::floor(value) != value) wrongType("an integer");
    return static_cast<int>(value);
}

const std::string& JsonValue::asString() const {
    if (const auto* value = std::get_if<std::string>(&value_)) return *value;
    wrongType("a string");
}

const JsonValue::Array& JsonValue::asArray() const {
    if (const auto* value = std::get_if<Array>(&value_)) return *value;
    wrongType("an array");
}

const JsonValue::Object& JsonValue::asObject() const {
    if (const auto* value = std::get_if<Object>(&value_)) return *value;
    wrongType("an object");
}

const JsonValue* JsonValue::find(std::string_view key) const {
    const auto* object = std::get_if<Object>(&value_);
    if (object == nullptr) return nullptr;
    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

const JsonValue& JsonValue::at(std::string_view key) const {
    const JsonValue* value = find(key);
    if (value == nullptr) throw std::runtime_error("Missing JSON property: " + std::string(key));
    return *value;
}

std::string JsonValue::stringOr(std::string_view key, std::string fallback) const {
    const JsonValue* value = find(key);
    return value != nullptr && value->isString() ? value->asString() : std::move(fallback);
}

int JsonValue::intOr(std::string_view key, int fallback) const {
    const JsonValue* value = find(key);
    return value != nullptr && value->isNumber() ? value->asInt() : fallback;
}

bool JsonValue::boolOr(std::string_view key, bool fallback) const {
    const JsonValue* value = find(key);
    return value != nullptr && value->isBool() ? value->asBool() : fallback;
}

JsonValue JsonValue::parse(std::string_view text) {
    return Parser(text).parse();
}

JsonValue JsonValue::parseFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not open JSON file: " + path.string());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    try {
        return parse(buffer.str());
    } catch (const JsonParseError& error) {
        throw std::runtime_error("Invalid JSON in " + path.string() + " at byte " +
            std::to_string(error.offset()) + ": " + error.what());
    }
}
