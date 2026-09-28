#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace s3d {
inline std::filesystem::path read_runtime_location(const std::filesystem::path& config) {
  if(!std::filesystem::exists(config))return {};
  std::ifstream stream(config,std::ios::binary);
  std::string marker,line,extra;
  if(!std::getline(stream,marker)||!std::getline(stream,line))
    throw std::runtime_error("Cannot read StereoDepth runtime location");
  if(!marker.empty()&&marker.back()=='\r')marker.pop_back();
  if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(marker!="StereoDepth runtime v1"||line.empty()||line.find('\0')!=std::string::npos||std::getline(stream,extra))
    throw std::runtime_error("Invalid StereoDepth runtime location file");
  auto root=std::filesystem::u8path(line);
  if(!root.is_absolute()||!std::filesystem::is_regular_file(root/"pyproject.toml")||
      !std::filesystem::is_directory(root/"worker/stereodepth_worker"))
    throw std::runtime_error("StereoDepth runtime folder moved or is incomplete. Run Install.ps1 again.");
  return root;
}
inline std::filesystem::path windows_runtime_location() {
  // Resolve the module containing this code, not Premiere.exe or the working directory.
  static const char module_anchor=0;
  HMODULE module=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&module_anchor),&module))
    throw std::runtime_error("Cannot locate StereoDepth module");
  std::wstring filename(32768,L'\0');
  const auto length=GetModuleFileNameW(module,filename.data(),static_cast<DWORD>(filename.size()));
  if(!length||length>=filename.size())throw std::runtime_error("Cannot locate StereoDepth module path");
  filename.resize(length);
  return read_runtime_location(std::filesystem::path(filename).parent_path()/"StereoDepth.runtime.txt");
}
}
