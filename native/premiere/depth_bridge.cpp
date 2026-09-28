#include "platform.hpp"
#include "depth_bridge.hpp"
#include "runtime_config.hpp"
#include "s3d/pipeline.hpp"
#include <array>
#include <fstream>
#include <mutex>
#include <regex>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstring>

namespace s3d::premiere {
namespace {
std::timed_mutex bridge_mutex;
std::string quote(const std::string& s) {
  std::string out="\"";for(unsigned char c:s) {if(c=='\\'||c=='\"')out+='\\';if(c<32)throw std::runtime_error("Invalid source path");out+=c;}return out+'\"';
}
std::string token() {
  std::ifstream f(s3d::project_root()/"runtime"/"connection.json");
  std::string data((std::istreambuf_iterator<char>(f)),{});std::smatch m;
  if(!std::regex_search(data,m,std::regex("\"token\"\\s*:\\s*\"([A-Za-z0-9_-]{32,})\"")))return {};
  return m[1];
}
std::vector<unsigned char> request(const wchar_t* endpoint,const std::vector<unsigned char>& payload,bool binary=true,const CancelProbe& cancel={}) {
  // One cold-start owner. Worker model sessions and frame files survive individual effect renders.
  std::unique_lock guard(bridge_mutex,std::defer_lock);
  while(!guard.try_lock_for(std::chrono::milliseconds(25)))if(cancel&&cancel())throw RenderCancelled();
  const auto startup_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
  bool launched=false;int auth_retries=0;
  while(std::chrono::steady_clock::now()<startup_deadline) {
    if(cancel&&cancel())throw RenderCancelled();
    const auto auth=token();
    if(!auth.empty()) {
      const auto headers=L"Authorization: Bearer "+wide(auth)+L"\r\nContent-Type: "+(binary?std::wstring(L"application/octet-stream"):std::wstring(L"application/json"));
      try {
        auto response=local_request(48731,endpoint,headers,payload,120000,cancel);
        if(response.status==401&&auth_retries++==0)continue;
        if(response.status!=200)throw std::runtime_error(std::string(response.body.begin(),response.body.end()));
        return std::move(response.body);
      }catch(const HttpError& e){
        // Only connection-refused before sending can trigger worker startup.
        // Never resend an in-flight render after a timeout or spawn more workers.
        if(e.sent||e.code!=connection_refused_code)throw;
      }
    }
    if(!launched){launch_worker();launched=true;}
    for(int n=0;n<8;++n){if(cancel&&cancel())throw RenderCancelled();std::this_thread::sleep_for(std::chrono::milliseconds(25));}
  }
  throw std::runtime_error("Local depth worker did not start. Check runtime and Python dependencies.");
}
PreparedDepth decode(const std::vector<unsigned char>& bytes) {
  if(bytes.size()<24||std::memcmp(bytes.data(),"SDD1",4))throw std::runtime_error("Invalid depth response");
  unsigned w,h,hit;float low,high;
  std::memcpy(&w,bytes.data()+4,4);std::memcpy(&h,bytes.data()+8,4);
  std::memcpy(&low,bytes.data()+12,4);std::memcpy(&high,bytes.data()+16,4);std::memcpy(&hit,bytes.data()+20,4);
  PreparedDepth out{Depth(int(w),int(h)),low,high,hit!=0};
  if(bytes.size()!=24+out.depth.pixels.size()*4||!std::isfinite(low)||!std::isfinite(high)||high<=low)throw std::runtime_error("Invalid depth response size/range");
  std::memcpy(out.depth.pixels.data(),bytes.data()+24,bytes.size()-24);
  for(float v:out.depth.pixels)finite(v);return out;
}
}
PreparedDepth automatic_depth(const Image& image,int model,int quality,const std::string& source,const CancelProbe& cancel) {
  static const int sizes[]={280,518,756,1036};
  if(model<0||model>=17||quality<0||quality>3)throw std::runtime_error("Invalid AI model or quality");
  float factor=float(sizes[quality])/std::max(image.width,image.height);
  auto guide=resize(image,std::max(1,int(std::round(image.width*factor))),std::max(1,int(std::round(image.height*factor))));
  std::array<unsigned,6> header{0x31524453u,unsigned(guide.width),unsigned(guide.height),unsigned(model),unsigned(quality),unsigned(source.size())};
  if(source.size()>32768)throw std::runtime_error("Source identifier too long");
  std::vector<unsigned char> payload(24+source.size()+guide.pixels.size()*3);
  std::memcpy(payload.data(),header.data(),24);std::memcpy(payload.data()+24,source.data(),source.size());
  auto* rgb=payload.data()+24+source.size();
  for(auto p:guide.pixels){*rgb++=static_cast<unsigned char>(clamp01(p.r)*255+.5f);*rgb++=static_cast<unsigned char>(clamp01(p.g)*255+.5f);*rgb++=static_cast<unsigned char>(clamp01(p.b)*255+.5f);}
  return decode(request(L"/v1/render/depth",payload,true,cancel));
}
PreparedDepth external_depth(int id,std::int64_t time,std::int64_t scale,int offset,int fps_num,int fps_den,const CancelProbe& cancel) {
  std::ostringstream body;body<<"{\"id\":"<<id<<",\"time\":"<<time<<",\"scale\":"<<scale<<",\"offset\":"<<offset<<",\"fps_num\":"<<fps_num<<",\"fps_den\":"<<fps_den<<"}";
  const auto value=body.str();return decode(request(L"/v1/render/external",{value.begin(),value.end()},false,cancel));
}
int register_depth_source(const std::wstring& path,std::string& name,const std::string& import_mode) {
  const auto body="{\"path\":"+quote(utf8(path))+",\"import_mode\":"+quote(import_mode)+"}";
  const auto result=request(L"/v1/render/register-external",{body.begin(),body.end()},false);
  std::string text(result.begin(),result.end());std::smatch match;
  if(!std::regex_search(text,match,std::regex("\"id\"\\s*:\\s*([0-9]+)")))throw std::runtime_error("Failed to register depth source");
  const int id=std::stoi(match[1]);name=external_source_name(id);return id;
}
std::string external_source_name(int id) {
  if(id<=0)return "No source selected";
  const auto body="{\"id\":"+std::to_string(id)+"}";
  const auto result=request(L"/v1/render/external-name",{body.begin(),body.end()},false);
  // This endpoint deliberately returns plain UTF-8 display text, never an executable path.
  return std::string(result.begin(),result.end());
}
}
