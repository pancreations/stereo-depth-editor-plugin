#include "s3d/pipeline.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace s3d {
static std::uint32_t crc32(const unsigned char* data,std::size_t n) {
  std::uint32_t crc=0xffffffff;
  for(std::size_t i=0;i<n;++i) {
    crc^=data[i];
    for(int k=0;k<8;++k) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
  }
  return ~crc;
}
Depth read_depth_cache(const std::filesystem::path& path) {
  static_assert(std::endian::native==std::endian::little,"Cache format is little-endian");
  std::ifstream file(path,std::ios::binary);
  if(!file) throw std::runtime_error("Depth cache frame missing: generate the cache before export");
  std::array<std::uint32_t,6> h{};
  file.read(reinterpret_cast<char*>(h.data()),sizeof(h));
  if(!file || std::memcmp(h.data(),"S3D1",4)!=0 || h[1]!=1 || h[4]!=1)
    throw std::runtime_error("Invalid depth cache header");
  Depth out(static_cast<int>(h[2]),static_cast<int>(h[3]));
  const auto bytes=out.pixels.size()*sizeof(float);
  if(std::filesystem::file_size(path)!=sizeof(h)+bytes) throw std::runtime_error("Truncated or oversized depth cache");
  file.read(reinterpret_cast<char*>(out.pixels.data()),static_cast<std::streamsize>(bytes));
  if(!file || crc32(reinterpret_cast<unsigned char*>(out.pixels.data()),bytes)!=h[5])
    throw std::runtime_error("Depth cache checksum failed");
  for(float v:out.pixels) if(!std::isfinite(v)) throw std::runtime_error("Non-finite cache depth");
  return out;
}
CacheBinding read_binding(const std::filesystem::path& root,int slot) {
  if(slot<1 || slot>999999) throw std::runtime_error("Choose a valid depth cache slot in the companion panel");
  std::ifstream file(root/"bindings"/(std::to_string(slot)+".txt"));
  std::string magic; CacheBinding b;int external=0;
  file>>magic>>b.key>>b.fps_num>>b.fps_den>>b.frames>>b.width>>b.height>>external>>b.low>>b.high;
  if(!file || magic!="S3DB1" || b.key.size()!=64 ||
     b.key.find_first_not_of("0123456789abcdef")!=std::string::npos ||
     b.fps_num<=0 || b.fps_den<=0 || b.frames<=0 || !std::isfinite(b.low) ||
     !std::isfinite(b.high) || b.high<=b.low || (external!=0&&external!=1))
    throw std::runtime_error("Missing or invalid depth cache binding");
  checked_size(b.width,b.height);b.external=external==1;return b;
}
std::filesystem::path cache_frame_path(const std::filesystem::path& root,const CacheBinding& b,std::int64_t frame) {
  if(b.key.size()!=64 || b.key.find_first_not_of("0123456789abcdef")!=std::string::npos)
    throw std::invalid_argument("Invalid cache identity");
  if(frame<0 || frame>=b.frames) throw std::runtime_error("Depth frame outside cache: check frame offset and time mapping");
  std::ostringstream name;name<<std::setw(9)<<std::setfill('0')<<frame<<".s3d";
  return root/"entries"/b.key/name.str();
}
std::int64_t frame_at_time(std::int64_t t,std::int64_t scale,int num,int den) {
  if(scale<=0 || num<=0 || den<=0) throw std::invalid_argument("Invalid rational time base");
  // long double avoids intermediate integer overflow for Premiere ticks.
  long double value=static_cast<long double>(t)*num/(static_cast<long double>(scale)*den);
  if(value<std::numeric_limits<std::int64_t>::min() || value>std::numeric_limits<std::int64_t>::max()-1.0L)
    throw std::invalid_argument("Frame time overflow");
  return static_cast<std::int64_t>(std::floor(value+1e-8L));
}
}
