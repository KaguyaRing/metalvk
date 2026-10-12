# 源码依赖与语义清单

## 范围与读法

直接入口：xe Makefile 的 core `xe-y`、所选 profile 的 HMM 对象、module/pci/device、BO/VM/exec/queue/sync、GT/uC/IRQ/fence/PM 调用链；继续检查 DRM managed/GPUVM/TTM/scheduler/syncobj、DMA mapping、MMU notifier/HMM 代表实现。[L01][L02][L29][L30][L31][L32][L34][L35][L48]

元数据扫描覆盖 xe 全目录 C/H（包括未启用 display/test），检查 SPDX、includes、hash。没有 Linux generated headers/preprocessor/Kbuild 或 Darwin port binary，因此**不声称全部传递符号闭包穷尽**。本清单完成了所选执行链的子系统级依赖/语义审查；每行可包含一组共用契约的符号，不是逐个内联函数的 ABI 承诺。

分类：`P=PUBLIC_USABLE`（底座可用而非 Linux API 已实现），`A=SEMANTIC_ADAPTER_REQUIRED`，`U=PRIVATE_OR_UNSUPPORTED`，`N=NOT_CURRENTLY_SUPPORTABLE`（当前公开契约/工程能力下），`?=UNVERIFIED`。信心：H=已读源码可证依赖；M=Darwin 设计推论。所有 Darwin 映射均尚未实现。

### A–C：模块、PCI、driver model（直接依赖）

| Linux 符号/类型与来源 | 实际原因/语义 | 初始化前置 → 回收要求 | Darwin 候选/分类/信心 | 缺口与验证 |
|---|---|---|---|---|
| module_init/exit、init_funcs [L03] | 缓存→PCI→observation→PM 顺序，失败逆序 unwind | runtime/allocator 已就绪 → exit 前先断开设备 | kext 生命周期+有返回值 initializer registry / A/H | 不得用 void initializer 丢失失败；每阶段注入失败 |
| module_param_named/_unsafe、MODULE_* [L03][L50] | force_probe/固件路径/复位策略；owner 与配置身份 | 验证配置、限制特权 → 禁止在运行中危险重配 | 配置 manager / A/H | module_get/put 和每模块身份不等价于 NULL；卸载引用测试 |
| pci_driver、pci_dev、pciidlist [L04][L05][L06] | ID/class匹配与 probe/remove/PM | 原生 provider 有效、独占策略 → remove 后不再使用 pdev | IOPCIDevice+兼容对象 / A/H | ID 命中不证明配置适用；错卡/已有 owner拒绝 |
| pcim_enable_device、pci_set_master [L04] | 使能 PCI、启动 bus leading | owner+D0+资源合法 → 停 DMA后撤销/恢复命令位 | IOPCIDevice / A/H | open 不是 D0锁；实机停 DMA与配置快照 |
| xe_mmio_init、pci BAR [L04][L51] | GT寄存器/多tile MMIO，VRAM ready 等待 | BAR范围/cache有效、设备在线 → 撤销引用后释放map | mapDeviceMemoryWithRegister / A/H | BAR缩放/小窗口/rebar不能猜；边界与移除测试 |
| devm/drmm action、pcim [L08][L29] | 失败立即 action_or_reset；PCI devres与DRM对象寿命不同 | 按成功注册顺序保存回调 → 两个owner各自逆序释放 | 两层资源账本 / A/H | 不合并成“probe退出全free”；重复cleanup/延迟file引用 |
| struct device、kref、xe_file [L09][L43] | file/VM/queue相互保活；handle最后发布防UAF | per-open对象/ID表 → 最后引用才能destroy | retain/release+连接对象 / A/H | Linux struct不可cast OSObject；并发close/ID复用 |
| auxiliary bus / xe_heci_gsc_init [L01][L08] | GSC代理/父子设备路径仍在core | 平台能力条件与父寿命 → 子driver解绑先于parent释放 | 子IOService或显式拒绝 / ?/H | DG2具体分支已非固件表必需，不等于全部aux代码不可达；生成配置图 |
| sysfs/observation/coredump [L02][L08] | 属性、诊断、OA/perf接口，不是shader执行核心 | 有权限注册 → 注销后排空reader | registry/受限诊断 / A/M | 可不提供非必要接口，但明确feature拒绝，不以空成功替代 |
| PCI reset/PM callbacks [L04][L05][L46] | FLR/GT reset、电源上下文 | drain+可访问设备 → reset影响旧队列/VA generation | IOPCIFamily+厂商reset / A/H | 正常remove≠失联MMIO reset；AEC/PCI错误恢复闭包未完全审查 |

