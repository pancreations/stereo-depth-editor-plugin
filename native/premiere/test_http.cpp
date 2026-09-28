#include "http_client.hpp"
#include <chrono>
#include <iostream>
using namespace s3d::premiere;
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("Supply test server port");
        auto port=static_cast<unsigned short>(std::stoi(argv[1]));
        std::vector<unsigned char> payload{'t','e','s','t'};
        const std::wstring headers=L"Content-Type: application/octet-stream";
        auto good=local_request(port,L"/ok",headers,payload,2000);
        if(good.status!=200||good.body!=payload)throw std::runtime_error("Async response corrupted");
        for(auto endpoint:{L"/slow-headers",L"/slow-body"})for(int n=0;n<10;++n) {
            auto start=std::chrono::steady_clock::now();
            bool cancelled=false;
            try{local_request(port,endpoint,headers,payload,5000,[&]{return std::chrono::steady_clock::now()-start>std::chrono::milliseconds(30);});}
            catch(const RenderCancelled&){cancelled=true;}
            if(!cancelled||std::chrono::steady_clock::now()-start>std::chrono::seconds(1))throw std::runtime_error("Render cancellation stalled");
        }
        bool timed_out=false;
        try{local_request(port,L"/slow-headers",headers,payload,150);}catch(const HttpError& e){timed_out=e.sent&&e.code==timeout_code;if(!timed_out)std::cerr<<"Timeout code "<<e.code<<" sent "<<e.sent<<'\n';}
        if(!timed_out)throw std::runtime_error("Timeout not identified as an already-sent render");
        bool incomplete=false;
        try{local_request(port,L"/truncated",headers,payload,2000);}catch(const std::runtime_error&){incomplete=true;}
        if(!incomplete)throw std::runtime_error("Truncated response accepted");
        good=local_request(port,L"/ok",headers,payload,2000);
        if(good.body!=payload)throw std::runtime_error("Request did not recover after cancellation");
        std::cout<<"Async HTTP body integrity, 20 pending-I/O cancellations, timeout and recovery passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
