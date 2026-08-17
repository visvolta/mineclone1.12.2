#include "save/Nbt.hpp"

#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <zlib.h>

namespace nbt {
namespace {

class Writer {
public:
    void u8(std::uint8_t v) { bytes_.push_back(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v >> 8)); u8(static_cast<std::uint8_t>(v)); }
    void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v >> 16)); u16(static_cast<std::uint16_t>(v)); }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v >> 32)); u32(static_cast<std::uint32_t>(v)); }
    void stringValue(std::string_view value) {
        if (value.size() > 65535) throw std::runtime_error("NBT string exceeds 65535 bytes");
        u16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void payload(const Tag& tag) {
        switch (tag.type) {
            case Type::End: break;
            case Type::Byte: u8(static_cast<std::uint8_t>(std::get<std::int8_t>(tag.value))); break;
            case Type::Short: u16(static_cast<std::uint16_t>(std::get<std::int16_t>(tag.value))); break;
            case Type::Int: u32(static_cast<std::uint32_t>(std::get<std::int32_t>(tag.value))); break;
            case Type::Long: u64(static_cast<std::uint64_t>(std::get<std::int64_t>(tag.value))); break;
            case Type::Float: u32(std::bit_cast<std::uint32_t>(std::get<float>(tag.value))); break;
            case Type::Double: u64(std::bit_cast<std::uint64_t>(std::get<double>(tag.value))); break;
            case Type::ByteArray: {
                const auto& a = std::get<ByteArray>(tag.value); u32(static_cast<std::uint32_t>(a.size()));
                for (std::int8_t v : a) u8(static_cast<std::uint8_t>(v));
                break;
            }
            case Type::String: stringValue(std::get<std::string>(tag.value)); break;
            case Type::List: {
                const auto& list = std::get<List>(tag.value);
                u8(static_cast<std::uint8_t>(tag.listType)); u32(static_cast<std::uint32_t>(list.size()));
                for (const Tag& child : list) {
                    if (child.type != tag.listType) throw std::runtime_error("NBT list contains mixed tag types");
                    payload(child);
                }
                break;
            }
            case Type::Compound: {
                for (const auto& [name, child] : std::get<Compound>(tag.value)) {
                    if (child.type == Type::End) continue;
                    u8(static_cast<std::uint8_t>(child.type)); stringValue(name); payload(child);
                }
                u8(0); break;
            }
            case Type::IntArray: {
                const auto& a = std::get<IntArray>(tag.value);
                u32(static_cast<std::uint32_t>(a.size()));
                for (std::int32_t v : a) u32(static_cast<std::uint32_t>(v));
                break;
            }
            case Type::LongArray: {
                const auto& a = std::get<LongArray>(tag.value);
                u32(static_cast<std::uint32_t>(a.size()));
                for (std::int64_t v : a) u64(static_cast<std::uint64_t>(v));
                break;
            }
        }
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::uint8_t u8() { require(1); return bytes_[offset_++]; }
    std::uint16_t u16() { return static_cast<std::uint16_t>((u8() << 8) | u8()); }
    std::uint32_t u32() { return (static_cast<std::uint32_t>(u16()) << 16) | u16(); }
    std::uint64_t u64() { return (static_cast<std::uint64_t>(u32()) << 32) | u32(); }
    std::string stringValue() {
        const std::size_t length = u16(); require(length);
        std::string out(reinterpret_cast<const char*>(bytes_.data() + offset_), length); offset_ += length; return out;
    }
    Tag payload(Type type) {
        switch (type) {
            case Type::End: return Tag{};
            case Type::Byte: return Tag(static_cast<std::int8_t>(u8()));
            case Type::Short: return Tag(static_cast<std::int16_t>(u16()));
            case Type::Int: return Tag(static_cast<std::int32_t>(u32()));
            case Type::Long: return Tag(static_cast<std::int64_t>(u64()));
            case Type::Float: return Tag(std::bit_cast<float>(u32()));
            case Type::Double: return Tag(std::bit_cast<double>(u64()));
            case Type::ByteArray: {
                const std::uint32_t length = checkedLength();
                ByteArray out; out.reserve(length);
                for (std::uint32_t i = 0; i < length; ++i) out.push_back(static_cast<std::int8_t>(u8()));
                return Tag(std::move(out));
            }
            case Type::String: return Tag(stringValue());
            case Type::List: {
                const Type element = static_cast<Type>(u8());
                const std::uint32_t length = checkedLength();
                List out; out.reserve(length);
                for (std::uint32_t i = 0; i < length; ++i) out.push_back(payload(element));
                return Tag(element, std::move(out));
            }
            case Type::Compound: {
                Compound out;
                for (;;) {
                    const Type childType = static_cast<Type>(u8()); if (childType == Type::End) break;
                    const std::string name = stringValue(); out.insert_or_assign(name, payload(childType));
                }
                return Tag(std::move(out));
            }
            case Type::IntArray: {
                const std::uint32_t length = checkedLength();
                IntArray out; out.reserve(length);
                for (std::uint32_t i = 0; i < length; ++i) out.push_back(static_cast<std::int32_t>(u32()));
                return Tag(std::move(out));
            }
            case Type::LongArray: {
                const std::uint32_t length = checkedLength();
                LongArray out; out.reserve(length);
                for (std::uint32_t i = 0; i < length; ++i) out.push_back(static_cast<std::int64_t>(u64()));
                return Tag(std::move(out));
            }
        }
        throw std::runtime_error("Unknown NBT tag type");
    }
private:
    void require(std::size_t count) const {
        if (offset_ > bytes_.size() || count > bytes_.size() - offset_)
            throw std::runtime_error("Truncated NBT payload");
    }
    std::uint32_t checkedLength() { const std::uint32_t length=u32(); if (length > 64U*1024U*1024U) throw std::runtime_error("NBT array/list is unreasonably large"); return length; }
    const std::vector<std::uint8_t>& bytes_; std::size_t offset_ = 0;
};

std::vector<std::uint8_t> deflateBytes(const std::vector<std::uint8_t>& input, int windowBits) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("Could not initialize zlib deflater");
    std::vector<std::uint8_t> out(std::max<std::size_t>(256, input.size()/2 + 64));
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    int result = Z_OK;
    while (result == Z_OK) {
        if (stream.total_out == out.size()) out.resize(out.size()*2);
        stream.next_out = reinterpret_cast<Bytef*>(out.data()+stream.total_out);
        stream.avail_out = static_cast<uInt>(out.size()-stream.total_out);
        result = deflate(&stream, Z_FINISH);
    }
    if (result != Z_STREAM_END) { deflateEnd(&stream); throw std::runtime_error("zlib deflate failed"); }
    out.resize(stream.total_out); deflateEnd(&stream); return out;
}

