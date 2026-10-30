# Sunshine-Moonlight

Windows 双屏串流定制版，同时维护 Moonlight 客户端和 Foundation Sunshine 服务端。仓库维护者：**2dwa**；本次定制修改贡献者：**GPT-6 Astra（AI 辅助开发）**。这是独立定制版本，不是上游官方发行版。

## 功能

- 可配置快捷键，在本机与 Moonlight 串流之间切换操作焦点；保留两侧鼠标位置，以黑白鼠标形提示闪烁约 0.5 秒。
- 串流开启“捕获系统快捷键”后仍支持切回本机。
- Caps Lock、Num Lock、Scroll Lock 以运行 Moonlight 的本机状态为准同步。
- 串流期间双向复制纯文本、文件和文件夹。文件完整接收后再放入目标剪贴板。
- Foundation Sunshine 启用 RTX HDR、DLSS NR，复用匹配版本的官方适配器，保留原有运行库校验与配置入口。
- 禁用两端软件更新；保留手动安装增强运行库、驱动和工具的功能。
- Sunshine 默认无控制台窗口启动，修复无控制台模式下重启后后台不再启动的问题。

首次安装的默认焦点快捷键为 **Alt+Q**，可在 Moonlight 输入设置中修改。详细操作见 [使用说明](docs/usage-zh-CN.txt)。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| `moonlight-qt/` | Moonlight v6.1.0 完整主仓库源码快照及修改 |
| `foundation-sunshine/` | Foundation Sunshine 指定版本完整主仓库源码快照及修改 |
| `foundation-sunshine/src_assets/common/sunshine-control-panel/` | 控制面板完整源码，原子模块已展开，包含更新禁用修改 |
| `build/` | 集中环境配置示例、PowerShell 构建脚本 |
| `scripts/` | 本地打包脚本 |
| `tests/` | 锁定键、快捷键、剪贴板、无控制台启动与重启验证 |
| `docs/` | 使用、构建、协议、变更及验证资料 |
| `UPSTREAMS.json` | 上游来源、基线提交及第三方子模块固定提交 |

本仓库包含两个应用及控制面板的实际源码，不要求先下载补丁再重建源码树。第三方依赖保持 Git 子模块，统一由根目录 `.gitmodules` 管理。各组件目录中的原 `.gitmodules` 仅保留为上游资料；请从仓库根目录初始化依赖。

## 获取与构建

```powershell
git clone git@github.com:2dwa/Sunshine-Moonlight.git
cd Sunshine-Moonlight
git config core.longpaths true
git -c core.longpaths=true submodule update --init --recursive
Copy-Item build/environment.example.json build/environment.json
```

在 `build/environment.json` 中填写自己的 Qt、MSVC、Windows SDK、MSYS2、Node.js、Rust 和官方运行时包路径，打开 `Sunshine-Moonlight.code-workspace`。本机配置文件不会提交到 Git。

```powershell
./build/build.ps1 -Target moonlight
./build/build.ps1 -Target sunshine
./build/build.ps1 -Target sunshine-web
./build/build.ps1 -Target sunshine-gui
```

工具版本、第三方依赖、打包和测试命令见 [构建说明](docs/BUILD.md)。SDK、安装包、个人配置、配对凭据和 NVIDIA 私有运行库不存放在 Git 历史中。SDK/运行库仍需按各自上游许可取得。

## 验证与限制

已完成 Windows x64 Release 编译、两套编译器下的 40 项剪贴板检查、64 种锁定键组合检查及快捷键回归。无控制台版本已在实际安装目录连续重启两次，后台实例更换后控制面板恢复 HTTP 200。

双机文件复制和锁定键的完整端到端验收，以及 RTX HDR/DLSS NR 的实际 GPU 画质与性能测试尚未完成。文件复制上限为每次 10 GiB / 4096 个文件或目录；纯文本上限 8 MiB。游戏相对鼠标模式无法取得远端真实光标位置，提示显示在画面中心。详见 [验证记录](docs/validation-zh-CN.txt)。

## 署名与许可证

上游作者、版权声明、许可证及第三方声明均保留。本仓库根 `LICENSE` 是 GPLv3 文本；它不替换独立组件及第三方材料的原许可证。Moonlight 和 Sunshine 沿用其 GPLv3 许可，控制面板保留 MIT 许可。新增协调脚本、测试和文档按 GPLv3 提供。

参见 [贡献者与来源](CONTRIBUTORS.md)、[许可证说明](docs/LICENSES.md)、[变更记录](CHANGELOG.md) 和 [逐文件修改清单](docs/modified-files.json)。发布二进制时应同时提供匹配版本的对应源码、构建资料与适用许可证；不能仅分发安装器而省略这些资料。
