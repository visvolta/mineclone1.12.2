#include "save/RegionFile.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>

#include "save/Nbt.hpp"

namespace {
constexpr std::size_t sectorSize = 4096;
constexpr std::size_t headerSize = sectorSize * 2;

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
}
void putBe32(std::uint8_t* p, std::uint32_t v) {
    p[0]=static_cast<std::uint8_t>(v>>24U); p[1]=static_cast<std::uint8_t>(v>>16U);
    p[2]=static_cast<std::uint8_t>(v>>8U); p[3]=static_cast<std::uint8_t>(v);
}
std::uint32_t nowSeconds() {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}
}

int RegionFile::index(int x,int z){ return ((z & 31) << 5) | (x & 31); }

std::vector<std::optional<RegionFile::StoredChunk>> RegionFile::readAll() const {
    std::vector<std::optional<StoredChunk>> result(1024);
    if (!std::filesystem::exists(path_)) return result;
    std::ifstream in(path_,std::ios::binary); if(!in) throw std::runtime_error("Could not open region file: "+path_.string());
    std::array<std::uint8_t,headerSize> header{}; in.read(reinterpret_cast<char*>(header.data()),header.size());
    if(in.gcount()!=static_cast<std::streamsize>(header.size())) throw std::runtime_error("Truncated region header: "+path_.string());
    for(int i=0;i<1024;++i){
        const std::uint8_t* entry=header.data()+i*4;
        const std::uint32_t offset=(static_cast<std::uint32_t>(entry[0])<<16U)|(static_cast<std::uint32_t>(entry[1])<<8U)|entry[2];
        const std::uint32_t count=entry[3]; if(offset<2||count==0) continue;
        in.seekg(static_cast<std::streamoff>(offset)*sectorSize);
        std::array<std::uint8_t,5> prefix{}; in.read(reinterpret_cast<char*>(prefix.data()),5); if(in.gcount()!=5) continue;
        const std::uint32_t length=be32(prefix.data()); if(length<1||length>count*sectorSize-4||prefix[4]!=2) continue;
        StoredChunk chunk; chunk.compressed.resize(length-1); in.read(reinterpret_cast<char*>(chunk.compressed.data()),static_cast<std::streamsize>(chunk.compressed.size()));
        if(in.gcount()!=static_cast<std::streamsize>(chunk.compressed.size())) continue;
        chunk.timestamp=be32(header.data()+sectorSize+i*4); result[static_cast<std::size_t>(i)]=std::move(chunk);
    }
    return result;
}

void RegionFile::rewrite(const std::vector<std::optional<StoredChunk>>& chunks) const {
    std::filesystem::create_directories(path_.parent_path());
    const auto temp=path_.string()+".tmp"; std::ofstream out(temp,std::ios::binary|std::ios::trunc); if(!out) throw std::runtime_error("Could not write region file: "+temp);
    std::array<std::uint8_t,headerSize> header{}; out.write(reinterpret_cast<const char*>(header.data()),header.size());
    std::uint32_t sector=2;
    for(int i=0;i<1024;++i){
        if(!chunks[static_cast<std::size_t>(i)]) continue;
        const StoredChunk& chunk=*chunks[static_cast<std::size_t>(i)];
        const std::uint32_t length=static_cast<std::uint32_t>(chunk.compressed.size()+1);
        const std::uint32_t total=length+4;
        const std::uint32_t sectors=(total+sectorSize-1)/sectorSize;
        if(sector>0xFFFFFFU||sectors>255U) throw std::runtime_error("Chunk is too large for Anvil region file");
        std::uint8_t* entry=header.data()+i*4; entry[0]=static_cast<std::uint8_t>(sector>>16U); entry[1]=static_cast<std::uint8_t>(sector>>8U); entry[2]=static_cast<std::uint8_t>(sector); entry[3]=static_cast<std::uint8_t>(sectors);
        putBe32(header.data()+sectorSize+i*4,chunk.timestamp);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(sectors)*sectorSize,0); putBe32(payload.data(),length); payload[4]=2;
        std::copy(chunk.compressed.begin(),chunk.compressed.end(),payload.begin()+5);
        out.seekp(static_cast<std::streamoff>(sector)*sectorSize); out.write(reinterpret_cast<const char*>(payload.data()),static_cast<std::streamsize>(payload.size())); sector+=sectors;
    }
    out.seekp(0); out.write(reinterpret_cast<const char*>(header.data()),header.size()); out.close();
    std::error_code ec; std::filesystem::rename(temp,path_,ec); if(ec){ std::filesystem::remove(path_,ec); ec.clear(); std::filesystem::rename(temp,path_,ec); if(ec) throw std::runtime_error("Could not replace region file: "+path_.string()); }
}

std::optional<std::vector<std::uint8_t>> RegionFile::readChunk(int localX,int localZ) const {
    auto all=readAll(); const auto& stored=all[static_cast<std::size_t>(index(localX,localZ))]; if(!stored) return std::nullopt;
    return nbt::zlibDecompress(stored->compressed);
}

void RegionFile::writeChunk(int localX,int localZ,const std::vector<std::uint8_t>& nbtBytes){
    auto all=readAll(); StoredChunk chunk; chunk.compressed=nbt::zlibCompress(nbtBytes); chunk.timestamp=nowSeconds(); all[static_cast<std::size_t>(index(localX,localZ))]=std::move(chunk); rewrite(all);
}

bool RegionFile::hasChunk(int localX,int localZ) const { auto all=readAll(); return all[static_cast<std::size_t>(index(localX,localZ))].has_value(); }
