# metalvk

[English](README.md)

**开发中的厂商中立 GPU 通用层与渲染管道。** 目标是在 macOS 15／26 上让 NVIDIA、AMD、Intel GPU 实际执行 Metal 渲染，支持范围按 GPU 架构、PCI ID 与 OS build 逐项核验。这是覆盖目标，不代表这些厂商的全部型号已支持。仅有 Vulkan 驱动不等于完成 macOS 系统接入。

中立执行接口采用 **Vulkan 规范**，不自造 GPU ABI。早期的 `mtvk_gpu_*` 后端注册／转发 ABI 已移除。

## 当前状态

**macOS 完整渲染管道尚未拉通，目前没有可用 GPU 驱动，也没有通过验收的 macOS Metal 硬件组合。** 编译、离线着色器编译、kext 加载、GPU 执行和系统显示分别验收。

| 组件／证据 | 已验证范围 | 不能证明什么 |
|---|---|---|
| Vulkan 计算与图形 | Windows RTX 4060 Laptop、现有 NVIDIA ICD；完整回读，验证层错误／警告为零 | 本项目 Darwin 驱动或 Metal 翻译 |
| NVK／NAK 基线 | 完整 Linux ICD 构建、CPU suite、SM89 离线编译与主机适配器一致性测试 | Darwin ICD 或在 macOS 上执行 NAK 输出 |
| NVIDIA 诊断 kext | macOS 15／26 Intel 原生构建、ad-hoc 签名检查；旧 CI 产物导入符号匹配参考导出清单 | 目标 Kernel Collection 接纳、真机加载、PCI 服务启动或 GPU 执行 |
| 系统 Metal 验收客户端 | Apple Paravirtual 参考渲染与全图校验 | 本项目 Metal 加速或物理 GPU 支持 |
| Metal 翻译与硬件后端 | 尚未形成完整实现 | Metal 渲染、WindowServer 呈现或显示输出 |

NVIDIA kext 当前只匹配 **`10de:28e0`**，只读 BAR0 probe **默认关闭**。AMD／Intel 硬件路径尚未验收。上述独立结果不能拼成“完整驱动已可用”的声明。

## 目标渲染线路与验收

```text
Metal 应用／系统客户端
  → 本项目 Metal 设备接入与 Metal → Vulkan 翻译
  → Vulkan Loader／厂商 ICD
  → 厂商 Darwin 资源、提交与同步后端
  → GPU 执行
  → 回读或 IOSurface／呈现／显示
```

Vulkan 是内部执行接口，**不是 Vulkan → Apple Metal**。MoltenVK 无法提供缺失的 NVIDIA macOS 驱动，共享硬件上下文会拒绝它。MSL 源码翻译和预编译 AIR／metallib 兼容需要分别实现、验证。

- 显式选择本项目目标 registry ID，核对实际 PCI／GPU 身份。禁止以其他系统 GPU、软件光栅化或 MoltenVK 回退满足验收。
- 每个声明支持的 GPU／OS 组合须通过 Metal 创建资源与着色器、真实 draw、完成等待与全图回读。最小图案只证明对应功能子集，不代表任意 Metal 应用兼容。
- 分别记录编译、加载、Vulkan 执行、Metal 离屏、未修改应用设备发现和系统显示。离屏通过不代表 WindowServer、HDR、高刷、睡眠唤醒或整卡 reset 通过。
- 在发布硬件执行能力前，先实现并核验所有权、电源管理、可信固件、DMA／IOMMU、GPU 内存／VA、提交与同步。

详见[渲染验收门槛](docs/rendering-plan.md)、[NVIDIA 接入](docs/nvidia-nvk.md)及[内核所有权／运行时限制](docs/kernel-runtime.md)。缺少目标硬件或测试条件应标记为**未验收**，不能标记通过。

## 中立核心

默认产物 `libmetalvk.a` 编译基础层、纯宿主内存配额、有界 CPU 工作记账与 deadline 运算，只使用标准 C 头文件；不含厂商 SDK、硬件驱动、内核代码、私有 ioctl、Vulkan 实现或着色器编译器。诊断协议是独立产物 `libmetalvk-diagnostics.a`。

还已实现[显示后端控制 API](docs/display-backend.md)：模式／HDR 校验与传递、显示复位／配置恢复、运行时信号及失败关闭状态管理。硬件回调必须由厂商后端提供，没有默认模拟成功路径；目前不宣称物理显示／HDR 已支持。

已增加[真实 EDID／CTA／DisplayID 字节解析](docs/edid.md)，并与后端核验的模式／HDR／位深能力取交集。支持的格式有明确范围，未处理特性会报告，不伪造数据；已测试 Windows 真实屏幕缓存，不等于硬件链路控制。

```text
include/                     src/                      （模块布局镜像）
  platform.h                   base/status.c
  metalvk.h                    ipc/diagnostics.c
  base/  init.h status.h span.h
  memory/ init.h budget.h       memory/budget.c
  runtime/ init.h work.h time.h runtime/work.c time.c
  display/ init.h control.h edid.h display/control.c validation.c edid.c cta.c displayid.c edid-capabilities.c
  ipc/   init.h diagnostics.h
```

## 语言

核心基础层使用 **C11**，标准 C 头文件，无平台依赖。kext / Metal 接入等需要 IOKit 或 Objective-C 运行时的部分可使用 **C++17 / Objective-C++**，与核心保持独立编译单元。

每个模块使用 `init.h` 聚合头文件；`metalvk.h` 聚合公共模块。后续模块沿用同一结构。

## 编译与验收

默认构建需要 C11 编译器、ar 和 make；**不需要厂商 SDK、Vulkan SDK 或内核 SDK**。

