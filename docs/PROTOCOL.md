# 定制协议与边界

## 中英文输入法：ImeSync2 按操作焦点同步

能力位仍为 `0x10000000`。新端点 `POST /api/v2/focus-ime/state` 请求与响应均固定 5 字节：ASCII `FIM2` + 状态字节（0=未知/只读，1=英文，2=中文）。请求 1/2 时对远端当前输入框应用该模式并重新读取状态；响应未知表示没有可访问或稳定的上下文。要求证书仍配对、对应串流控制连接活动且键盘输入开启，不接受任意窗口或键盘命令。

客户端在本机应用获得焦点时发送真实本机模式；此类请求的回复只作确认，绝不改写本机。串流获得焦点时请求状态 0，只有请求开始和回复到达时均仍聚焦串流才应用到上次本机后台输入上下文。请求期间切回本机则丢弃该次远端写回，避免覆盖本机切换。未知状态不作为英文发送。循环约 250 ms，使用固定配对证书 HTTPS、禁止重定向、限定回复长度与超时；断线不应用缓存状态。

旧 `/api/v1/focus-ime/state` 与 `FIM1` 空请求读状态接口保留。新客户端收到 v2 的 404 后退回 v1，只在串流前台时允许远端写回，不覆盖本机前台；本机至远端功能需新 Sunshine。原设置存储键 `syncImeFromHost` 为兼容已有配置继续使用，界面显示双向同步。

直接通过有限超时 `WM_IME_CONTROL` 设置并读回微软拼音模式，英文为 0，中文为 `IME_CMODE_NATIVE | IME_CMODE_SYMBOL`。不注入 Shift、不改变用户的快捷键绑定，不读取输入文本或候选词。目标 HWND/进程/线程/焦点身份需匹配；服务器在当前输入桌面执行，焦点改变时不确认其他窗口的状态。

Windows API 依据：[ImmGetDefaultIMEWnd](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetdefaultimewnd)、[WM_IME_CONTROL](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-control)。焦点方向与版本隔离见 `tests/test-ime-focus.cpp`；真实输入框验证见 `tests/test-ime.cpp`，各次验证结果以验证记录为准。

## 锁定键

Sunshine 能力位 `0x40000000` 表示支持此扩展。客户端用保留组合 `VK=0`、`KEY_ACTION_UP`、`flags=0x80` 发送目标锁定状态，modifier 低三位依次表示 Caps/Num/Scroll。服务端严格校验后只注入状态不同的键；按下和释放成对发送。与不支持此扩展的主机连接时禁用同步。

注意上游 `NV_KEYBOARD_PACKET::flags` 类型为 `char`，必须转换为无符号字节后比较 `0x80`；直接比较会在 Windows 默认 signed char 编译下丢弃所有状态包。`tests/test-lock-packet.cpp` 用真实数据包结构覆盖此回归。

Windows 同步线程使用与原 `syncThreadDesktop()` 相同的输入桌面权限。仅能打开、切换到桌面并不代表能够注入输入；需要通过 `tests/test-lock-live.cpp` 验证真实前台窗口的锁定状态和字母大小写。失败日志为 `Keyboard lock sync failed`，包含目标状态和 Win32 错误；错误码为 0 也不能当成成功，SendInput 返回数量仍是判断依据。

## 文本和文件剪贴板

能力位为 `0x20000000`，复用已配对证书验证的 GameStream HTTPS，无额外监听端口。所有请求使用 POST：

`/api/v1/focus-clipboard/{state,read,begin,write,commit,cancel}`

服务端要求证书仍在启用的配对列表中，对应串流控制连接处于 RUNNING，且原 `clipboard_sync` 设置开启。客户端固定已配对主机证书，不跟随 HTTP 重定向。

`FCL1` 描述符按小端编码：4 字节 magic、8 字节 offer ID、4 字节 kind、长度前缀 UTF-8 文本、4 字节条目数。每个文件条目为长度前缀 UTF-8 相对名称、8 字节长度、1 字节目录标志。本机绝对路径不出现在协议描述符中。

块请求包含 8 字节 offer ID、4 字节条目索引和 8 字节偏移；写入请求随后携带最多 256 KiB 数据。只能按预期偏移追加，全部文件完整后才能提交到剪贴板。

一次纯文本最多 8 MiB；文件总计最多 10 GiB、4096 条目、32 层目录。拒绝设备名、越界路径、重复名称、符号链接和目录联接。传输期间任一相关剪贴板变化会阻止发布旧内容。文件只按复制语义发布，源端剪切不删除源文件。

未完成接收会清理；已完成文件保留在用户本地应用数据目录，以便断线后粘贴。用户确认不再需要后可手动清理缓存。服务端以 SYSTEM 运行时，文件访问使用交互用户身份和对应缓存路径。

## 启动和重启

Windows Core 为 GUI 子系统，不创建控制台；显式命令行调用保留重定向输出。独立运行与服务运行的区分使用原有 LocalSystem 身份检查，不再依赖 `GetConsoleWindow`。独立进程退出时自行派生新进程，服务模式则由原监督程序恢复。

## UsbSync1 双向 USB

保留原 GET `/api/v1/usb-forwarding` 版本 1，新增 `bidirectional: true`。所有控制请求继续使用配对客户端证书，并检查主机 USB opt-in；设备 API GET/POST `/api/v1/usb-forwarding/devices` 读取 usbipd-win JSON 或执行显式 bind/unbind。POST 包含 operation、busid、instance，须与当前设备匹配；不接受任意命令，不使用强制绑定。

原 TLS `op=forward` 通道继续提供 Moonlight 本机 USB → Sunshine。新增 `op=import` 使用相同配对 TLS 与随机 token，让 Sunshine 连接本机回环 USB/IP exporter；Moonlight 提供临时回环入口给本机 usbip-win2。新方向只允许 USB/IP 0x0111 的 OP_REQ_IMPORT (0x8003)、零状态和完全匹配的 32 字节补零 busid；拒绝通过该通道列举或导入其他设备。设备槽按方向区分，同一个 busid 不会造成两端设备相互冲突。

客户端传输在专用线程执行，明文和加密写入队列均计入限额。对端正常关闭时排空已接收数据，再完成关闭；异常关闭/超时清理所属导入。Windows TCP keepalive 使用 10 秒空闲 / 1 秒间隔。控制 API 与数据均不发送到未匹配配对证书的服务端。

共享设置是 usbipd 的持久配置；释放连接不会偷偷撤销用户已有共享设置。窗口不自动连接设备、不跨重启持久导入。接口仅针对 Windows USB 组件；物理设备兼容性以实测为准。
