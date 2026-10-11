# metalvk

[English](README.md)

**厂商中立的 GPU 通用层与渲染管道。** 目标是将 Metal 语义转换到 Vulkan，并将受支持的设备接入系统 Metal。仅有 Vulkan 驱动不等于完成 macOS 系统接入。

中立执行接口采用 **Vulkan 规范**，不自造 GPU ABI。早期的 `mtvk_gpu_*` 后端注册／转发 ABI 已移除。

## 当前状态

默认产物 `libmetalvk.a` 编译基础层、纯宿主内存配额、有界 CPU 工作记账与 deadline 运算，只使用标准 C 头文件；不含厂商 SDK、硬件驱动、内核代码、私有 ioctl、Vulkan 实现或着色器编译器。诊断协议是独立产物 `libmetalvk-diagnostics.a`。

还已实现[显示后端控制 API](docs/display-backend.md)：模式／HDR 校验与传递、显示复位／配置恢复、运行时信号及失败关闭状态管理。硬件回调必须由厂商后端提供，没有默认模拟成功路径；目前不宣称物理显示／HDR 已支持。

已增加[真实 EDID／CTA／DisplayID 字节解析](docs/edid.md)，并与后端核验的模式／HDR／位深能力取交集。支持的格式有明确边界，未处理特性会报告，不伪造数据；已测试 Windows 真实屏幕缓存，不等于硬件链路控制。

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

- `backends/nvidia/`：独立 PCI／AD107 boot register 校验库及可显式启用的只读 BAR0 kext probe；默认关闭，尚无 RM／GSP 启动、DMA、GPU 执行或显示支持。详见 [NVIDIA 后端边界](docs/nvidia-backend.md)。`make test-nvidia CONFIG=Release` 运行宿主夹具测试，不是硬件测试。
- `include/vk/`、`src/vk/`：共享硬件 Vulkan 上下文及可复用[离屏渲染／回读目标](docs/offscreen-rendering.md)，独立构建为 `libmetalvk-vulkan.a`，需要 Vulkan SDK／Loader 与可用硬件 ICD。真实原生 pipeline／fence／多帧回读已在 Windows NVIDIA 通过，不代表 macOS NVIDIA 后端通过。
- `kexts/Platform/`：共享 PCI 快照、生命周期、只读 UserClient、受预算约束的 wired 宿主内存域、延后 CPU 工作与 uptime 服务，构建为 `libmetalvk-kernel-platform.a` 并链接进诊断 kext；内存／工作服务尚未绑定 RM 或暴露给用户态，尚无 GPU 执行能力。详见[所有权与执行限制](docs/kernel-runtime.md)。
- `app/`、`include/ipc`、`src/ipc`：只读诊断 App 与独立诊断协议。
- `validation/vulkan/`：真实 Vulkan ICD 的验证工具，不是通用层或生产后端。编译成功不证明 GPU 执行或 Metal 接入成功。
- `.github/workflows/reference-macos.yml`：仅手动执行的平台参考构建。用法见 [独立组件](docs/reference-components.md)。

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

硬件支持仍需各厂商对应 kext、共享平台接入及逐组合实机测试。**当前没有可用驱动，也没有发布 `0.1.0`。**
