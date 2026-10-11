# Darwin GPU kext：生命周期、PM 与 DMA 研究

研究日期：2026-10-12。范围为公开 Apple 源码、固定官方 NVIDIA RM、项目现有 kext。
本轮只读研究，不安装／加载 kext、不改变启动安全策略、不访问硬件或执行寄存器写入。
此前未提交的模块拆分与传输修复保留，不以旧 CI 证明这些修改通过原生构建。

## 1. 依据与版本

| 来源 | 固定版本／提交 | 本轮核对范围 |
|---|---|---|
| Apple XNU，macOS 15 参考 | `xnu-11417.140.69` / `43a90889846e00bfb5cf1d255cdc0a701a1e05a4` | IOService、IOUserClient、IODMACommand、OSByteOrder |
| Apple XNU，macOS 26 参考 | `xnu-12377.1.9` / `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` | 上述生命周期／DMA API 的存在与说明 |
| Apple IOPCIFamily，15 参考 | `681.140.3` / `0eeb801b9d471778f5f177bd2e6c395a18f0ad95` | open／close、PCI power、command 寄存器 |
| Apple IOPCIFamily，26 参考 | `726.0.5` / `4822b27a36e2de70e231ecf2bf3021384fab6ec2` | 固定来源；尚未逐函数比较全部行为 |
| NVIDIA open-gpu-kernel-modules | `610.57.04` / `e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb` | Linux PM、adapter shutdown、DMA、OS 接口 |

本地来源在 `Example/xnu-macos15`、`Example/xnu`、`Example/IOPCIFamily-macos15`、
`Example/IOPCIFamily`、`Example/open-gpu-kernel-modules`。固定源码是研究基线，
不证明目标 15／26 的所有 build、私有 Metal ABI 或实际加载行为相同。

公开依据：