std::vector<std::uint8_t> inflateBytes(const std::vector<std::uint8_t>& input, int windowBits, std::size_t hint) {
    z_stream stream{};
    if (inflateInit2(&stream, windowBits) != Z_OK) throw std::runtime_error("Could not initialize zlib inflater");
    std::vector<std::uint8_t> out(std::max<std::size_t>(256, hint));
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    int result = Z_OK;
    while (result == Z_OK) {
        if (stream.total_out == out.size()) out.resize(out.size()*2);
        stream.next_out = reinterpret_cast<Bytef*>(out.data()+stream.total_out);
        stream.avail_out = static_cast<uInt>(out.size()-stream.total_out);
        result = inflate(&stream, Z_NO_FLUSH);
    }
    if (result != Z_STREAM_END) { inflateEnd(&stream); throw std::runtime_error("zlib inflate failed"); }
    out.resize(stream.total_out); inflateEnd(&stream); return out;
}

} // namespace

Compound& Tag::compound() { if (type != Type::Compound) throw std::runtime_error("NBT tag is not a compound"); return std::get<Compound>(value); }
const Compound& Tag::compound() const { if (type != Type::Compound) throw std::runtime_error("NBT tag is not a compound"); return std::get<Compound>(value); }
List& Tag::list() { if (type != Type::List) throw std::runtime_error("NBT tag is not a list"); return std::get<List>(value); }
const List& Tag::list() const { if (type != Type::List) throw std::runtime_error("NBT tag is not a list"); return std::get<List>(value); }

