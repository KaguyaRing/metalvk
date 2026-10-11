# 完整渲染管道：实施计划与验收

本计划延续 Metal → Vulkan 的厂商中立路线，不恢复已删除的自造 GPU 注册／命令 ABI。
状态须以证据更新，不能以代码目录、编译成功或相同类实例大小代替运行验收。

## 已有实测（2026-10-11）

- Windows Vulkan ICD：NVIDIA GeForce RTX 4060 Laptop GPU，`10de:28e0`。
- 驱动：NVIDIA `616.92`，Vulkan API `1.4.351`；SDK `1.4.363.0`。
- 计算：1、63、64、65、257、4097 元素与未写尾部全部通过。
- 图形：顶点／片元着色器 → graphics pipeline → draw → render-pass 出站依赖 → image-to-buffer copy → host barrier → fence → 回读；全部 4096 像素通过。
- 两种验证均启用 Khronos 普通／同步验证，错误 0、警告 0。
- 本机 `vendor=0x1002` 返回初始化失败，未获得 AMD 执行证据，不能宣称 AMD 已支持。
- 本机 `vendor=0x8086` 同样未找到符合条件的设备，未获得 Intel 执行证据。
- 根因纠正：此前未声明符号是 `mtkv_`／`mtvk_` 字母转置。块注释、C/C++ 前端和编译器原型识别不是根因。

### 已完成的 CI 证据

