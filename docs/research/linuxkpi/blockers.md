# 阻塞项、证据缺口与调查优先级

| 优先级/问题 | 资产与风险/实际来源 | 解除所需证据 | 当前判断 |
|---|---|---|---|
| P0：固件许可 | DG2 GuC在macOS上的专利组合使用限制；firmware必要 [L11] | Intel澄清/专业法务评估、实际blob/hash与可用授权 | 发行/执行计划法律门，不以用户自下载绕过 |
| P0：源码链接许可 | GPL-only walker/managed/MMU等，与XNU最终组合义务 [L28][L29][L48] | compiler/link闭包、文件notice、分发模型审查 | 无许可结论前不导入生产 |
| P0：HMM/MMU失效 | CPU地址变更后GPU访问过期页，UAF/泄漏 [L17][L18] | 公开KPI通知协议或获准受限合同；munmap/remap/exit竞态回归 | 原版完整userptr合同当前不具备 |
| P0：硬件/环境 | build/revision/现有owner/安全状态未知 | 隔离Mac只读基线、授权、恢复方案 | G4以后全部BLOCKED |
| P1：DMA/页/TTM | page ownership、IOMMU与VRAM驱逐/缓存 [L15][L30][X01] | descriptor模型、mapper/migration实测、失败回滚 | 固定wire不能代替完整TTM/HMM |
| P1：异步回收 | GuC dereg/RCU/fence/workqueue、PM锁序 [L20][L22][L41][L46] | 锁序/引用图；stop/drain、超时/close/reset/fault注入 | 尚无LinuxKPI实现/执行能力 |
| P1：ABI/mmap | VCHR mmap ENODEV，Linux fd/sync合同 [X08][M01] | bridge定稿+layout测试+ANV必需feature核对 | ioctl workaround不是透明Linux ABI |
| P1：构建/KC | SDK/编译器/generated配置/imports未知 | 可重复真实构建+精确build link/KC日志 | host generator不是driver compilation |
| P2：FreeBSD配对 | Linux6.14与BSD/drm-kmod差异 [F02][D02] | 固定配对依赖/config/编译回归/许可 | B不是已可复用现成runtime |
| P2：macOS26 | 仅exports/PCI样本对照 | 独立build/KC/硬件gate与API差分 | 不承诺自动兼容 |
| P2：显示/Metal | KMS/IOSurface/WindowServer/translation未完成 [L01][L02] | 各自架构与硬件合同 | 当前不开始、不广告支持 |

## 参考核对

`3caaa0f`提交说明与路径、`git tag --contains`证明兼容runtime/AMD kext进入v1.14.0；v1.14.1 HEAD已固定。README明确41/41为noop device/pipeline、内核driver未在硬件加载；这是作者报告，不升级为独立硬件事实。[R05][R10]

参考xe来自外部Linux目录，FreeBSD/drm-kmod来自作者本机路径，自己替换GPL walker/module/trace等；它不是本目标原版xe，KC报告不证明原版许可/语义完整。[R07][R08] 本轮未复建/复制wrapper。

NVENC Blackwell门控存在，[R06] RTX4060不是DG2研究目标，不追加编码库/吞吐结论。参考执行结果不计本项目门。

## 比例适当的安全模型

资产：kernel完整性、进程隔离、DMA边界、firmware真实性、数据及显示可恢复性。

信任边界：非特权应用→ioctl/IPC→compat objects→DRM/TTM/GPUVM→DMA/IOMMU→firmware/GPU；PM/hot-removal异步事件跨所有寿命。

攻击链：handle owner未检查→绑定他人BO→DMA泄漏；长度/extension溢出→内核越界；close/remap与submit竞争→释放后设备访问；BO/queue/work无quota→wired耗尽；固件路径/供应链替换→不可信解析/硬件控制。对应多层授权、整数边界、quota、版本化协议、事务回滚、generation/refs/drain、hash与安全固件加载。这是设计威胁模型，**没有对不存在的实现做SAST/DAST通过认证**。

新增脚本仅读本地文本/固定git argv、写自己的元数据，无网络入口/驱动执行；限制单文件8MiB、subprocess timeout、UTF-8。来源路径由本地操作者指定，非IPC输入。不扫描第三方系统。

剩余低风险：网页可变/不可达、Mesa archive完整性未重验、词法include非链接闭包、Mermaid未渲染检查。均已明确，不隐藏为硬件验收完成。
