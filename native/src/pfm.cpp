#include "s3d/pipeline.hpp"
#include <bit>
#include <fstream>
#include <string>

namespace s3d {
struct PFM {int w,h,channels;std::vector<float> data;};
static PFM read(const std::filesystem::path& path) {
  std::ifstream f(path,std::ios::binary);std::string magic;int w=0,h=0;float scale=0;
  f>>magic>>w>>h>>scale;
  if(!f || (magic!="PF" && magic!="Pf") || scale>=0 || std::endian::native!=std::endian::little)
    throw std::runtime_error("Expected little-endian PFM image");
  int channels=magic=="PF"?3:1;
  PFM p{w,h,channels,std::vector<float>(checked_size(w,h)*channels)};
  char newline;f.get(newline);if(newline=='\r') f.get(newline);
  if(newline!='\n') throw std::runtime_error("Invalid PFM header");
  f.read(reinterpret_cast<char*>(p.data.data()),std::streamsize(p.data.size()*4));
  if(!f) throw std::runtime_error("Truncated PFM image");
  for(float& v:p.data) v*=std::abs(scale);
  return p;
}
Image read_pfm_image(const std::filesystem::path& path) {
  auto p=read(path);Image out(p.w,p.h);
  for(int y=0;y<p.h;++y) for(int x=0;x<p.w;++x) {
    auto i=(std::size_t(p.h-1-y)*p.w+x)*p.channels;
    out.at(x,y)={p.data[i],p.data[i+(p.channels==3?1:0)],p.data[i+(p.channels==3?2:0)],1};
  }
  return out;
}
Depth read_pfm_depth(const std::filesystem::path& path) {
  auto p=read(path);if(p.channels!=1) throw std::runtime_error("Depth PFM must be grayscale");
  Depth out(p.w,p.h);
  for(int y=0;y<p.h;++y) std::copy_n(&p.data[std::size_t(p.h-1-y)*p.w],p.w,&out.at(0,y));
  return out;
}
void write_pfm(const Image& p,const std::filesystem::path& path) {
  p.validate();std::ofstream f(path,std::ios::binary);f<<"PF\n"<<p.width<<" "<<p.height<<"\n-1.0\n";
  for(int y=p.height-1;y>=0;--y) for(int x=0;x<p.width;++x) {
    Pixel v=p.at(x,y);float rgb[]={v.r,v.g,v.b};f.write(reinterpret_cast<char*>(rgb),sizeof(rgb));
  }
  if(!f) throw std::runtime_error("Cannot write PFM image");
}
void write_pfm(const Depth& p,const std::filesystem::path& path) {
  p.validate();std::ofstream f(path,std::ios::binary);f<<"Pf\n"<<p.width<<" "<<p.height<<"\n-1.0\n";
  for(int y=p.height-1;y>=0;--y) f.write(reinterpret_cast<const char*>(&p.at(0,y)),p.width*sizeof(float));
  if(!f) throw std::runtime_error("Cannot write depth PFM");
}
}
