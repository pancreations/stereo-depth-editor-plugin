#pragma once
#include <string>
namespace s3d::premiere {
std::wstring wide(const std::string&);
std::string utf8(const std::wstring&);
void launch_worker();
}
