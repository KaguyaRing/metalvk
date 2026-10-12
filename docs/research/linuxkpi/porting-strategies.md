# Route A / Route B 决策矩阵

Route A：Linux源码→Darwin-native LinuxKPI接口→XNU。Route B：Linux源码→FreeBSD LinuxKPI→FreeBSD-on-XNU→XNU。二者都不是Linux `.ko`加载器。

| 维度 | Route A机制/成本 | Route B机制/成本 | 证据/不确定性 |
|---|---|---|---|
| API与DRM覆盖 | 自己提供选定Linux 6.14 API；可限制契约，但不能删必要TTM/VM/scheduler | 借上游LinuxKPI与drm-kmod；需版本精确配对、DRM Linux版本差异fork | [L01][L02][F02][D02]；本轮F/D组合未构建验收 |
| 模块注册/driver core | 单一Darwin生命周期适配pci_driver与devres | SYSINIT、新总线class、BSD driver methods、Linux pci_driver双向包装 | [L03][F01][R01][R02]；init返回值不得丢失 |
| PCI ownership | 明确IOService拥有provider、兼容pdev保留refs | 外层IOService+BSD device_t+Linux pdev多层owner | [P02][R01]；都需exclusive/D0/terminate标准 |
| alloc/pages/TTM | descriptor-backed页模型仍要保留TTM驱逐/迁移及shmem契约 | BSD malloc/vm_page/pager模型再落XNU；增加一层实体和cache表示 | [L30][F02][R04]；wired全固定不是等价实现 |
| HMM/userptr | 无公开等价，需要受限合同或新增可信机制 | FreeBSD VM假设仍需XNU invalidation通知，不能由BSD struct自动提供 | [L16][L17][L32]；共同硬阻塞，高信心 |
| DMA/IOMMU | Linux sg/attrs直接转IODMACommand | Linux dma→BSD bus_dma→IODMACommand，多层segment/cookie寿命 | [L15][L35][X01][F06]；真机mapper语义未验证 |
| IRQ/调度上下文 | 单层规定top-half/线程与locks；仍需Linux hardirq适配 | Linux IRQ→BSD bus_setup_intr→IOKit action；context差异更难追踪 | [L21][X15]；不能默认action=hardirq |
| 锁/等待/RCU | 实现Linux条件等待/ww锁/RCU契约并测grace period | BSD sleepq信号/timeout、ck_epoch、DPCPU记录、sched_pin/critical sections须再适配XNU | [L20][L22][F03][F05]；不止同名锁wrapper，CPU pin与grace period是额外语义 |
| work/timer/cancellation | manager直接提供Linux queue/cancel_sync；需防self-fini | BSD taskqueue/start_threads、mtx、callout与drain作为中间runtime | [L24][L41][F04]；两者都须drain callbacks才能卸载 |
| 用户ABI/ioctl | 可显式wire bridge，必须改ANV；或者提供限定DRM兼容面 | BSD file+Linux file+XNU clone devfs，ioctl结构/layout/errno三边转换 | [L09][R04]；完整ABI未验收 |
| mmap | IOKit mapping契约，限制与feature声明清楚 | 复用DRM fault处理但预填充pin；TTM迁移与VM语义改变 | [X08][R04][R09]；均非无需改用户态 |
| fw/ACPI/平台 | 使用受限Darwin服务，固定文件与firmware版本 | request_firmware/ACPICA接口→BSD样式→Apple平台解释器 | [L11][L13][P01]；实际blob与平台quirk未知 |
| PM/remove/reset | 单一准入generation/drain协调，保留xe硬件回调 | Linux runtime PM+BSD suspend事件+IOService PM三套顺序 | [L46][R03]；转发成功ack不能证明省电/安全 |
| 错误处理/诊断 | 结构化stage/status映射，较易定位单边owner | Linux负errno/BSD正errno/IOReturn与多层初始化转换 | [L04][R01]；重复cleanup及错误吞掉风险 |
| 导出/macOS兼容 | 尽量公开KPI，unsupported隔离；逐build审查 | BSD VM和跨任务服务可能推动依赖更多unsupported | [X07][R09]；没有新产物imports或KC证据 |
| 上游维护 | Linux API改变直接影响自己的契约；研发量大 | Linux↔FreeBSD与FreeBSD↔Darwin都需维护，不能跟浮动master | [F02][D02]；reference构建路径并非可复现锁文件 |
| 许可/分发 | 原Linux完整目标GPL依赖仍在；独立GPL研究模块需法务 | Linux+BSD许可集合更大；绕过GPL-only源需合法独立替代且功能验收 | [L28][L29][F01][D01]；不得复用NullMoth实现 |
| 攻击面 | 输入入口、DMA/VA/recovery天然高风险；可以明确受限入口 | 多层file/page/driver对象增加转换边界与状态数量 | [L09][L43][R04]；没有路线被认证安全 |
| 达到真实执行 | 长期可能，但HMM/RCU/TTM/scheduler/GuC缺口不能靠小wrapper解决 | 前期编译复用更多，但共同硬阻塞和版本/许可成本仍在 | [L19][L40][R05]；硬件证据均无 |

## 临时建议，仅对本研究目标

**优先研究Route A的有界合同，Route B作为固定上游对照。** 原因不是“更简单”：可以直接表达Darwin任务/descriptor/终止ownership，少一个BSD VM/file/newbus模拟层；xe执行链的HMM、mmap和异步回收硬问题在B也存在，编译复用不会解决它们。

建议置信度中等。没有A/B性能、开发工期、原生完整编译结果，不能给虚构分数或承诺A更快。B可能更快达到源级编译，若获得精确Linux/FreeBSD/DRM配对、许可批准和公开KPI语义验证，建议可重评。

研究第一合同可围绕owned BO/nonfaulting VM，不声称完整xe/ANV支持；对USERPTR、FAULT_MODE、PRIME外部共享明确拒绝后也必须审查用户态实际是否必需。**用户未批准该缩减合同或任一路线实施，NVIDIA RM不变。**
