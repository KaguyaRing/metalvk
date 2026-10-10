# metalvk

**厂商中立的 GPU 通用层**，提供版本化公共接口、后端注册、设备与能力枚举、调度转发及生命周期管理。

本次默认产物是 `libmetalvk.a`。它只编译 `src/base/status.c` 与 `src/gpu/runtime.c`，只使用标准 C11 头文件；不包含任何厂商 SDK、硬件驱动、GPU 寄存器操作、私有 ioctl、Vulkan 实现或着色器编译器。

## 职责与接入

```text
上层 API / Metal→Vulkan 前端（独立模块）
                    │ 通用接口
                    ▼
             metalvk 通用层
      版本检查 / 注册 / 能力 / 生命周期 / 转发
          │             │             │
       后端 A         后端 B         后端 C
       独立 kext      独立 kext      独立 kext
          │             │             │
             对应 GPU 的具体操作
```

后端通过 `mtvk_gpu_backend_descriptor` 注册，通用层复制描述表，并只按统一能力与操作合同转发。新增或替换兼容后端无需修改通用层。库不按厂商名称、设备 ID 或任何厂商特例选择执行路径。

核心不会创建实际 GPU 命令或资源；它转发后端所属的队列、命令与完成 token，由后端验证 token 并执行 GPU 操作。**这些 token 不是 Vulkan API**。Metal→Vulkan 的标准语义翻译属于后续独立前端，本层不冒充 Vulkan 实现。

## 公共接口

公共入口：`include/metalvk.h`，版本化合同：`include/gpu/runtime.h`。

- `mtvk_gpu_runtime_create` / `destroy`：由嵌入平台提供分配器与互斥锁，核心不绑定操作系统 SDK。
- `mtvk_gpu_backend_register` / `unregister`：版本协商、复制注册表、安全替换；仍有设备或回调时返回 `busy`。
- `mtvk_gpu_device_enumerate` / `open` / `describe` / `close`：能力快照与设备所有权管理。
- `mtvk_gpu_dispatch`：统一 `submit` / `wait` / `cancel` 转发，按能力拒绝不支持操作。

ABI major 不兼容即拒绝；minor 只允许追加字段并声明最低宿主版本。传给回调、返回调用者的结构始终标明实际持有的已知字段长度。metadata 是快照，不是硬件在线保证。

不同设备可并发转发，同一设备的重叠调用返回 `busy`。后端回调不在核心互斥锁内调用；回调／设备引用存活期间不能注销后端。token 不重复使用，旧 token 与其他 runtime 的 token 不会命中新对象。

## 编译与验收

需要 C11 编译器、ar 和 make；**不需要厂商 SDK、Vulkan SDK 或内核 SDK**。

```sh
make CONFIG=Release
make test CONFIG=Debug
make test CONFIG=Release
make test CONFIG=Alpha
make test-plugin CONFIG=Release
python3 tools/check_core_boundary.py --archive target/Release/libmetalvk.a
```

Windows 使用 `mingw32-make` 和 `AR=llvm-ar`。

测试覆盖版本不兼容、可兼容的新 minor 前缀、能力检查、多个后端、错误传播、OOM、关闭失败回滚、并发调用与卸载竞态、坏后端清理、过期／跨 runtime token，以及独立编译的两个二进制插件在不重建核心时的加载、替换与卸载。插件只是接口测试夹具，不冒充 GPU 驱动。

默认 CI `.github/workflows/core.yml` 仅构建通用层：Linux/macOS、内存与线程检查、C++ 公共头、freestanding 编译检查、源依赖边界与静态库成员检查。上传产物只包含通用库及公共头。

## 独立组件

- `backends/`：独立厂商 kext 原型，不进入通用库默认构建。
- `app/`、`include/ipc`、`src/ipc`：旧诊断 App／协议，协议有独立 `libmetalvk-diagnostics.a`，不进入通用库。
- `validation/vulkan/`：实际 Vulkan ICD 的验证工具，不是通用层或生产后端。
- G0 的旧全局 API 留在 `include/gpu/backend.h`、`src/gpu/backend.c`；只通过 `make test-legacy` 保留历史测试，不导出到默认稳定公共头／库。

平台参考构建 `.github/workflows/reference-macos.yml` 仅手动执行。用法见 [独立组件](docs/reference-components.md)。

## kext 边界与实际支持

在内核中，共享的平台 framework/kext 应持有同一个 runtime 并导出通用 C ABI；各厂商 kext 在加载时注册自己的后端，在注销成功后才能卸载。不能让每个厂商 kext 各自建立孤立注册表，也不能把用户态静态库地址直接传到内核。用户态／内核之间的访问需独立平台适配与版本化 IPC。

接口对所有厂商一致，但“所有现有 GPU 已实机可用”**尚未验收**：仍需各厂商对应 kext、共享平台接入和逐组合实机测试。当前完成的是通用层及其接入合同，没有完整厂商执行后端，也没有发布可用驱动 `0.1.0`。

## English

`metalvk` is an SDK-free, vendor-neutral C11 GPU orchestration layer. Its versioned ABI manages backend registration, capability snapshots, device lifetimes and dispatch forwarding. Hardware operations belong to independently registered backends. The default library contains no vendor, Vulkan, kernel or application implementation. Actual hardware support requires corresponding backends and target-platform validation.
