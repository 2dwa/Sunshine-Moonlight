// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#pragma once
#ifdef _WIN32
#include "src/platform/windows/focusclipboard_win.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <wtsapi32.h>

namespace focusclip {
// The service launches Core as SYSTEM in the console session. File clipboard
// paths and received cache files must be accessed as the interactive user.
struct UserContext {
    bool impersonated=false;
    UserContext() {
        HANDLE process_token=nullptr;require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&process_token)!=FALSE,"Cannot query process user");
        BYTE info[1024];DWORD size=0;bool ok=GetTokenInformation(process_token,TokenUser,info,sizeof(info),&size)!=FALSE;CloseHandle(process_token);
        require(ok,"Cannot query process SID");
        if(IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(info)->User.Sid,WinLocalSystemSid)) {
            HANDLE user=nullptr;require(WTSQueryUserToken(WTSGetActiveConsoleSessionId(),&user)!=FALSE,"No interactive user for clipboard");
            impersonated=ImpersonateLoggedOnUser(user)!=FALSE;CloseHandle(user);require(impersonated,"Cannot access user clipboard files");
        }
    }
    ~UserContext(){if(impersonated)RevertToSelf();}
};
// One owner thread for Win32 clipboard ownership; HTTP workers only enqueue work.
class Server {
    std::mutex mutex; std::condition_variable wake;
    std::deque<std::function<void()>> queue; bool stopping=false; std::thread thread;
    std::unique_ptr<Clipboard> clipboard;
    std::unique_ptr<UserContext> user_context;
    Offer current; DWORD observed=0; std::string origin, upload_owner;
    std::unique_ptr<Incoming> upload;
    std::chrono::steady_clock::time_point upload_time;
    void refresh() {
        auto seq=Clipboard::sequence();
        if(!current.id || seq!=observed) {
            auto next=clipboard->read();require(seq==Clipboard::sequence(),"Clipboard changed while reading");
            current=std::move(next);observed=seq;origin.clear();
        }
    }
    std::string process(const std::string& client,const std::string& op,const std::string& body) {
        if(!user_context)user_context=std::make_unique<UserContext>();
        if(!clipboard)clipboard=std::make_unique<Clipboard>();
        if(upload && std::chrono::steady_clock::now()-upload_time>std::chrono::seconds(60))upload.reset();
        if(op=="state") {
            require(body.empty(),"Unexpected state body");refresh();
            if(origin==client){Offer own;own.id=current.id;return encode(own);}return encode(current);
        }
        if(op=="read") {
            Reader r{body};auto id=r.get(8);auto index=r.get(4);auto offset=r.get(8);r.end();
            require(id==current.id && observed==Clipboard::sequence(),"Clipboard offer expired");return read_chunk(current,size_t(index),offset);
        }
        if(op=="begin") {
            require(!upload || upload_owner==client,"Another clipboard transfer is in progress");
            auto offer=decode(body);upload.reset();
            upload=std::make_unique<Incoming>(offer,cache_dir(L"Sunshine-Focus-Sync"),Clipboard::sequence());upload_owner=client;upload_time=std::chrono::steady_clock::now();return {};
        }
        require(upload && upload_owner==client,"No upload for this client");
        Reader r{body};auto id=r.get(8);require(id==upload->offer.id,"Wrong transfer ID");upload_time=std::chrono::steady_clock::now();
        if(op=="cancel") {r.end();upload.reset();return {};}
        if(op=="write") {
            auto index=r.get(4);auto offset=r.get(8);upload->append(size_t(index),offset,body.substr(r.pos));return {};
        }
        require(op=="commit","Unknown clipboard operation");r.end();upload->complete();
        clipboard->write(upload->offer,upload->baseline);observed=Clipboard::sequence();current=upload->offer;origin=client;
        upload->committed=true;upload.reset();std::string result;put(result,current.id,8);return result;
    }
public:
    Server(){thread=std::thread([this]{
        for(;;) {
            std::function<void()> job;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stopping||!queue.empty();});
             if(stopping && queue.empty())break;if(!queue.empty()){job=std::move(queue.front());queue.pop_front();}}
            if(job)job();
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            if(upload && std::chrono::steady_clock::now()-upload_time>std::chrono::seconds(60))upload.reset();
        }
        upload.reset();clipboard.reset();user_context.reset();
    });}
    ~Server(){{std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();thread.join();}
    std::string request(const std::string& client,const std::string& op,const std::string& body) {
        auto task=std::make_shared<std::packaged_task<std::string()>>([=]{return process(client,op,body);});auto future=task->get_future();
        {std::lock_guard<std::mutex> lock(mutex);require(queue.size()<16,"Clipboard queue is full");queue.emplace_back([task]{(*task)();});}wake.notify_one();return future.get();
    }
};
}
#endif
