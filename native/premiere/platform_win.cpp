#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include "platform.hpp"
#include "depth_bridge.hpp"
#include "runtime_config.hpp"
#include <filesystem>
#include <cwctype>

namespace s3d::premiere {
std::wstring wide(const std::string& s) {
  int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring v(n,0);
  MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),v.data(),n);return v;
}
std::string utf8(const std::wstring& s) {
  int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string v(n,0);
  WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),v.data(),n,nullptr,nullptr);return v;
}
void launch_worker() {
  const auto root=s3d::project_root();
  auto exe=root/".venv310"/"Scripts"/"pythonw.exe";
  if(const auto* python=_wgetenv(L"S3D_PYTHON"))exe=python;
  std::wstring command=L"\""+exe.wstring()+L"\" -m stereodepth_worker.server --connection \""+(root/"runtime"/"connection.json").wstring()+L"\" --models \""+(root/"models").wstring()+L"\"";
  STARTUPINFOW info{};info.cb=sizeof(info);PROCESS_INFORMATION process{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&info,&process))
    throw std::runtime_error("Cannot start local depth worker. Run scripts/start-worker.ps1.");
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
}
int browse_depth_source(std::string& name) {
  wchar_t path[32768]={};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();
  dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrTitle=L"Choose depth video or first image in depth sequence";
  dialog.lpstrFilter=L"Depth images and video\0*.png;*.exr;*.tif;*.tiff;*.pfm;*.mov;*.mp4;*.mkv;*.avi\0All files\0*.*\0";
  dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
  if(!GetOpenFileNameW(&dialog))return 0;
  auto extension=std::filesystem::path(path).extension().wstring();
  std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return wchar_t(towlower(c));});
  std::string mode="auto";
  if(extension==L".png"||extension==L".tif"||extension==L".tiff"||extension==L".exr"||extension==L".pfm") {
    const int choice=MessageBoxW(dialog.hwndOwner,
      L"Import as an image sequence?\n\nYes: use this image as the first frame and load the following numbered images at the clip frame rate.\nNo: use only this image as a still depth pass.\nCancel: keep the current depth source.",
      L"Import External Depth",MB_YESNOCANCEL|MB_ICONQUESTION);
    if(choice==IDCANCEL)return 0;
    mode=choice==IDYES?"sequence":"still";
  }
  return register_depth_source(path,name,mode);
}
}
