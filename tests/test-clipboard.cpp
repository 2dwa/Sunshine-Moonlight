// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#include "foundation-sunshine/src/nvhttp/focus_clipboard.h"
#include <iostream>
#include <functional>
using namespace focusclip;
static int cases=0;
void check(bool ok){require(ok,"Test failed");++cases;}
void rejects(std::function<void()> f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
int main(){
    try {
        Offer text;text.id=new_id();text.kind=1;text.text=utf8(L"中文 text 😀\r\nsecond line");
        check(decode(encode(text)).text==text.text);
        for(const std::string bad:{"../outside", "/absolute", "C:/outside", "a/../../x", "a\\b", "x:y", "CON", "a/NUL.txt", "COM1", "LPT9.txt", "a.", "a ", "a/", "a\n"})rejects([&]{safe_relative(bad);});
        rejects([&]{decode(encode(text).substr(0,12));});rejects([&]{decode(encode(text)+"junk");});
        Offer files;files.id=new_id();files.kind=2;files.files={{"folder",0,true},{"folder/a.bin",chunk_bytes*2+19,false},{"folder/empty",0,false},{utf8(L"中文.txt"),5,false}};
        auto manifest=encode(files);check(decode(manifest).files.size()==4);
        auto duplicate=files;duplicate.files.push_back({"FOLDER/A.BIN",1,false});rejects([&]{validate(duplicate);});
        auto conflict=files;conflict.files.push_back({"folder/a.bin/child",1,false});rejects([&]{validate(conflict);});
        auto excessive=files;excessive.files[1].size=max_bytes+1;rejects([&]{validate(excessive);});
        auto cache=fs::absolute("out/tests/clipboard-test-cache");fs::create_directories(cache);
        fs::path discarded;
        {
            Incoming partial(files,cache,0);discarded=partial.directory;
            rejects([&]{partial.complete();});rejects([&]{partial.append(1,1,"x");});rejects([&]{partial.append(0,0,"x");});
            partial.append(1,0,std::string(chunk_bytes,'A'));
            rejects([&]{partial.append(1,0,"x");});
        }
        check(!fs::exists(discarded));
        Incoming transfer(files,cache,0);transfer.append(1,0,std::string(chunk_bytes,'A'));transfer.append(1,chunk_bytes,std::string(chunk_bytes,'B'));transfer.append(1,chunk_bytes*2,std::string(19,'C'));transfer.append(3,0,"12345");transfer.complete();
        check(read_chunk(transfer.offer,1,chunk_bytes)==std::string(chunk_bytes,'B'));
        check(read_chunk(transfer.offer,1,chunk_bytes*2)==std::string(19,'C'));
        rejects([&]{read_chunk(transfer.offer,1,transfer.offer.files[1].size);});
        // Real Win32 clipboard round trips on a private window station. Never
        // touch the user's interactive clipboard, even if setup fails.
        auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
        require(station!=nullptr,"Cannot create isolated test window station");
        require(SetProcessWindowStation(station)!=FALSE,"Cannot select isolated station");
        auto desktop=CreateDesktopW(L"FocusClipboardTest",nullptr,nullptr,0,GENERIC_ALL,nullptr);
        require(desktop && SetThreadDesktop(desktop),"Cannot select isolated test desktop");
        {
            Clipboard clipboard;
            auto initial=Clipboard::sequence();clipboard.write(text,initial);auto read=clipboard.read();check(read.text==text.text);
            {
                Server server;
                auto published=decode(server.request("client-A","state",""));check(published.text==text.text);
                Offer remote=text;remote.id=new_id();remote.text="Remote to local";
                server.request("client-A","begin",encode(remote));std::string id;put(id,remote.id,8);
                rejects([&]{server.request("client-B","commit",id);});
                server.request("client-A","commit",id);
                check(clipboard.read().text==remote.text);
                check(decode(server.request("client-A","state","")).kind==0);
                check(decode(server.request("client-B","state","")).text==remote.text);
                remote.id=new_id();server.request("client-A","begin",encode(remote));
                clipboard.write(text,Clipboard::sequence());id.clear();put(id,remote.id,8);
                rejects([&]{server.request("client-A","commit",id);});
                server.request("client-A","cancel",id);
            }
            rejects([&]{clipboard.write(text,initial);});
            clipboard.write(transfer.offer,Clipboard::sequence());auto copied=clipboard.read();check(copied.kind==2 && copied.files.size()==4);
            bool found=false;for(auto& e:copied.files)if(e.name=="folder/a.bin")found=read_chunk(copied,size_t(&e-copied.files.data()),0)==std::string(chunk_bytes,'A');check(found);
            OpenClipboard(clipboard.window);auto h=GetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"));auto p=static_cast<DWORD*>(GlobalLock(h));check(p && *p==DROPEFFECT_COPY);GlobalUnlock(h);EmptyClipboard();CloseClipboard();
        }
        std::cout<<"PASS "<<cases<<" clipboard checks (protocol, traversal, chunks, cleanup, isolated Win32 text/files)\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<" after "<<cases<<" checks\n";return 1;}
}