宿主测试还需 C++17 编译器执行所有者锁并发测试；库本身仍是 C11。

```sh
make CONFIG=Release
make test CONFIG=Debug
make test CONFIG=Release
make test CONFIG=Alpha
python3 tools/check_core_boundary.py --archive target/Release/libmetalvk.a
```

Windows 使用 `mingw32-make` 与 `AR=llvm-ar`。

`tools/check_core_boundary.py` 校验默认库的源码集合、头文件依赖、归档成员与“无厂商/无平台条件编译”约束。`.github/workflows/core.yml` 在 Linux/macOS 上执行这些检查，并进行 freestanding 语法与 C++ 公共头检查。

## 独立组件（不进入默认库）

- `backends/nvidia/`：独立 PCI／AD107 boot register 校验库、共享 transport／discovery／session 支持，以及可显式启用的只读 BAR0 kext probe。共享分发器尚未接入真实 NVIDIA UserClient，尚无 RM／GSP 启动、DMA、GPU 执行或显示支持。详见 [NVIDIA 后端范围](docs/nvidia-backend.md)。`make test-nvidia CONFIG=Release` 运行宿主夹具测试，不是硬件测试。
- `include/vk/`、`src/vk/`：共享硬件 Vulkan 上下文及可复用[离屏渲染／回读目标](docs/offscreen-rendering.md)，独立构建为 `libmetalvk-vulkan.a`，需要 Vulkan SDK／Loader 与可用硬件 ICD。真实原生 pipeline／fence／多帧回读已在 Windows NVIDIA 通过，不代表 macOS NVIDIA 后端通过。
- [NVK／NAK 基线](docs/nvidia-nvk.md)：固定 Mesa 26.2.4，完整 Linux ICD 构建及仓库顶点／片元 shader 离线编译为 SM89 原生代码已通过。检查器核验操作名称／类别（30 个上游操作映射至 28 个传输操作），不是完整字段或 RM 语义验证；适配器自测是主机 loopback，不是内核 IPC。Darwin 端点接线、RM／GSP 执行与 macOS ICD 未实现；Linux 产物不是 macOS 驱动。
- `kexts/Platform/`：共享 PCI 快照、生命周期、只读 UserClient、受预算约束的 wired 宿主内存域、延后 CPU 工作与 uptime 服务，构建为 `libmetalvk-kernel-platform.a` 并链接进诊断 kext；内存／工作服务尚未绑定 RM 或暴露给用户态，尚无 GPU 执行能力。详见[所有权与执行限制](docs/kernel-runtime.md)。
- `app/`、`include/ipc`、`src/ipc`：只读诊断 App 与独立诊断协议。
- `validation/vulkan/`：真实 Vulkan ICD 的验证工具，不是通用层或生产后端。编译成功不证明 GPU 执行或 Metal 接入成功。
- `.github/workflows/reference-macos.yml`：可手动执行或由发布流程复用的平台参考构建。用法见 [独立组件](docs/reference-components.md)。
- `.github/workflows/release.yml`：按不可变标签在 macOS 15／26 Intel 构建，并发布仅供诊断的 GitHub prerelease。压缩包保留 bundle 权限，附构建信息、警告、许可证与校验和；不自动安装或加载 kext。详见[开发产物发布](docs/development-releases.md)。

RTX 4060 Laptop GPU 已在 Windows 上通过真实计算回读与 64×64 离屏图形测试，4096 像素全部校验，验证层错误／警告均为零。这不证明 macOS 或系统 Metal 支持。详见 [完整渲染计划与验收门槛](docs/rendering-plan.md)。

## 规划中的模块（尚未实现）

逐步实现完整可用的模块，不创建空驱动：

```text
include|src/  platform/  版本探测与私有 ABI 表查询
              display/   进一步实现 EDID/DisplayID 解析、链路编码与呈现
              accel/     IOGraphicsAccelerator2 接入
              agdc/      AppleGraphicsDeviceControl 接入
              metal/     Metal 语义层（Metal → Vulkan）
              vk/        后续 Vulkan 资源／提交模块
              backend/   厂商专属平台接入
data/abi/     kext ABI 表（按 OS build 键，与逻辑分离）
kexts/Platform/   后续内核资源与恢复服务
kexts/Display/    framebuffer / accelerator / agdc
```

硬件支持仍需各厂商对应 kext、共享平台接入及逐组合实机测试。**`v0.1.0` 是开发里程碑标签，不代表可用 GPU 驱动，也不是完整 Metal 渲染已验收的版本。**

## 模块与 kext 开发标准

- 保持头／源目录镜像与模块聚合头；中立核心、Vulkan 用户态、公共内核平台、厂商后端和诊断 App 分别维护。
- 分帧、discovery 能力策略与 session 分发分离。后续 lifecycle、RM／GSP、DMA／BO、GPU VA、queue／IRQ、sync 与显示管理器须明确所有权及清理顺序，不增加空成功 handler。
- 诊断 UserClient 保持只读。GPU 执行使用独立且经过权限检查的厂商接口，明确连接所属句柄、配额、退出／复位行为，禁止暴露裸内核指针。
- 静态导出匹配或 ad-hoc 签名不保证加载。目标机授权、Kernel Collection 链接、模块加载、PCI 接线与诊断往返分别取证；历史 CI 产物不证明后续工作区改动通过。
- 不为让构建／测试通过而加载 kext、启用 MMIO 写入／DMA、调整启动安全策略或修改 OpenCore。硬件测试须有明确授权、隔离目标机和恢复方案。

Mesa 及其依赖**不是统一 MIT**。项目中立核心的许可证不替代上游 notices；正式二进制分发仍需最终依赖／许可审查。