- [Apple：Securely extending the kernel](https://support.apple.com/guide/security/securely-extending-the-kernel-sec8e454101b/web)。
- [15 参考 IOService.h](https://github.com/apple-oss-distributions/xnu/blob/43a90889846e00bfb5cf1d255cdc0a701a1e05a4/iokit/IOKit/IOService.h)。
- [15 参考 IODMACommand.h](https://github.com/apple-oss-distributions/xnu/blob/43a90889846e00bfb5cf1d255cdc0a701a1e05a4/iokit/IOKit/IODMACommand.h)。
- [15 参考 IOPCIDevice.cpp](https://github.com/apple-oss-distributions/IOPCIFamily/blob/0eeb801b9d471778f5f177bd2e6c395a18f0ad95/IOPCIDevice.cpp)。
- [固定 NVIDIA nv.c](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb/kernel-open/nvidia/nv.c)。

## 2. 加载不等于执行

Apple 文档说明：macOS 11 起启用的第三方 kext 纳入 AuxKC，涉及用户批准与重启；
Apple silicon 的 kext 启用另有 1TR／Reduced Security／LocalPolicy 要求。
不能把 Apple silicon 的全部设置步骤直接套用到 Intel 目标机。
文档存在开发测试流程不等于本项目可在任意机器加载。

当前构建明确 `-arch x86_64`，Info.plist 精确匹配 `10de:28e0`；
`OSBundleLibraries` 仅列 IOKit／libkern／mach／IOPCIFamily，尚无 accelerator 注册。
`OSBundleRequired=Root` 不是用户批准、安全策略或 AuxKC 验证的替代品。
CI 的 `codesign --sign -` 与 undefined-symbol 筛选只是开发产物检查，
不是 Developer ID 分发签名、目标 Kernel Collection 的完整链接验证或真机加载证据。

纠正平台推断：Apple silicon 并非“不能加载第三方 kext”；Apple 文档明确其授权机制。
但本项目无 arm64 GPU 后端，且第三方 GPU 总线／硬件／Metal 接入没有验收，不能因此宣称支持。

## 3. 当前实际行为

| 模块 | 已做 | 未做 |
|---|---|---|
| NVIDIA Driver | 精确匹配、显式开关、身份属性、启动公共服务 | 长期 provider ownership、PM、RM／GSP、IRQ、GPU 执行 |
| Probe | 临时 open、只读 4096-byte BAR0 映射、前后配置与 boot 比对、RAII 清理 | PM 协调、设备移除排空、安全写入与 DMA |
| Platform device | PCI 缓存、锁保护 ready、诊断 client 创建 | 执行生命周期管理器、请求排空、PM 状态 |
| Diagnostic client | local-user 校验、固定大小请求、拒绝 descriptor／异步请求、关闭标记 | NVIDIA discovery、BO／VA／queue／fence、连接配额 |
| Memory／Work | wired CPU 内存域与受限 CPU 工作队列 | 真实 device DMA／IOMMU、中断退出协调、RM OS 回调接线 |

`IOMatchCategory=MetalVKDiagnostics` 是匹配分类，不能代替硬件所有权；
成为同一 provider 的 client 并不自动获得独占寄存器访问权。
目前诊断与可选 probe 不足以升级为执行驱动。

## 4. 已核实的 Apple 语义

### Provider ownership

15 参考 `IOService.cpp:3612` 的默认 `handleOpen()` 设置 owner；
`IOPCIDevice.cpp:1919` 先调用 superclass，成功后保存 session options 并处理资源／mapper 属性。
因此 open 是访问所有权，不是电源锁，也不是设备仍连接的保证。
probe 的 open 只持续到函数退出；后续执行不能假定仍持有它。
禁止使用 seize 抢占已有 owner 来让测试通过。

`IOPCIDevice.cpp:1973` 的 DriverKit session close 分支会清理 bus-leading／memory command 位，
并更新配置 shadow；当前普通 kext open 没有传该 session option，不能指望 close 自动停 DMA。
普通 close 也会处理 mapper 相关属性，所以不能将 provider open／close 泛化成无副作用的纯读。

### PM 与终止

`IOService.h:1834–1909,2099–2107`：PMinit 必须与 PMstop 配对；通常配合 joinPMtree／registerPowerDriver。
setPowerState 在独立线程上下文调用；完成时返回 implied ack，未完成时返回预期微秒上限并随后 ack。
PMstop 保证返回后不再进入 PM 方法，但它不替驱动停止 GPU DMA 或排空外部请求。
不能为尚未实现的硬件电源切换直接返回成功 ack。

`IOService.h:471–497`：willTerminate 通知按 root-to-leaf，didTerminate 按 leaf-to-root。
pending I/O 的 defer 与 provider 是否 open 有具体标准，不能随意 defer 以永久保留对象。
必须结合真实 provider/IOService 实现设计关闭顺序；本轮未证明所有终止竞态已解决。

### 用户进程退出

15 参考 `IOUserClient.cpp:1961` 的默认 clientDied 在内部 closed 转换成功后调用 clientClose。
当前诊断 client 的 override 可通过此路径关闭；无需仅因没有 clientDied override 就判定遗漏。
但未来执行连接仍须有幂等关闭、在途引用和资源排空，不能用 client free 直接释放 GPU 仍在使用的 backing。
IOUserClient 默认 IPC locking 保护相关入口，不替代跨 client 的 device／RM 锁或 GPU 同步。

### MMIO 与 DMA

15 参考 `OSByteOrder.h:112–117` 的 _OSReadInt32 是 volatile 32-bit 读取；
x86 little-endian 的 OSReadLittleInt32 使用该路径。当前 probe 地址对齐与映射范围检查有依据，
但内存读屏障与前后快照仍不能阻止下一刻断电／移除。

`IODMACommand.h:304–349`：setMemoryDescriptor 默认 autoPrepare；prepare 可能阻塞，
不得在 gated workloop action、自旋锁／中断上下文做这类准备。prepare 本身不是通用线程安全操作。
prepare／complete 必须配对；clearMemoryDescriptor 默认可自动 complete 并释放映射资源。
gen64IOVMSegments 返回 device I/O 地址片段；CPU 指针或 getBytesNoCopy 不等于 DMA 地址。
地址宽度、mapper、段数量／长度、方向、cache、映射范围都需匹配目标 GPU，不能猜测常量。

## 5. 与 NVIDIA RM 的实际差距

固定 `kernel-open/nvidia/nv.c:4418–4449` 在 standby／hibernate 前排空 deferred open 队列，
调用 rm_power_management 并停止 bottom-half 队列；resume 则重建队列后恢复 RM。
Linux 的冻结用户态／nv_system_pm_lock 条件不能直接搬到 Darwin，必须重新建立准入门控。

`nv.c:1988–2041` 的 adapter shutdown 包含 rm_disable_adapter、停止队列、清理中断与锁、
rm_shutdown_adapter；可选 FLR 还有额外条件。这里是依赖研究，不是可复制的 Darwin 停止模板。

`nv-dma.c:674–723` 将 DMA mapping 元数据保存在 priv，映射失败恢复状态，
解除映射失败时不盲目释放它；Linux scatterlist／dma_map_sg 的 ownership 与粒度
仍需逐项映射到 Darwin descriptor／IODMACommand。已有 wired allocator 不能替代这些实现。

## 6. 模块化接线标准（待实现，不是现有能力）

维持公共 Platform 与 NVIDIA backend 隔离，不新增跨厂商自造提交 ABI：

- 公共 lifecycle 管理器：请求准入、在途记账、停止原因、generation 与等待者完成。
- 厂商 PM／ownership 管理器：provider open、power-plane 接线、真实电源与设备移除协调。
- RM／GSP 管理器：OS 回调、可信固件、初始化与硬件关闭，固件失败不启用提交。
- DMA／BO、GPU VA、queue／IRQ、sync 管理器：各自持有资源，父管理器按依赖协调。
- 独立 NVIDIA UserClient：真实 discovery 与权限受限的厂商操作；保持诊断 client 只读。

IOWorkLoop／IOCommandGate 可用于状态串行化，但不把所有阻塞 RM／DMA 操作塞进 gate。
两阶段操作需先保留 generation／在途引用，锁外执行阻塞准备，再回管理器核验并提交；
设备状态改变则回滚未发布资源，已提交资源必须先证明硬件不再访问。
状态检查与准入记账须在同一同步范围完成，不能在锁外检查 ready 后直接触发 DMA。

下表是需要实现的状态转换，不是要先添加假 handler：

| 场景 | 先做 | 退出条件 |
|---|---|---|
| 初始化 | 校验 OS／设备，建立 ownership／PM，再初始化 RM 和资源 | 全部必要子系统就绪才开放执行 |
| 休眠 | 关闭新请求，协调在途提交与等待，按 RM 标准进入省电 | 真正完成后 ack；超时明确失败／恢复策略 |
| 唤醒 | 核验 provider／电源，恢复 RM、映射与队列 | 旧 generation 句柄处理明确后重开准入 |
| 进程退出 | 关该连接的准入，撤销句柄和等待者 | backing 不再被 GPU 使用才能释放 |
| 热拔插／GPU lost | 立即关闭准入、失效设备 generation，不再访问寄存器 | 确认 I/O／DMA 停止，再清理；不能对失联设备强行 MMIO reset |
| 卸载 | 关全局准入、排空 callback／IRQ／RM 和所有 client | 无硬件访问与代码回调后方可释放和卸载 |

## 7. 后续研究与验证门

1. 查询目标真机 OS build／架构／安全策略与 KC 依赖，不自动修改设置。
2. 按固定 IOPCIFamily 核实 PM／mapper／interrupt 入口和真正导出符号；
   核对新旧版本差异，不能只用 header 存在证明 KPI 可链接。
3. 列全 RM OS 回调、线程上下文、返回值与 teardown 依赖，识别还没有 Darwin 对应能力的项。
4. 完成 discovery 显式 wire 编码与字段校验后，才做独立 UserClient 的只读往返。
5. 生命周期先做宿主状态机／竞争／故障回归，再进行 15／26 原生构建与隔离真机加载。
6. 真机故障覆盖：连接关闭／kill、启动中失败、provider 忙、睡眠中提交、热拔插、DMA map 失败、
   IRQ／callback 未退出、fence timeout、GPU lost；每种都有资源与失败状态检查，不能只观察未 panic。

现阶段结论：模块布局已有基础，但 ownership／PM／终止协调是 RM 与 DMA 接线前的硬缺口。
本轮没有新增执行能力；所有加载、动态电源、硬件恢复与 Metal 支持仍未验收。
