#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include "http_client.hpp"
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace s3d::premiere {
namespace {
struct Handle {HINTERNET value{};~Handle(){if(value)WinHttpCloseHandle(value);}};
struct Completion {
    std::mutex mutex;std::condition_variable condition;
    DWORD status{},error{},read{};bool closed{};
    static void CALLBACK callback(HINTERNET,DWORD_PTR context,DWORD event,void* data,DWORD size) noexcept {
        if(!context)return;
        auto& self=*reinterpret_cast<Completion*>(context);
        std::lock_guard lock(self.mutex);
        if(event==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)self.closed=true;
        else if(event==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)self.error=static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError;
        else if(event==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||event==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE||event==WINHTTP_CALLBACK_STATUS_READ_COMPLETE){
            self.status=event;if(event==WINHTTP_CALLBACK_STATUS_READ_COMPLETE)self.read=size;
        }
        self.condition.notify_all();
    }
    void wait(DWORD expected,std::chrono::steady_clock::time_point deadline,const CancelProbe& cancel,bool sent) {
        std::unique_lock lock(mutex);
        while(true) {
            lock.unlock();if(cancel&&cancel())throw RenderCancelled();lock.lock();
            if(error)throw HttpError(error,sent&&error!=ERROR_WINHTTP_CANNOT_CONNECT);
            if(status==expected){status=0;return;}
            if(std::chrono::steady_clock::now()>=deadline)throw HttpError(ERROR_WINHTTP_TIMEOUT,sent);
            condition.wait_for(lock,std::chrono::milliseconds(25));
        }
    }
};
struct Request {
    Completion completion;
    HINTERNET value{};
    bool callback_ready{};
    ~Request(){
        if(!value)return;
        // All WinHTTP API calls have returned; pending asynchronous I/O may be cancelled.
        WinHttpCloseHandle(value);
        if(callback_ready){std::unique_lock lock(completion.mutex);completion.condition.wait(lock,[&]{return completion.closed;});}
    }
};
}
HttpResponse local_request(unsigned short port,const wchar_t* path,const std::wstring& headers,
                          const std::vector<unsigned char>& payload,int timeout_ms,const CancelProbe& cancel) {
    if(cancel&&cancel())throw RenderCancelled();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    Handle session{WinHttpOpen(L"StereoDepth/0.3.1",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,WINHTTP_FLAG_ASYNC)};
    if(!session.value)throw HttpError(GetLastError(),false);
    WinHttpSetTimeouts(session.value,2000,2000,timeout_ms,timeout_ms);
    Handle connection{WinHttpConnect(session.value,L"127.0.0.1",port,0)};
    if(!connection.value)throw HttpError(GetLastError(),false);
    // Read buffer and request payload outlive HANDLE_CLOSING (cancellation completion).
    std::array<unsigned char,65536> read_buffer{};
    Request req;req.value=WinHttpOpenRequest(connection.value,L"POST",path,nullptr,nullptr,nullptr,0);
    if(!req.value)throw HttpError(GetLastError(),false);
    DWORD_PTR context=reinterpret_cast<DWORD_PTR>(&req.completion);
    if(!WinHttpSetOption(req.value,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)))throw HttpError(GetLastError(),false);
    if(WinHttpSetStatusCallback(req.value,Completion::callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)
        throw HttpError(GetLastError(),false);
    req.callback_ready=true;
    if(!WinHttpSendRequest(req.value,headers.c_str(),DWORD(-1),const_cast<unsigned char*>(payload.data()),DWORD(payload.size()),DWORD(payload.size()),context))
        throw HttpError(GetLastError(),false);
    // Once queued, conservatively treat a timeout as possibly transmitted.
    // Connection-refused is the only completion that proves it was not sent.
    req.completion.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,deadline,cancel,true);
    if(!WinHttpReceiveResponse(req.value,nullptr))throw HttpError(GetLastError(),true);
    req.completion.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,deadline,cancel,true);
    HttpResponse response;DWORD size=sizeof(response.status);
    if(!WinHttpQueryHeaders(req.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&response.status,&size,nullptr))throw HttpError(GetLastError(),true);
    DWORD expected{},expected_size=sizeof(expected);
    bool has_length=WinHttpQueryHeaders(req.value,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&expected,&expected_size,nullptr)!=0;
    constexpr size_t limit=128*1024*1024;
    if(has_length){if(expected>limit)throw std::runtime_error("Oversized depth worker response");response.body.reserve(expected);}
    while(true) {
        if(!WinHttpReadData(req.value,read_buffer.data(),DWORD(read_buffer.size()),nullptr))throw HttpError(GetLastError(),true);
        req.completion.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,deadline,cancel,true);
        auto count=req.completion.read;if(!count)break;
        if(response.body.size()+count>limit)throw std::runtime_error("Oversized depth worker response");
        response.body.insert(response.body.end(),read_buffer.begin(),read_buffer.begin()+count);
    }
    if(has_length&&response.body.size()!=expected)throw std::runtime_error("Incomplete depth worker response");
    return response;
}
}
