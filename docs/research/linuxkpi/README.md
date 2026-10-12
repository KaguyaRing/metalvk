# LinuxKPI GPU 移植可行性研究：Phase 2

日期：2026-10-12。状态：**研究交付完成；执行后端未实现，架构迁移未批准**。

本研究独立核对固定来源，选择 Linux 6.14 xe / Intel DG2-G10 `8086:56a0`，分析 x86_64 macOS 15 的无显示执行路径。macOS build、实机 revision、SDK 与授权环境未知。研究结论不是 GPU 支持声明。

## 结论摘要

- 驱动不是 Linux `.ko` 二进制兼容：需要重新编译源码，提供 Linux 内核对象、同步与生命周期语义。[L01][L03][L49]
- 关闭 `DRM_XE_DISPLAY` 不会移除 DRM/TTM/GPUVM、HMM/MMU notifier、GuC、fence、RCU 和异步销毁依赖。[L01][L02][L16][L22][L41]
- Darwin 可提供 PCI、wired backing、DMA、线程和锁的底座，但没有经本轮证明可替代 Linux MMU interval notifier/HMM 的公开 kext 接口。固定用户页不等价于动态失效通知。[L17][X02]
- 原版 xe 包含 GPL-only 文件及 GPL 核心依赖；不能宣称整套移植可随 metalvk MIT 许可直接分发。固件专利许可也需专门审查。[L28][L29][L48]
- 对该目标暂推荐 **Route A 的受限语义契约验证研究**，不是批准实施完整直接移植。Route B 值得保留，但增加 FreeBSD newbus/VM/file/sysinit 语义，不能绕过 HMM、mmap 或许可问题。[F01][F02][R01][R04]
- G0、限定范围的 G1 研究门已通过；Darwin 编译/KC/真机后续门均未通过。宿主生成器成功不属于 G2。

## 文档导航

| 文档 | 内容 |
|---|---|
| [目标基线](target-baseline.md) | 候选比较、固定 revision、配置与未知信息 |
| [依赖清单](driver-dependencies.md) | 直接/传递依赖、运行语义、前置与回收、验证要求 |
| [生命周期](driver-lifecycle.md) | probe、执行、失败、关闭、PM、reset、remove |
| [Darwin KPI 映射](darwin-kpi-map.md) | 声明、实现、导出与语义差异 |
| [路线比较](porting-strategies.md) | Route A/B 机制矩阵与不确定性 |
| [Vulkan 边界](vulkan-boundaries.md) | ANV 所需内核接口与厂商中立架构 |
| [许可](licensing.md) | 文件级许可、固件条件、分发风险 |
| [构建与部署](build-deployment.md) | 工具、生成头、链接/KC、安全离线实验 |
| [验证门](verification-gates.md) | G0–G13 前置、程序、证据、失败条件 |
| [阻塞项](blockers.md) | 问题、证据缺口、调查优先级 |
| [决定记录](decision.md) | 临时建议、实施入口与授权边界 |
| [证据索引](evidence.md) | 固定源码链接与自动核对窗口 |

## 证据标准和复现

所有 `[Lxx]` 等 ID 在 [证据索引](evidence.md) 解码到完整提交与具体行号；仅索引、元数据和原创研究文字入库，无参考实现源代码导入。

`source-inventory.json` 包含 xe 所有 C/H 的规范化 hash、SPDX 和直接 include，以及选定调用窗口。**它是词法 include 目录，不是预处理图、所有配置的可达调用图或最终链接闭包。** 手工调用链复核的范围见依赖文档。

在项目根目录运行：

```powershell
python docs/research/linuxkpi/audit_sources.py --references ../../Example
python -B -m unittest discover -s docs/research/linuxkpi -p test_audit_sources.py -v
git diff --check
```

检查器仅读取固定源码与文档，不联网、不构建 kext、不访问硬件。`--write-evidence` 只用于有意生成元数据；后续默认检查不覆盖基线。脚本成功不证明语义完整或安全。

测试临时目录应设到批准的 `C:/Users/kaguyaring/AppData/Local/Temp/opencode`；`-B`避免在研究目录产生bytecode。八项检查器回归覆盖有效/断裂链接、缺文档/证据、占位符、UTF-8/CRLF、超限输入和确定性证据渲染。

前一代理的 CI/ASan/Windows 硬件结果仅为历史报告，本轮不重跑也不升级其证据范围。本轮独立核对 metalvk `c3f2de3` 源文件、当前 Git 基线、参考历史、目标上游调用链、Darwin 导出及许可证；没有证实 Darwin GPU 执行。