- [Vulkan 编译及 ASan/UBSan 回归](https://github.com/KaguyaRing/metalvk/actions/runs/38110893338)：Linux、macOS 15/26 全部通过。
- [原生共享库／kext／App 构建](https://github.com/KaguyaRing/metalvk/actions/runs/38110894139)：15.7.9 (`24G830`) 与 26.6.1 (`25G76`) x86_64 宿主通过；没有加载 kext。
- [系统 Metal 验收客户端编译与发现](https://github.com/KaguyaRing/metalvk/actions/runs/38111095280)：两宿主通过；发现的设备是 Apple Paravirtual，不是 metalvk 设备。
- [系统 Metal 参考渲染](https://github.com/KaguyaRing/metalvk/actions/runs/38111292491)：两宿主的 Apple Paravirtual 设备均通过 4096 像素校验；不是物理 GPU 或 metalvk 支持证据。

## 模块边界

```text
系统 Metal 客户端
  → Metal 用户态适配／对象与资源语义
  → MSL/AIR 着色器转换与功能核验
  → Vulkan API／Loader／厂商 ICD（用户态）
  → 厂商平台适配（内存、固件、提交、同步）
  → 共享内核平台服务 + 厂商 kext
  → GPU 执行
  → IOSurface／呈现同步
  → IOFramebuffer／显示协商／显示硬件
```

- `libmetalvk.a`：SDK-free 基础层、宿主内存配额、CPU 工作记账、deadline 运算和显示后端控制 API；仍无平台／厂商依赖。
- `libmetalvk-vulkan.a`：可选用户态 Vulkan 上下文，按真实队列能力选设备；不进入内核。
- `libmetalvk-kernel-platform.a`：原生内核公共模块；包含 PCI 快照、生命周期、只读诊断 UserClient，以及独立的 wired 宿主内存域、CPU 工作队列与 uptime。厂商子类保留精确匹配。内存／工作服务尚未绑定 RM／用户态入口。静态链接进 kext，不是独立加载的驱动。
- `libmetalvk-diagnostics.a`：独立线协议；诊断可用不代表 Metal 可用。
- `libmetalvk-nvidia.a`／`libmetalvk-nvidia-kernel.a`：独立 NVIDIA PCI／boot 身份验证与原生只读硬件入口，后者链接进 kext；不进入中立核心，默认关闭 MMIO probe。详见 [NVIDIA 后端](nvidia-backend.md)，尚未启动 RM／GSP／DMA 或通过 NVIDIA 真机加载。
- 后续采用平台管理器协调内存、提交、同步、显示、恢复子管理器；实现具体功能时才增加模块，不增加假成功入口。

## 按依赖推进的门槛

| 阶段 | 必须完成的功能 | 通过证据 | 当前状态 |
|---|---|---|---|
| R0 | 官方 Vulkan 的真实 compute + graphics 闭环 | 硬件身份、全量回读、零验证消息 | Windows NVIDIA 已通过 |
| R1 | 可复用用户态／内核模块与真实 kext 构建 | 核心隔离、回归测试、原生链接／签名检查 | 首批模块与原生 CI 通过；不等于加载／加速 |
| R2 | macOS 厂商执行路径 | 真实 GPU 初始化、资源分配、提交与 fence 回读；进程退出后无悬挂 DMA | 宿主内存前置服务已实现；GPU 执行未实现 |
| R3 | Metal 离屏闭环 | 经 Metal API 创建资源与着色器，真实 draw/readback；不 CPU 伪造，不绕回 MoltenVK | 未实现 |
| R4 | 系统设备发现／跨进程资源 | 未修改应用能发现设备；权限隔离、IOSurface 共享与同步验收 | 未实现 |
| R5 | 可见呈现与显示管道 | WindowServer／framebuffer 呈现、模式协商、vblank、热插拔、睡眠唤醒 | 通用参数／事件状态管理已实现；硬件呈现及系统接线未实现 |
| R6 | HDR／DisplayID／刷新率与故障恢复 | 元数据方向与格式有证据；模式切换、高刷、GPU hang/reset、恢复测试 | 通用 HDR／显示复位 API 与首批 EDID／CTA／DisplayID 解析已实现；硬件 HDR 和整 GPU 恢复未实现 |
| R7 | macOS 15/26 与多厂商覆盖 | 逐 OS build／GPU／驱动组合真机记录 | 未验收 |

R2 必须先明确厂商 ICD 在 macOS 上的可用执行后端、固件依赖、许可证与 OS 适配边界。
Windows 的厂商 ICD 不可直接拿来当 macOS 内核执行实现。Vulkan Loader 本身也不提供硬件驱动。
MSL 源码成功转换不等于已编译 AIR/metallib 的系统应用可用，两类输入须分别处理和验证。

### R2 前置：宿主内存

已加入独立的纯 C 预算／关闭状态与原生 wired 内存域，详见 [宿主内存模块](kernel-memory.md)。域关闭与待发布分配同锁核验，失败回滚、不释放仍被引用的 backing；没有将 CPU 指针当作 DMA 地址，也未声称已绑定官方 RM 回调。原生编译／符号检查通过不等于已经在目标内核中执行这些分配／释放方法。

后续已加入[有界工作队列、延后释放与 uptime](kernel-runtime.md)。回调锁外执行，停止接受与投递同锁，close 超时保留在途资源；未实现 IRQ 分配池或驱动卸载的模块级静止屏障，也未启用 DMA／GSP／GPU 命令。

## 系统 Metal 验收门

显示后端现在可接入[模式／HDR、运行时信号与显示复位控制 API](display-backend.md)。生产库执行真实校验、事务状态与厂商回调，不包含模拟成功后端；缺失硬件回调拒绝操作。测试替身仅验证 API 契约，不升级 R5／R6 的硬件验收状态，也不说明 Apple 私有 HDR 接口已核验。

首批[EDID／CTA／DisplayID 字节解析](edid.md)已接入同一通用／内核库，使用真实 Windows 屏幕缓存解析高刷时序和 HDR 能力，并拒绝截断／额外块。能力结果必须与厂商核验结果取交集；不把 sink EDID 的 desired luminance 当作内容 mastering metadata，不提供默认屏幕或猜测时序。尚需完整标准时序覆盖、flip／vblank 通用完成管理，再进入厂商实际读取／链路／扫描输出阶段。

独立客户端 `validation/metal/render.mm` 已实现显式 registry ID 选择、MSL 运行时编译、真实 Metal render/blit、限时完成等待与全图校验；使用方法见 [独立组件](reference-components.md)。它不是 Metal → Vulkan 翻译器，也不注册设备。后续 R3/R4 必须以自己的 accelerator registry ID 运行，不准回落到系统默认 GPU。

CI 的 Apple Paravirtual 参考渲染只检查验收客户端本身；不升级 R2/R3 的项目实现状态，也不作为物理 GPU 证据。

## 上游观察约束

`Example/nvidia-macos-driver` 已同步 `v1.12.0`（`cb41be5`），仅阅读发布说明／架构，不引用实现代码。其发布说明声称新增 headless／MUXless 双 GPU 路径，并明确范围为 macOS 15；高于 60 Hz 的黑屏／闪烁等问题仍在开放列表。它不能证明 metalvk 已具备上述能力或能兼容 macOS 26。

架构上的后续验收须分别区分“独显计算／离屏”“另一块 GPU 扫描输出”和“直接显示”；对内屏、外屏、MUXless、双 GPU 跨设备复制、刷新率／复位分别建测试，不以单一 framebuffer 路径代表全部组合。

## ABI 与验证约束

- Recovery 材料只供分析；macOS 15/26 的类实例大小一致不证明 vtable、成员偏移或行为相同。
- 原生 CI 编译宿主为 15/26，使用固定 MacKernelSDK；不表示私有图形 ABI、kext 可加载性或 GPU 执行已通过。
- 未验证的 OS build／私有 ABI 禁止进入寄存器写入、DMA 或 GPU 提交路径。
- 每个新增特权接口必须明确身份校验、句柄所有者、资源上限、权限、退出回收与复位行为，不暴露裸内核地址。
- 所有阶段分别记录“编译”“加载”“执行”“显示”；缺证据不能越级标记完成。

## 本轮安全复核范围

范围：共享 Vulkan 上下文、验证输入／回读／清理、共享 PCI 服务与诊断 UserClient、受影响构建脚本。

- 外部入口：CLI 参数、SPIR-V 文件、ICD 查询、IOKit 请求及 PCI 配置读回。
- 资产：主机内存、Vulkan 对象、内核内存和设备生命周期；特权操作本轮仅只读 PCI 查询。
- 验证：严格厂商 ID、文件／ID bound 上限、指令长度与截断检查、逐像素损坏回归、Clang 静态分析、硬件同步验证、原有三种配置回归；CI 补充 ASan/UBSan。
- 映射不经整数或不兼容指针的类型别名；非 coherent 内存显式 invalidate；失败资源按提交状态清理。
- 真实 ICD 下的测试专用故障注入已通过：图像分配返回 OOM、提交后 fence 返回 timeout、排空返回异常。测试断言提交状态、排空次数、fence 销毁和 context close 次数；永久排空异常时不销毁／关闭在途资源，交给测试进程退出回收。测试不作为成功渲染证据，也不导入生产库。
- 只读 UserClient 维持本地用户权限、固定请求大小、拒绝描述符／异步请求、关闭状态与设备离线检查；无新写入能力。
- 不进行生产或第三方设备攻击测试，不调整 U 盘／OpenCore，不自动加载 kext。

残余限制：SPIR-V 结构检查不是完整语义验证，生成着色器另需 `spirv-val`；本工具不作为任意不可信着色器执行沙箱。返回值注入不等于真实物理 OOM、GPU 丢失／永久挂起测试。内核加载／热拔插、跨进程共享、显示恢复、主机磁盘满和网络超时仍无对应故障注入证据，须在相关阶段的隔离环境补齐。
