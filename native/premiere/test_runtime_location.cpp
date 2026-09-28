#include "s3d/runtime_location.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv) {
  try {
    if(argc!=2)throw std::runtime_error("Supply expected runtime directory or --missing/--invalid");
    if(std::wstring(argv[1])==L"--invalid") {
      try { s3d::windows_runtime_location(); }
      catch(const std::exception&) { return 0; }
      throw std::runtime_error("Invalid runtime location was accepted");
    }
    auto actual=s3d::windows_runtime_location();
    if(std::wstring(argv[1])==L"--missing") {
      if(!actual.empty())throw std::runtime_error("Unexpected runtime location");
    } else if(actual!=std::filesystem::path(argv[1]))throw std::runtime_error("Incorrect module-relative runtime location");
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