std::vector<std::uint8_t> encode(const Document& document) {
    if (document.root.type != Type::Compound) throw std::runtime_error("NBT root must be a compound");
    Writer writer; writer.u8(static_cast<std::uint8_t>(Type::Compound)); writer.stringValue(document.name); writer.payload(document.root); return writer.take();
}

Document decode(const std::vector<std::uint8_t>& bytes) {
    Reader reader(bytes); const Type type=static_cast<Type>(reader.u8()); if (type != Type::Compound) throw std::runtime_error("NBT root is not a compound");
    Document document; document.name=reader.stringValue(); document.root=reader.payload(type); return document;
}

std::vector<std::uint8_t> zlibCompress(const std::vector<std::uint8_t>& bytes) { return deflateBytes(bytes, MAX_WBITS); }
std::vector<std::uint8_t> zlibDecompress(const std::vector<std::uint8_t>& bytes, std::size_t hint) { return inflateBytes(bytes, MAX_WBITS, hint); }
std::vector<std::uint8_t> gzipCompress(const std::vector<std::uint8_t>& bytes) { return deflateBytes(bytes, MAX_WBITS+16); }
std::vector<std::uint8_t> gzipDecompress(const std::vector<std::uint8_t>& bytes) { return inflateBytes(bytes, MAX_WBITS+32, 65536); }

void writeGzipFile(const std::filesystem::path& path, const Document& document) {
    std::filesystem::create_directories(path.parent_path()); const auto compressed=gzipCompress(encode(document));
    const auto temp=path.string()+".tmp"; std::ofstream out(temp, std::ios::binary|std::ios::trunc); if(!out) throw std::runtime_error("Could not write "+temp);
    out.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size())); out.close();
    std::error_code ec; std::filesystem::remove(path.string()+"_old",ec); if(std::filesystem::exists(path)) std::filesystem::rename(path,path.string()+"_old",ec);
    std::filesystem::rename(temp,path);
}

Document readGzipFile(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); if(!in) throw std::runtime_error("Could not read "+path.string());
    const std::vector<std::uint8_t> compressed((std::istreambuf_iterator<char>(in)),{}); return decode(gzipDecompress(compressed));
}

const Tag* find(const Compound& c, std::string_view key){ auto it=c.find(key); return it==c.end()?nullptr:&it->second; }
Tag* find(Compound& c, std::string_view key){ auto it=c.find(key); return it==c.end()?nullptr:&it->second; }
std::int64_t integer(const Compound& c,std::string_view key,std::int64_t fallback){ const Tag* t=find(c,key); if(!t)return fallback; switch(t->type){case Type::Byte:return std::get<std::int8_t>(t->value);case Type::Short:return std::get<std::int16_t>(t->value);case Type::Int:return std::get<std::int32_t>(t->value);case Type::Long:return std::get<std::int64_t>(t->value);default:return fallback;} }
double number(const Compound& c,std::string_view key,double fallback){ const Tag* t=find(c,key); if(!t)return fallback; if(t->type==Type::Float)return std::get<float>(t->value); if(t->type==Type::Double)return std::get<double>(t->value); return static_cast<double>(integer(c,key,static_cast<std::int64_t>(fallback))); }
std::string string(const Compound& c,std::string_view key,std::string fallback){ const Tag* t=find(c,key); return t&&t->type==Type::String?std::get<std::string>(t->value):fallback; }
bool boolean(const Compound& c,std::string_view key,bool fallback){ const Tag* t=find(c,key); return t&&t->type==Type::Byte?std::get<std::int8_t>(t->value)!=0:fallback; }

} // namespace nbt
