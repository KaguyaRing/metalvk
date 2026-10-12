# 研究决定记录：LKPI-002

状态：**PROVISIONAL / RESEARCH_ONLY**。日期：2026-10-12。

## 已决定的研究范围

Linux xe v6.14 / `38fec10eb60d687e30c8c6b5420d86e8149f7557`、DG2-G10 `8086:56a0`、x86_64 macOS15公开XNU/PCI来源，先审无display执行路径。不是已验证支持组合；build/revision/firmware hash仍UNKNOWN。

选择理由：driver与ANV UAPI边界明确，可固定源码并追踪主要执行子系统；不是假设xe比AMD/NVIDIA容易，不改厂商中立架构。

## 临时推荐

优先Route A有界合同研究，Route B固定样本对照；不添加FreeBSD-on-XNU生产依赖。机制和不确定性见 [路线矩阵](porting-strategies.md)。

拒绝推导：probe=执行、export=可加载、KC=授权、wiring=MMU通知、noop pipeline=shader执行、MIT多数文件=整驱动MIT、内核port=Darwin ANV ICD=Metal。NVIDIA RM不替换，Vulkan边界不变。

## 完成和未完成

完成：仓库审计/选目标/固定源/重要直接与传递依赖/生命周期/KPI分类/路线矩阵/许可与build/G0–G13/研究文件与安全QA。

未完成：所有配置预处理/传递binary closure、Linux/Darwin整driver编译、KC/签名/加载、实际IPC、DMA/GuC/提交/回读、ANV Darwin执行、显示、Metal。历史CI不重跑/挪用；host生成器仅辅助证据。

## 下一工程单元建议（不开始）

建议：**经批准的独立Intel backend语义合同与生命周期最小实现**，先module注册失败回滚、provider独占/准入/connection refs、stage/gen/drain与错误查询；不MMIO写、不DMA、不广告执行。必须实现真实注册/失败/停止，不提供同名空Linux API或假probe成功。

进入条件：

1. 用户批准路线、源码/许可模型及backend边界，必要时批准受限owned-BO/nonfaulting合同，不默认为架构迁移。
2. 固定compiler/SDK/build/device/owner，建立可重复目标编译环境。
3. 法务/Intel澄清固件条款，建立完整source/link闭包计划。纯生命周期单元不加载firmware，阻止未解决的后续发行。
4. 定稿wire/feature与ANV需求；必需userptr无法支持时停止组合而非假实现。

验收：真实Darwin-target编译零诊断；各stage错误逆序回收；并发open/close/stop/进程退出无UAF/泄漏/丢唤醒；不抢设备/广告执行；host模型与native编译分开。不在此单元宣称Linux ABI/完整DRM实现。

后续分门：页/线程/RCU→PCI/DMA/IRQ→TTM/BO/VM→GuC/scheduler/fence→真实回读→ANV ICD→显示→Metal。每单元需授权与实际验收。

## 交付纪律

只提交研究文档、元数据、QA脚本及双语README入口；不改production、不导入implementation、不移tag。push前fetch比对upstream，只fast-forward不force；不明/divergence则保留local commit并报告。
