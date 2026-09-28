#pragma once
#include "s3d/types.hpp"
#include <string>
#include "http_client.hpp"
namespace s3d::premiere {
struct PreparedDepth {Depth depth;float low=0,high=1;bool cache_hit=false;};
PreparedDepth automatic_depth(const Image&,int model,int quality,const std::string& source,const CancelProbe& cancel={});
PreparedDepth external_depth(int id,std::int64_t time,std::int64_t scale,int offset,int fps_num,int fps_den,const CancelProbe& cancel={});
int browse_depth_source(std::string& display_name);
int register_depth_source(const std::wstring& path,std::string& display_name,const std::string& import_mode="auto");
std::string external_source_name(int id);
}