### D–F：内存、DMA、并发（直接调用及注明的传递依赖）

| Linux 符号/类型与来源 | 必需运行语义 | 初始化前置 → 回收要求 | Darwin 候选/分类/信心 | 缺口与验证 |
|---|---|---|---|---|
| kzalloc/kfree、slab、kvalloc/vmap [L44][L45][L03] | GFP上下文、对齐、零填充、失败；多页映射 | allocator和quota → 与实际分配器配对，slab在module exit清空 | IOMalloc/IOBufferMemoryDescriptor / A/H | wired内存非swappable；GFP_ATOMIC不得阻塞，OOM/配额 |
| struct page/folio、shmem（TTM传递）[L30] | 页引用、dirty、swap backing、迁移 | 页owner+backing → 所有映射/DMA释放后回收 | descriptor+自建页元数据 / A/H | 不能把Linux struct page地址当物理页；swap/shmem不等价，压力/迁移测试 |
| TTM placement/eviction、VRAM manager [L02][L08][L30] | system/VRAM放置、驱逐、迁移与reservation | region实际容量、TTM初始化 → fence完成后释放BO/manager | 保留TTM逻辑+Darwin backing / A/H | 固定所有BO会改变eviction语义及资源上限；小BAR/超额测试 |
| mmu_interval_notifier、HMM [L16][L17][L18][L32] | userptr地址失效序列、重试、blockable回调、repin | mm保活+notifier登记 → 注销等待回调与DMA取消 | 无已证明公开等价物 / N/H | wiring不提供失效通知；munmap/remap/进程退出与GPU并行是硬阻塞 |
| xe_pt / TLB invalidation [L39][L24] | GPU页表更新、fence依赖、reset协调；不是CPU mmap | BO/DMA/VA有效 → 失效确认后回收PT页 | GPU厂商逻辑+DMA / A/H | 不以CPU TLB flush代替GPU TLB；取消/重绑定/旧VA访问测试 |
| dma_map_sgtable/unmap [L15][L35] | orig_nents vs mapped nents、方向、返回错误、寿命 | backing锁页+device mask+mapper → 停DMA后unmap一次 | IODMACommand / A/H | IOVM地址≠physical≠CPU VA；分段/超mask/partial失败 |
| dma masks/coherent及缓存 [L08][L15] | 一致性、地址宽度、alignment、DMA属性 | 实际DG2范围+cache policy → complete/clear配对 | IOMapper/descriptor / A/M | 完整coherent与streaming调用闭包待编译证明；非identity IOMMU验证 |
| spin_lock_irqsave、mutex、rwsem [L17][L52][L20][L42] | IRQ与线程context、睡眠可否、内存ordering | 锁先init → callback/drain结束后destroy | lck/IOLock+上下文设计 / A/H | workloop gate不能替代所有spinlock；锁序、重入和CPU并行 |
| atomics、smp_wmb/mb [L20][L46] | stopped状态原子变换和可见性 | 关联状态不变量 → 无observer后free | 编译器atomics+架构barrier / A/H | volatile不是同步；memory ordering litmus+竞争回归 |
| wait_event/completion、wake_up [L20] | 条件与睡眠登记防丢唤醒、超时/取消 | waitqueue/条件锁 → 唤醒失效等待者并排空 | assert_wait/thread_block / A/H | waiter注册必须与条件联动；signal/interruption不完全等价 |
| workqueue/ordered/delayed work/timer [L23][L24][L41] | 顺序、重入、cancel_sync、reclaim、异步销毁 | runtime/worker → 不在自身worker等待自身；drain后卸载 | kernel线程/thread_call+manager / A/H | 原有CPU work queue不是完整Linux workqueue；自取消/超时/stop竞态 |
| RCU/call_rcu（fence释放）[L22] | 等所有reader过grace period再释放 | reader追踪/RCU运行 → 卸载等待callbacks | 新RCU语义实现 / A/H | 立即free不成立；原生OSObject引用不自动覆盖无锁reader |
| dma_fence/irq_work [L22][L42] | GPU seqno观察、signal/error、回调、延期释放 | fence context/IRQ/seqno backing → stop/finish、RCU回收 | 原语适配+实际中断 / A/H |软件signal可能是error，不能当GPU完成；timeout/reset并行 |

### G–I：DRM、固件、执行、用户边界

