# 许可证与分发说明

| 组件 | 保留的许可证文件 | 适用说明 |
| --- | --- | --- |
| Moonlight | `moonlight-qt/LICENSE` | GPLv3；单个文件已有的声明继续适用 |
| Foundation Sunshine | `foundation-sunshine/LICENSE` | GPLv3；单个文件已有的声明继续适用 |
| Sunshine Control Panel | `foundation-sunshine/src_assets/common/sunshine-control-panel/LICENSE` | MIT；保留原版权和许可文字 |
| 根构建脚本、测试和新文档 | 根 `LICENSE` | GPLv3 |
| 第三方依赖、工具和资源 | 各原目录、子模块中的 LICENSE / COPYING / NOTICE 等 | 使用各自许可证，不因聚合到本仓库而统一改为 GPL |

原许可证、版权行、作者资料和第三方声明不被 GPT-6 Astra 的贡献者署名替代。修改过的源码附有日期和修改者说明；`modified-files.json` 列出文件及内容摘要。

源代码基线、原始仓库、固定依赖提交见根 `UPSTREAMS.json`。本仓库保留源树已有的工具文件；其来源和许可沿用上游，并非本次重新授权。

发布 GPL 覆盖的安装包/便携包时，应让接收者同时取得该二进制对应的源码、修改、依赖版本和控制构建/安装的资料，附上相应许可证及原有声明。源码或二进制发布前也需检查其中独立第三方组件的许可条件。当前 Git 提交以源码和构建资料为交付内容，不发布本地安装器。

RTX Video / DLSS 适配器及 NVIDIA 运行库受各自适用许可约束。此仓库仅保留上游公开代码、构建逻辑、声明与固定摘要；不提交 NVIDIA 私有 SDK 或未被原版分发的运行库。配置示例中的官方便携包由构建者自行按上游条件取得。

参考：[GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)、[MIT 许可](https://opensource.org/license/mit)。
