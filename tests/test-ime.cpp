// Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "moonlight-qt/app/streaming/focusime_win.h"
#include <cstdio>
#include <objbase.h>
#include <future>
#include <cstring>
#include <thread>
#include <initializer_list>
#include <atomic>
static std::atomic<unsigned> shifts{0};
LRESULT CALLBACK hook(int c,WPARAM w,LPARAM l){
 if(c==HC_ACTION){auto k=reinterpret_cast<KBDLLHOOKSTRUCT*>(l);if((k->vkCode==VK_LSHIFT||k->vkCode==VK_RSHIFT||k->vkCode==VK_SHIFT)&&(k->flags&LLKHF_INJECTED))++shifts;}
 return CallNextHookEx(nullptr,c,w,l);
}
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,a,b);}
int fixture() {
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"FocusImeLiveTest";RegisterClassW(&wc);
 auto w=CreateWindowW(wc.lpszClassName,L"Cross-process Microsoft Pinyin test",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,540,160,nullptr,nullptr,wc.hInstance,nullptr);
 auto e=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,10,10,490,50,w,nullptr,wc.hInstance,nullptr);
 ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(e);
 MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}CoUninitialize();return 0;
}
int main(int argc,char**argv){
 if(argc==2&&!std::strcmp(argv[1],"--fixture"))return fixture();
 focusime::Mode decoded=focusime::Mode::unknown;
 for(unsigned i=0;i<3;++i)if(!focusime::decode(focusime::encode(static_cast<focusime::Mode>(i)),decoded)||unsigned(decoded)!=i)return 1;
 for(auto bad:{std::string(),std::string("FIM1"),std::string("FIM1\3",5),std::string("FIM1\xff",5),std::string("FIM1\1x",6),std::string("BAD1\1",5)})if(focusime::decode(bad,decoded))return 1;
 if(argc!=2||std::strcmp(argv[1],"--run-interactive")){puts("PASS: IME wire protocol. Use --run-interactive for real Microsoft Pinyin input verification.");return 0;}
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 HWND previous=GetForegroundWindow();
 WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"FocusImeLiveTest";RegisterClassW(&wc);
 wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);
 std::wstring command=L"\""+std::wstring(executable)+L"\" --fixture";
 STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
 if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return 2;
 struct Search {DWORD pid;HWND window;};Search search{child.dwProcessId,nullptr};
 for(int tries=0;tries<100&&!search.window;++tries){
  EnumWindows([](HWND w,LPARAM p)->BOOL{auto s=reinterpret_cast<Search*>(p);DWORD pid=0;GetWindowThreadProcessId(w,&pid);wchar_t name[64]{};GetClassNameW(w,name,64);if(pid==s->pid&&!wcscmp(name,L"FocusImeLiveTest")){s->window=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));Sleep(50);
 }
 if(!search.window){TerminateProcess(child.hProcess,2);CloseHandle(child.hProcess);CloseHandle(child.hThread);return 2;}
 HWND window=search.window;auto target=focusime::inspect(window);HWND edit=target.focus;
 SetForegroundWindow(window);
 auto ownerThread=GetCurrentThreadId();MSG createQueue;PeekMessageW(&createQueue,nullptr,0,0,PM_NOREMOVE);
 HHOOK keyHook=SetWindowsHookExW(WH_KEYBOARD_LL,hook,wc.hInstance,0);
 std::atomic<int> result{0};
 std::thread worker([&]{
  Sleep(200);auto initial=focusime::inspect(window);auto initialMode=initial.mode;
  if(GetForegroundWindow()!=window||initialMode==focusime::Mode::unknown){puts("SKIP: Microsoft Pinyin test context unavailable");result=2;PostMessageW(window,WM_CLOSE,0,0);PostThreadMessageW(ownerThread,WM_QUIT,0,0);return;}
  const focusime::Mode modes[]={focusime::Mode::english,focusime::Mode::english,focusime::Mode::chinese,focusime::Mode::chinese,focusime::Mode::english,focusime::Mode::chinese};
  unsigned checks=0;
  for(auto mode:modes){
   if(GetForegroundWindow()!=window){result=3;break;}
   auto current=focusime::inspect(window);
   bool applied=focusime::apply(current,mode);Sleep(500);
   auto observed=focusime::inspect(window).mode;
   printf("desired=%u observed=%u applied=%d\n",unsigned(mode),unsigned(observed),applied);
   if(!applied||observed!=mode){result=1;break;}++checks;
   // A delayed opposite remote reply must not undo local input-mode changes.
   auto stale = mode == focusime::Mode::english ? focusime::Mode::chinese : focusime::Mode::english;
   if(focusime::mayApplyRemote(false,false,stale) || focusime::mayApplyRemote(true,false,stale))
     focusime::apply(focusime::inspect(window),stale);
   Sleep(300);
   if(focusime::inspect(window).mode!=mode){result=1;break;}++checks;
   if(GetForegroundWindow()!=window){result=3;break;}
   SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L""));
   const WORD latin[]={ 'A',VK_SPACE };const WORD pinyin[]={ 'N','I',VK_SPACE };
   auto keys=mode==focusime::Mode::english?latin:pinyin;unsigned count=mode==focusime::Mode::english?2:3;
   for(unsigned i=0;i<count;++i){INPUT events[2]{};events[0].type=events[1].type=INPUT_KEYBOARD;events[0].ki.wVk=events[1].ki.wVk=keys[i];events[1].ki.dwFlags=KEYEVENTF_KEYUP;if(SendInput(2,events,sizeof(INPUT))!=2){result=1;break;}Sleep(200);}
   Sleep(800);wchar_t text[64]{};SendMessageW(edit,WM_GETTEXT,64,reinterpret_cast<LPARAM>(text));
   bool valid=mode==focusime::Mode::english?(text[0]==L'a'||text[0]==L'A'):(text[0]>=0x3400&&text[0]<=0x9fff);
   printf("actual text: first codepoint=U+%04X valid=%d\n",unsigned(text[0]),valid);
   if(!valid){result=1;break;}++checks;
  }
  // Exercise the same target while a separate UI thread owns foreground.
  std::promise<HWND> ready;
  std::thread other([&]{
   auto second=CreateWindowW(wc.lpszClassName,L"Background IME target test",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,360,120,nullptr,nullptr,wc.hInstance,nullptr);
   auto field=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,10,10,300,35,second,nullptr,wc.hInstance,nullptr);
   ShowWindow(second,SW_SHOW);SetForegroundWindow(second);SetFocus(field);ready.set_value(second);
   MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
  });
  auto otherWindow=ready.get_future().get();Sleep(150);
  for(auto desired:{focusime::Mode::english,focusime::Mode::chinese}){
   bool ok=focusime::apply(initial,desired);Sleep(100);auto observed=focusime::inspect(window,initial.focus).mode;
   printf("background desired=%u observed=%u applied=%d foreground_preserved=%d\n",unsigned(desired),unsigned(observed),ok,GetForegroundWindow()==otherWindow);
   if(!ok||observed!=desired||GetForegroundWindow()!=otherWindow)result=1;else ++checks;
  }
  PostMessageW(otherWindow,WM_CLOSE,0,0);other.join();SetForegroundWindow(window);Sleep(150);
  focusime::apply(focusime::inspect(window),initialMode);
  if(shifts!=0){printf("FAIL: %u injected Shift events\n",unsigned(shifts));result=1;}
  printf("%s: %u real IME state/text checks; injected Shift=%u; original mode restored=%d\n",result==0?"PASS":"FAIL",checks,unsigned(shifts),focusime::inspect(window).mode==initialMode);
  PostMessageW(window,WM_CLOSE,0,0);PostThreadMessageW(ownerThread,WM_QUIT,0,0);
 });
 MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}worker.join();WaitForSingleObject(child.hProcess,5000);CloseHandle(child.hProcess);CloseHandle(child.hThread);if(keyHook)UnhookWindowsHookEx(keyHook);if(IsWindow(previous))SetForegroundWindow(previous);return result;
}
