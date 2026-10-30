# 定制协议与边界

## 中英文输入法：远端为准

能力位 `0x10000000`。`POST /api/v1/focus-ime/state` 请求体为空，复用 GameStream 配对证书 HTTPS；要求仍配对、该证书对应控制连接活动且键盘输入开启，仅提供读取接口。响应固定 5 字节：ASCII `FIM1` + 一个状态字节（0=未知/上下文禁用，1=英文，2=中文）。不传输窗口标题、输入文本、候选词或组合字符串。

Moonlight 的独立工作线程每约 250 ms 请求状态，不依赖前台焦点或剪贴板开关。不跟随 HTTP 跳转，固定主机配对证书，限制响应长度和超时。未知状态、请求失败或不匹配证书不应用旧状态；断开串流即销毁工作线程。

本机对当前本机输入框或上次本机输入框设置微软拼音模式并读回校验：英文值为 0，中文值为 `IME_CMODE_NATIVE | IME_CMODE_SYMBOL`。仅将 `0x401` 清除 NATIVE 得到 `0x400` 会被新版微软拼音忽略，因此不能据此宣称切换成功。通过有限超时的 `WM_IME_CONTROL` 操作目标线程的默认 IME 窗口；不注入 Shift，不改变用户的 Shift 开关。不支持/禁止输入法的窗口不强行启用 IME。本机获得焦点时重新选择实际输入框；后台使用先前验证过进程/线程身份的焦点句柄。

Windows API 依据：[ImmGetDefaultIMEWnd](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetdefaultimewnd)、[WM_IME_CONTROL](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-control)。具体模式值及后台/跨进程行为还经 `tests/test-ime.cpp` 的真实微软拼音输入验证。

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
