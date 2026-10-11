# metalvk

**厂商中立的 GPU 通用层与渲染管道。** 目标是让 Metal 语义跑在 Vulkan 之上：任何具备 Vulkan 驱动的设备，接上就能被系统 Metal 看见。

方向已定：**中立接口用现成的 Vulkan 规范**（缝），不自造一套 GPU ABI。早期版本里的 `mtvk_gpu_*` 后端注册／转发 ABI 因此已移除。

## 当前状态

默认产物 `libmetalvk.a` 只编译基础层，只使用标准 C 头文件；不含厂商 SDK、硬件驱动、内核代码、私有 ioctl、Vulkan 实现或着色器编译器。诊断协议是独立产物 `libmetalvk-diagnostics.a`。

```text
include/                     src/                      （与 src 严格镜像）
  platform.h                   base/status.c
  metalvk.h       顶层聚合头   ipc/diagnostics.c
  base/  init.h status.h span.h
  ipc/   init.h diagnostics.h
```

## 语言

核心基础层用 **C11**（标准 C 头文件，无平台依赖）。kext / Metal 接入等需要 IOKit 或 Objective-C 运行时的部分，按 AGENTS.md 允许使用 **C++17 / Objective-C++**，不与核心混编在同一编译单元内。

每个子目录一个 `init.h` 聚合该模块头文件；`metalvk.h` 聚合全部模块。这是 AGENTS.md 约定的结构，后续模块按同一方式扩展。

## 编译与验收

需要 C11 编译器、ar 和 make；**不需要厂商 SDK、Vulkan SDK 或内核 SDK**。

```sh
make CONFIG=Release
make test CONFIG=Debug
make test CONFIG=Release
make test CONFIG=Alpha
python3 tools/check_core_boundary.py --archive target/Release/libmetalvk.a
```

Windows 使用 `mingw32-make` 与 `AR=llvm-ar`。

`tools/check_core_boundary.py` 校验默认库的源码集合、头文件依赖、归档成员与"无厂商/无平台条件编译"约束。默认 CI `.github/workflows/core.yml` 在 Linux/macOS 上跑这些检查，并做 freestanding 语法与 C++ 公共头检查。

## 独立组件（不进入默认库）

- `backends/`：厂商 kext 原型目录，尚未接入。
- `app/`、`include/ipc`、`src/ipc`：只读诊断 App 与独立诊断协议。
- `validation/vulkan/`：真实 Vulkan ICD 的验证工具，不是通用层或生产后端。
- `.github/workflows/reference-macos.yml`：仅手动执行的平台参考构建。用法见 [独立组件](docs/reference-components.md)。

## 规划中的模块（尚未实现）

按 AGENTS.md 的模块化约定预留，实现时逐目录落地，不创建空驱动：

```text
include|src/  platform/  版本探测与私有 ABI 表查询
              display/   IOFramebuffer 接入、EDID/DisplayID、模式池、HDR 元数据
              accel/     IOGraphicsAccelerator2 接入
              agdc/      AppleGraphicsDeviceControl 接入
              metal/     Metal 语义层（Metal → Vulkan）
              vk/        Vulkan 绑定
              backend/   厂商后端注册
data/abi/     kext ABI 表（按 OS build 键，与逻辑分离）
kexts/Platform/   内核平台服务
kexts/Display/    framebuffer / accelerator / agdc
```

"所有现有 GPU 已实机可用"尚未验收：仍需各厂商对应 kext、共享平台接入和逐组合实机测试。当前没有可用驱动，也没有发布 `0.1.0`。

## English

`metalvk` is a vendor-neutral GPU layer that runs Metal semantics on top of Vulkan, so any device with a Vulkan driver can appear to system Metal. The neutral interface is the Vulkan specification itself; the earlier home-grown `mtvk_gpu_*` backend ABI has been removed. The default library contains only the base layer and uses standard C headers only. Hardware support requires per-vendor backends and target-platform validation.
