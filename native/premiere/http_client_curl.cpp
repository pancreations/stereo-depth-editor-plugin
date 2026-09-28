#include "http_client.hpp"
#include <curl/curl.h>
#include <memory>
#include <sstream>
#include <exception>

namespace s3d::premiere {
namespace {
struct Transfer {
  HttpResponse response;
  const CancelProbe& cancel;
  bool cancelled=false;
  std::exception_ptr error;
  static size_t write(char* data,size_t size,size_t count,void* context) noexcept {
    auto& self=*static_cast<Transfer*>(context);
    const auto bytes=size*count;
    try {
      if(bytes>256*1024*1024-self.response.body.size())return 0;
      self.response.body.insert(self.response.body.end(),data,data+bytes);
      return bytes;
    }catch(...){self.error=std::current_exception();return 0;}
  }
  static int progress(void* context,curl_off_t,curl_off_t,curl_off_t,curl_off_t) noexcept {
    auto& self=*static_cast<Transfer*>(context);
    try {self.cancelled=self.cancel && self.cancel();return self.cancelled?1:0;}
    catch(...){self.error=std::current_exception();return 1;}
  }
};
}
HttpResponse local_request(unsigned short port,const wchar_t* path,const std::wstring& headers,
                          const std::vector<unsigned char>& payload,int timeout_ms,const CancelProbe& cancel) {
  if(cancel&&cancel())throw RenderCancelled();
  static const auto initialized=curl_global_init(CURL_GLOBAL_DEFAULT);
  if(initialized!=CURLE_OK)throw HttpError(initialized,false);
  std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> handle(curl_easy_init(),curl_easy_cleanup);
  if(!handle)throw std::bad_alloc();
  // All callers supply ASCII API paths and bearer headers; media paths are UTF-8 in payloads.
  std::wstring wpath(path);
  std::string url="http://127.0.0.1:"+std::to_string(port)+std::string(wpath.begin(),wpath.end());
  curl_slist* list=nullptr;
  std::istringstream lines(std::string(headers.begin(),headers.end()));
  std::string line;
  while(std::getline(lines,line)) {
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    auto* next=curl_slist_append(list,line.c_str());
    if(!next){curl_slist_free_all(list);throw std::bad_alloc();}list=next;
  }
  std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> owned(list,curl_slist_free_all);
  Transfer transfer{{},cancel};
  auto* h=handle.get();
  curl_easy_setopt(h,CURLOPT_URL,url.c_str());
  curl_easy_setopt(h,CURLOPT_PROXY,"");
  curl_easy_setopt(h,CURLOPT_NOSIGNAL,1L);
  curl_easy_setopt(h,CURLOPT_CONNECTTIMEOUT_MS,2000L);
  curl_easy_setopt(h,CURLOPT_TIMEOUT_MS,long(timeout_ms));
  curl_easy_setopt(h,CURLOPT_HTTPHEADER,list);
  curl_easy_setopt(h,CURLOPT_POSTFIELDS,payload.data());
  curl_easy_setopt(h,CURLOPT_POSTFIELDSIZE_LARGE,curl_off_t(payload.size()));
  curl_easy_setopt(h,CURLOPT_WRITEFUNCTION,Transfer::write);
  curl_easy_setopt(h,CURLOPT_WRITEDATA,&transfer);
  curl_easy_setopt(h,CURLOPT_NOPROGRESS,0L);
  curl_easy_setopt(h,CURLOPT_XFERINFOFUNCTION,Transfer::progress);
  curl_easy_setopt(h,CURLOPT_XFERINFODATA,&transfer);
  std::unique_ptr<CURLM,decltype(&curl_multi_cleanup)> multi(curl_multi_init(),curl_multi_cleanup);
  if(!multi)throw std::bad_alloc();
  curl_multi_add_handle(multi.get(),h);
  CURLcode result=CURLE_FAILED_INIT;
  int running=0;
  try {
    do {
      if(cancel&&cancel()){transfer.cancelled=true;break;}
      if(curl_multi_perform(multi.get(),&running)!=CURLM_OK)
        throw std::runtime_error("Depth worker HTTP transport failed");
      if(running && curl_multi_poll(multi.get(),nullptr,0,25,nullptr)!=CURLM_OK)
        throw std::runtime_error("Depth worker HTTP polling failed");
    }while(running);
    int remaining=0;
    while(auto* message=curl_multi_info_read(multi.get(),&remaining))
      if(message->msg==CURLMSG_DONE)result=message->data.result;
  }catch(...){curl_multi_remove_handle(multi.get(),h);throw;}
  curl_multi_remove_handle(multi.get(),h);
  if(transfer.error)std::rethrow_exception(transfer.error);
  if(transfer.cancelled)throw RenderCancelled();
  if(result!=CURLE_OK)throw HttpError(result,result!=CURLE_COULDNT_CONNECT);
  long status=0;curl_easy_getinfo(h,CURLINFO_RESPONSE_CODE,&status);
  transfer.response.status=static_cast<unsigned long>(status);
  return std::move(transfer.response);
}
}
