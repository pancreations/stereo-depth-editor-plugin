#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace s3d::premiere {
#ifdef _WIN32
inline constexpr unsigned long connection_refused_code=12029;
inline constexpr unsigned long timeout_code=12002;
#else
inline constexpr unsigned long connection_refused_code=7; // CURLE_COULDNT_CONNECT
inline constexpr unsigned long timeout_code=28; // CURLE_OPERATION_TIMEDOUT
#endif
using CancelProbe=std::function<bool()>;
struct RenderCancelled:std::runtime_error {RenderCancelled():std::runtime_error("Render cancelled"){};};
struct HttpError:std::runtime_error {
    unsigned long code;bool sent;
    HttpError(unsigned long c,bool s):std::runtime_error("Depth worker connection failed (transport error "+std::to_string(c)+")"),code(c),sent(s){}
};
struct HttpResponse {unsigned long status{};std::vector<unsigned char> body;};
HttpResponse local_request(unsigned short port,const wchar_t* path,const std::wstring& headers,
                          const std::vector<unsigned char>& body,int timeout_ms,const CancelProbe& cancel={});
}