| Linux 符号/类型与来源 | 必需运行语义 | 初始化前置 → 回收要求 | Darwin 候选/分类/信心 | 缺口与验证 |
|---|---|---|---|---|
| drm_dev_register/unplug、GEM [L08][L09][L10] | 正确时点发布render接口，unplug后旧file仍有寿命 | 完成firmware/engine等初始化 → 关闭新请求、旧引用排空 | IOService+DRM契约 / A/H | register不能声明已执行；open/unplug/close并行 |
| DRM GPUVM/EXEC（传递）[L19][L31] | reservation、deadlock retry、VM/BO consistency | BO/VM references、wound-wait锁 → unlock与ref释放 | 保留核心逻辑/适配ww_mutex / A/H | 单个mutex不等价于ww retry；多BO反向锁序 |
| drm_sched_*、xe scheduler [L19][L34][L40] | job依赖、arm提交点、backend run/free/timedout | queue/LRC/fw+fence → stop/dereg、job drain、异步destroy | 调度核心+runtime适配 / A/H | after-arm错误交由scheduler清理，禁止随意free已发布job |
| syncobj/timeline/user fence [L09][L27][L33] | per-file handle、点值、wait/signal、error及导入导出 | fence/ID表 → waiter取消后释放 | 连接owned sync objects / A/H | fd transfer须明确ABI；semaphore不是timeline/fence |
| request_firmware、uC state [L11][L12][L13] | 固定blob验证/解析、upload/auth、GuC CT通信 | 磁盘root可用+trusted文件+MMIO/DMA → 清理fw BO、停worker | 受限vnode读取+原厂协议 / A/H | 所选blob/hash和认证未取证；缺失/损坏/版本不符必须fail closed |
| GuC CT/queue/ring/LRC [L13][L40] | context注册、ring emit、sched消息与G2H，默认GuC路径 | MMIO/GT/IRQ/PT/LRC均ready → disable/deregister并等回执/失败处理 | GPU专用实现+Darwin DMA/IRQ / A/H |不改force_execlist掩盖GuC缺口；固件ready与提交/readback分别验收 |
| IRQ install/synchronize/free [L21][L42] | MSI/MSI-X、mask、ack、completion、安全drain | handler对象与GT ready → mask/drain后free映射 | IOKit event source/workloop / A/H | Linux hardirq移至线程改变ack时机/锁上下文，需DG2验证 |
| GT reset/pagefault/recovery [L24][L20] | prepare-stop→清页故障/TLB→reset→重建提交 | 排空/停准入、device仍在线 → 旧fence error/VA明确失效 | 厂商协议+生命周期 / A/H | 不保证整卡reset/恢复；看门狗、超时、失败永久wedged |
| file_operations/open/postclose [L09] | 每file独立VM/queue，close假定ioctl已不改ID表 | open refs与准入gate → kill queues、close VM、最后file_put | devfs或UserClient，两者择一契约 / A/H | XNU入口不能照搬Linux fd锁假设；close/kill必须回归 |
| xe ioctl/UAPI/copy_from_user [L09][L26][L27][L38] | 固定宽度、reserved校验、extensions链和进程指针 | caller身份、size/次数quota → 回滚失败并不发布handle | 显式wire/或DRM ioctl bridge / A/H | 编码/errno/layout不自动兼容；fuzz与跨连接IDOR测试 |
| drm_gem_mmap、fault/munmap [L09][L30][X08] | offset→BO、保护、fault、迁移和invalidations | BO授权/引用与page backing → revocation/unmap完成后free | IOKit mapping+重定义传输 / A/H |不能仅提供ioctl就声称Linux ABI保持；mapping关闭/进程退出 |
| PRIME/dma-buf、sync_file [L01][L02][L09][L27] | fd backing共享、跨进程/设备及外部sync | fd引用/授权 → detach/unmap/ref归零 | 未确定fd机制 / N/H | 可拒绝外部功能并不广告，不能宣传完整ANV feature；link闭包与feature矩阵 |
| KMS/i915 display [L01][L02] | 完整display profile需要共享显示源、connector/atomic/audio | 初期profile明确关闭 → 无KMS/display验收 | 未来IOFramebuffer/IOSurface/AGDC / ?/H |关闭显示不是支持显示；后续独立scope |

## 覆盖缺口不是省略实现许可

当前未展开所有传递内联/API：Linux xarray/maple tree、RCU不同flavor、ww_mutex/lockdep、percpu、shmem swap、DMA allocation attributes、完整PCI AER和display闭包。源 include 及配置已说明这些边界需要下一轮预处理/编译与完整链接图。路线可行性受以上硬阻塞支配，不以“grep没有看到”认定无依赖。

受限第一实验可以研究 owned GEM BO + 非faulting VM；**原版HMM代码仍需要编译依赖**。若未来明确拒绝USERPTR/FAULT_MODE，必须同步更改版本化能力契约和ANV feature策略并获架构授权，不能悄悄删源或用空notifier函数让编译通过。
