// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
// Shared by the Focus builds of Moonlight and Sunshine. C++17 / Windows.
// Wire format is explicitly little endian, never native structs or remote paths.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace focusclip {
namespace fs = std::filesystem;
constexpr uint64_t max_bytes = 10ull * 1024 * 1024 * 1024;
constexpr size_t chunk_bytes = 256 * 1024;
constexpr size_t max_text = 8 * 1024 * 1024;
constexpr size_t max_entries = 4096;
inline void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    require(n > 0, "Invalid UTF-8"); std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), r.data(), n); return r;
}
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    require(n > 0, "Invalid Unicode"); std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), r.data(), n, nullptr, nullptr); return r;
}
inline std::string path_utf8(const fs::path& p) { return utf8(p.generic_wstring()); }
inline uint64_t new_id() { GUID g{}; require(SUCCEEDED(CoCreateGuid(&g)), "Cannot create transfer ID"); uint64_t v; std::memcpy(&v, &g, 8); return v ? v : 1; }
inline void put(std::string& s, uint64_t n, unsigned bytes) { for (unsigned i=0;i<bytes;i++) s.push_back(char(n >> (8*i))); }
inline void putstr(std::string& s, const std::string& v) { put(s, v.size(), 4); s += v; }
struct Reader {
    const std::string& data; size_t pos = 0;
    uint64_t get(unsigned bytes) { require(bytes <= 8 && pos + bytes <= data.size(), "Truncated message"); uint64_t v=0; for(unsigned i=0;i<bytes;i++) v |= uint64_t(uint8_t(data[pos++])) << (i*8); return v; }
    std::string str(size_t limit) { size_t n=size_t(get(4)); require(n<=limit && n<=data.size()-pos,"Invalid string size"); auto s=data.substr(pos,n); pos+=n; return s; }
    void end() { require(pos==data.size(), "Trailing data"); }
};
struct Entry { std::string name; uint64_t size=0; bool dir=false; fs::path local; fs::file_time_type modified{}; };
struct Offer { uint64_t id=0; unsigned kind=0; std::string text; std::vector<Entry> files; };
struct PathLess {
    bool operator()(const std::wstring& a,const std::wstring& b) const {
        return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_LESS_THAN;
    }
};
inline fs::path safe_relative(const std::string& name) {
    require(!name.empty() && name.size()<=2000, "Invalid filename length");
    require(name.find_first_of("\\:\0",0,3)==std::string::npos, "Invalid filename");
    auto p=fs::path(wide(name)); require(!p.is_absolute() && !p.has_root_path(), "Absolute path rejected");
    size_t depth=0;
    for (auto& c:p) {
        auto s=c.wstring(); require(++depth<=32 && !s.empty() && s!=L"." && s!=L".." && s.size()<=240, "Unsafe path component");
        require(s.back()!=L'.' && s.back()!=L' ' && s.find_first_of(L"<>\"|?*\r\n\t") == std::wstring::npos, "Unsafe filename");
        for(auto ch:s) require(ch>=32, "Control character in filename");
        std::wstring base=s.substr(0,s.find(L'.')); for(auto& ch:base) ch=towupper(ch);
        require(base!=L"CON" && base!=L"PRN" && base!=L"AUX" && base!=L"NUL" && base!=L"CONIN$" && base!=L"CONOUT$" &&
                !(base.size()==4 && (base.substr(0,3)==L"COM" || base.substr(0,3)==L"LPT") && ((base[3]>=L'0' && base[3]<=L'9') || base[3]==L'¹' || base[3]==L'²' || base[3]==L'³')), "Device filename rejected");
    }
    return p;
}
inline void validate(const Offer& o) {
    require(o.kind<=2 && o.id!=0, "Unknown clipboard format");
    require(o.text.size()<=max_text && o.text.find('\0')==std::string::npos, "Text too large or contains NUL"); wide(o.text);
    require(o.files.size()<=max_entries, "Too many files");
    require((o.kind==2 && !o.files.empty() && o.text.empty()) || (o.kind!=2 && o.files.empty()), "Invalid clipboard offer");
    uint64_t total=0; std::set<std::wstring,PathLess> names, file_names;
    for(auto& e:o.files) {
        auto p=safe_relative(e.name); auto key=p.generic_wstring(); for(auto& c:key)c=towupper(c);
        require(names.insert(key).second, "Duplicate filename");
        require(e.size<=max_bytes-total && (!e.dir || e.size==0), "Clipboard exceeds 10 GiB"); total+=e.size;
        if(!e.dir) file_names.insert(key);
    }
    for(auto& e:o.files) for(auto p=safe_relative(e.name).parent_path(); !p.empty(); p=p.parent_path()) {
        auto key=p.generic_wstring();for(auto& c:key)c=towupper(c);require(!file_names.count(key),"File/directory conflict");
    }
}
inline std::string encode(const Offer& o) {
    validate(o); std::string s="FCL1";put(s,o.id,8);put(s,o.kind,4);putstr(s,o.text);put(s,o.files.size(),4);
    for(auto& e:o.files){putstr(s,e.name);put(s,e.size,8);put(s,e.dir?1:0,1);}return s;
}
inline Offer decode(const std::string& s) {
    require(s.size()<=max_text+max_entries*2020+24 && s.substr(0,4)=="FCL1","Invalid clipboard message");
    Reader r{s,4};Offer o;o.id=r.get(8);o.kind=unsigned(r.get(4));o.text=r.str(max_text);size_t n=size_t(r.get(4));require(n<=max_entries,"Too many files");
    for(size_t i=0;i<n;i++){Entry e;e.name=r.str(2000);e.size=r.get(8);auto flag=r.get(1);require(flag<=1,"Invalid entry flag");e.dir=flag!=0;o.files.push_back(e);}r.end();validate(o);return o;
}
struct Clipboard {
    HWND window=nullptr;
    Clipboard() { window=CreateWindowExW(0,L"STATIC",L"Focus clipboard",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr); require(window!=nullptr,"Cannot create clipboard window"); }
    ~Clipboard(){if(window)DestroyWindow(window);}
    struct Open { bool ok; Open(HWND w):ok(OpenClipboard(w)!=FALSE){require(ok,"Clipboard is busy");} ~Open(){if(ok)CloseClipboard();} };
    static DWORD sequence(){return GetClipboardSequenceNumber();}
    Offer read() {
        Offer o;o.id=new_id();std::vector<fs::path> roots;
        {
            Open guard(window);
            if(IsClipboardFormatAvailable(CF_HDROP)) {
                auto drop=HDROP(GetClipboardData(CF_HDROP));require(drop!=nullptr,"Cannot read file clipboard");
                UINT n=DragQueryFileW(drop,0xffffffff,nullptr,0);require(n>0 && n<=max_entries,"Too many clipboard roots");
                for(UINT i=0;i<n;i++){UINT len=DragQueryFileW(drop,i,nullptr,0);std::wstring p(len+1,0);DragQueryFileW(drop,i,p.data(),len+1);p.resize(len);roots.emplace_back(p);}
                o.kind=2;
            } else if(IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                auto h=GetClipboardData(CF_UNICODETEXT);require(h!=nullptr,"Cannot read clipboard text");size_t bytes=GlobalSize(h);require(bytes<=max_text*2+2,"Clipboard text too large");
                auto p=static_cast<const wchar_t*>(GlobalLock(h));require(p!=nullptr,"Cannot lock clipboard text");
                size_t n=0;while(n<bytes/2 && p[n])++n;
                std::wstring w(p,n);GlobalUnlock(h);require(n<bytes/2,"Unterminated clipboard text");o.text=utf8(w);o.kind=1;
            }
        }
        for(auto& root:roots) {
            auto append=[&](const fs::path& p,const fs::path& rel){
                auto attr=GetFileAttributesW(p.c_str());require(attr!=INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_REPARSE_POINT),"Links/reparse points cannot be copied");
                bool dir=(attr&FILE_ATTRIBUTE_DIRECTORY)!=0;require(dir || fs::is_regular_file(p),"Not a regular file");
                o.files.push_back({path_utf8(rel),dir?0:fs::file_size(p),dir,p,fs::last_write_time(p)});require(o.files.size()<=max_entries,"Too many clipboard entries");
            };
            append(root,root.filename());
            if(fs::is_directory(root))for(auto& e:fs::recursive_directory_iterator(root))append(e.path(),root.filename()/e.path().lexically_relative(root));
        }
        validate(o);return o;
    }
    void write(const Offer& o,DWORD expected) {
        validate(o);require(o.kind!=0,"Empty format");std::string bytes;UINT fmt;
        if(o.kind==1){auto w=wide(o.text);bytes.assign(reinterpret_cast<const char*>(w.c_str()),(w.size()+1)*sizeof(wchar_t));fmt=CF_UNICODETEXT;}
        else {
            std::set<fs::path> roots;
            for(auto& e:o.files){auto rel=safe_relative(e.name);auto p=e.local;for(auto it=++rel.begin();it!=rel.end();++it)p=p.parent_path();roots.insert(p);}
            DROPFILES drop{};drop.pFiles=sizeof(drop);drop.fWide=TRUE;bytes.assign(reinterpret_cast<char*>(&drop),sizeof(drop));
            for(auto& p:roots){auto w=p.wstring();bytes.append(reinterpret_cast<const char*>(w.c_str()),(w.size()+1)*sizeof(wchar_t));}bytes.append(sizeof(wchar_t),0);fmt=CF_HDROP;
        }
        auto h=GlobalAlloc(GMEM_MOVEABLE,bytes.size());require(h!=nullptr,"Clipboard allocation failed");
        void* p=GlobalLock(h);if(!p){GlobalFree(h);throw std::runtime_error("Clipboard lock failed");}memcpy(p,bytes.data(),bytes.size());GlobalUnlock(h);
        try{
            Open guard(window);require(sequence()==expected,"Clipboard changed during transfer");require(EmptyClipboard()!=FALSE,"Cannot empty clipboard");require(SetClipboardData(fmt,h)!=nullptr,"Cannot set clipboard");h=nullptr;
            if(fmt==CF_HDROP){
                // A remote cut is deliberately published as a copy, never a move.
                auto effect=GlobalAlloc(GMEM_MOVEABLE,sizeof(DWORD));
                if(effect){auto value=static_cast<DWORD*>(GlobalLock(effect));if(value){*value=DROPEFFECT_COPY;GlobalUnlock(effect);if(!SetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"),effect))GlobalFree(effect);}else GlobalFree(effect);}
            }
        }catch(...){if(h)GlobalFree(h);throw;}
    }
};
inline std::string read_chunk(const Offer& o,size_t index,uint64_t offset) {
    require(index<o.files.size(),"Invalid file index");auto& e=o.files[index];require(!e.dir && offset<e.size,"Invalid file offset");
    auto attrs=GetFileAttributesW(e.local.c_str());require(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"Source changed");
    require(fs::file_size(e.local)==e.size,"Source file size changed");
    require(e.modified==fs::file_time_type{} || fs::last_write_time(e.local)==e.modified,"Source file changed");
    std::ifstream f(e.local,std::ios::binary);require(bool(f),"Cannot open copied file");f.seekg(std::streamoff(offset));
    std::string data(size_t(std::min<uint64_t>(chunk_bytes,e.size-offset)),0);f.read(data.data(),std::streamsize(data.size()));require(size_t(f.gcount())==data.size(),"Source file changed");return data;
}
struct Incoming {
    Offer offer;fs::path directory;std::vector<uint64_t> offsets;DWORD baseline=0;bool committed=false;
    Incoming(const Offer& o,const fs::path& cache,DWORD seq):offer(o),baseline(seq) {
        validate(o);require(o.kind!=0,"Empty upload");
        if(o.kind!=2)return;
        fs::create_directories(cache);uint64_t total=0;for(auto& e:o.files)total+=e.size;
        require(fs::space(cache).available>total+64*1024*1024,"Insufficient clipboard cache space");
        directory=cache/std::to_wstring(new_id());require(fs::create_directory(directory),"Cannot create clipboard staging directory");
        try { for(auto& e:offer.files){e.local=directory/safe_relative(e.name);if(e.dir)fs::create_directories(e.local);else{fs::create_directories(e.local.parent_path());std::ofstream f(e.local,std::ios::binary);require(bool(f),"Cannot stage clipboard file");}offsets.push_back(0);} }
        catch(...){std::error_code ec;fs::remove_all(directory,ec);throw;}
    }
    ~Incoming(){if(!committed && !directory.empty()){std::error_code ec;fs::remove_all(directory,ec);}}
    void append(size_t index,uint64_t offset,const std::string& data) {
        require(index<offer.files.size(),"Invalid upload index");auto& e=offer.files[index];
        require(!e.dir && offset==offsets[index] && !data.empty() && data.size()<=chunk_bytes && data.size()<=e.size-offset,"Invalid chunk");
        std::ofstream f(e.local,std::ios::binary|std::ios::app);require(bool(f),"Cannot open staged file");f.write(data.data(),std::streamsize(data.size()));f.close();require(bool(f),"Cannot write staged file");offsets[index]+=data.size();
    }
    void complete() { for(size_t i=0;i<offer.files.size();i++)require(offer.files[i].dir || offsets[i]==offer.files[i].size,"Incomplete upload"); }
};
inline fs::path cache_dir(const wchar_t* product) {
    wchar_t buf[MAX_PATH];HANDLE token=nullptr;
    OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token);
    auto result=SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,token,SHGFP_TYPE_CURRENT,buf);
    if(token)CloseHandle(token);require(SUCCEEDED(result),"Missing user local app data directory");return fs::path(buf)/product/L"Clipboard";
}
inline std::string chunk_request(uint64_t id,size_t index,uint64_t offset) {std::string s;put(s,id,8);put(s,index,4);put(s,offset,8);return s;}
} // namespace focusclip
