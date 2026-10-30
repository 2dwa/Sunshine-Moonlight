# Windows 构建与打包

## 基线与工具

源码已包含修改，无需重复应用旧补丁。来源提交在根 `UPSTREAMS.json` 中。先从根目录执行 `git -c core.longpaths=true submodule update --init --recursive`；需要 Git、网络和足够磁盘空间。Windows 建议启用 Git `core.longpaths=true`，使用较短的仓库路径；部分深层第三方子模块会受到系统长路径设置限制。

本次验证使用：Qt 6.8.2 MSVC x64、MSVC 14.42.34433、Windows SDK 10.0.22621.0、MSYS2 UCRT64 GCC 16.2、CMake/Ninja、Node.js 22.15.0、Rust stable 1.99.1 MSVC 目标、Python 3，以及 NSIS。

Sunshine 的 MSYS2 环境需包含其上游依赖，包括 Boost、cppwinrt、curl、MinHook、miniupnpc、nlohmann-json、libvpl、OpenSSL、Opus、pkgconf。具体依赖和平台要求也应参照两个组件原有构建文档。

## 本机配置

复制 `build/environment.example.json` 为 `build/environment.json`，编辑 SDK 和运行时路径。支持绝对路径；相对路径以 `build/` 目录为基准。`.local/`、`out/`、`dist/` 和实际 `environment.json` 都不进入 Git。

`msvcRoot` 示例布局是 `VC/Tools/MSVC/<版本>` 与 `Windows Kits/10` 放在同一个根下的独立工具链。已有 Visual Studio 的用户可按此布局提供独立目录，或调整脚本中 VC/SDK 两处路径；不修改系统 PATH。

`sunshineBase` 应是上游 `v2026.1020.23431.杂鱼` 官方便携包的干净解压目录，包含 `assets/gui`、`tools` 和许可证等。`moonlightRuntime` 是 Moonlight 6.1.0 官方 Windows x64 包的干净运行时目录，用于打包 Qt/媒体 DLL。不要指向含个人配置和配对信息的日常安装目录。

RTX HDR / DLSS NR 构建默认开启并复用该 Sunshine 版本的官方适配器。CMake 会检查固定 SHA-256；不会静默接受其他版本的 DLL。增强运行库、兼容显卡和驱动仍需按照原版要求配置，仓库不分发私有运行库。

## 构建

从根目录运行：

```powershell
./build/build.ps1 -Target moonlight
./build/build.ps1 -Target sunshine
./build/build.ps1 -Target sunshine-web
./build/build.ps1 -Target sunshine-gui
```

或打开 `Sunshine-Moonlight.code-workspace` 使用内置构建任务。`-Target environment` 仅为当前进程加载 Qt/MSVC 环境；dot-source 此目标可供终端后续命令使用。

网页会生成到 Sunshine 源目录中的 `build/assets/web`，该目录已被上游忽略。其他构建产物默认进入根 `out/`。所有源码子模块都由根 `.gitmodules` 固定，不要单独在子目录重新初始化上游仓库。

## 验证

```powershell
./tests/run-unit-tests.ps1
python tests/test-console-startup.py
python tests/test-adapters.py
```

第一条在 MSYS2 UCRT64 下编译并运行锁定键、快捷键、剪贴板检查，生成启动探针；第二条还需已构建 `out/sunshine/sunshine.exe`。剪贴板测试使用独立窗口站，不触碰用户交互剪贴板。第三条只检查适配器 DLL 导出和 ABI，不启动 GPU 图像处理。

`tests/test-sunshine-live-restart.py` 是**手动集成测试**：它连接本机 47990，确认没有活动串流后真的重启 Sunshine 两次。仅在要测试的实例已启动、允许重启时运行；它不是普通单元测试的一部分。

`tests/test-lock-live.cpp` 是另一项**手动 Win32 集成测试**。用 UCRT64 g++ 编译（仓库根目录下 `g++ -std=c++17 -I. tests/test-lock-live.cpp -o out/tests/test-lock-live.exe -luser32 -limm32`），空闲桌面上运行 `out/tests/test-lock-live.exe --run-interactive`。它短暂显示专用测试窗口，验证真实 SendInput、三种锁定状态、重复同步及 WM_CHAR 大小写，最后恢复锁定状态与焦点；失去测试窗口焦点或检测到修饰键按住时停止。此项不在默认单元测试中运行。

## 打包

四个目标构建成功后：

```powershell
python scripts/package_apps.py
# 仅重新打包 Sunshine：
python scripts/package_apps.py --only sunshine
```

产物输出到 `dist/`，包括 NSIS 安装包、便携 ZIP 和 SHA-256 文件。安装器采用独立目录，不自动注册 Windows 服务；覆盖安装修改版 Moonlight 时保留已有 `Moonlight.ini`。

二进制发行时应关联本仓库对应提交的完整源码快照、固定依赖和构建资料，并附带各组件的许可证和声明。`docs/LOCAL-BUILD-SHA256SUMS.txt` 仅记录整理仓库前的本地验收产物，不承诺不同机器重建后具有相同字节摘要。
