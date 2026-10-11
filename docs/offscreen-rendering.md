# 首个可调用的 Vulkan 离屏渲染管线

## 已打通的范围

`libmetalvk-vulkan.a` 现在同时编入 `src/vk/context.c` 与 `src/vk/offscreen.c`。原先只有验收程序持有的 image／framebuffer／命令缓冲／fence／回读生命周期已进入可复用库；`validation/vulkan/render.c` 通过同一公共 API 运行真实图形任务，不再复制该资源管理逻辑。

实际路径：

```text
硬件 Vulkan context
  → offscreen image + memory + view + clear/store render pass + framebuffer
  → 应用创建兼容的原生 Vulkan graphics pipeline
  → begin → 应用 vkCmdBindPipeline / vkCmdDraw
  → end pass → image-to-buffer copy → transfer-to-host barrier
  → queue submit → fence wait → non-coherent invalidate（如需要）
  → RGBA8 全图回读 → 应用验证 → 复用或关闭
```

没有 CPU 生成渲染结果，没有软件 GPU／MoltenVK fallback，不新增厂商命令 ABI。调用者使用 Vulkan 自身的 pipeline、shader module 和命令录制接口。库不理解验收程序的四象限图案，也不要求调用者采用该图案。

**这是一条以可用硬件 ICD 为前提的用户态离屏管线，不是 NVIDIA macOS 驱动、Metal 翻译器或可见显示管线。** 本机 Windows 官方 NVIDIA ICD 已实测；目前仍没有接通自研 macOS NVIDIA RM／GSP／DMA／ICD，也没有对外声称 R2／R3 通过。

## 接口与资源标准

公共头为 `include/vk/offscreen.h`，由 `vk/init.h` 聚合；不进入 SDK-free 的 `metalvk.h`／`libmetalvk.a`，也不进入 kext。context 必须拥有 graphics queue，所有 context、target、queue 和应用自有 pipeline 访问由 owner 线程外部串行化。

1. `mtvk_vk_offscreen_open`：创建固定尺寸的 RGBA8、单样本、单颜色附件目标。最大 4096×4096，每项 device allocation 最多 128 MiB，最多 8 个活跃 target／context。原始资源上界为每 context 两类 allocation 合计最多 2 GiB；ICD 自己的内部开销另计。不是无限分配器，OOM 直接返回 Vulkan 错误。
2. `mtvk_vk_offscreen_render_pass`：取得兼容性所需的原生 render pass。应用自行创建 pipeline／shader modules，布局、顶点输入、descriptor 及 shader 合法性遵循 Vulkan；库不编译 MSL、GLSL、AIR 或 SPIR-V。
3. `mtvk_vk_offscreen_begin`：reset 已完成的命令池，开始 clear/store render pass，返回正在录制的 VkCommandBuffer。clear float 必须有限。应用可录制 graphics commands；**不得**自行 end pass、end/reset command buffer、submit，或加入嵌套 render pass。
4. `mtvk_vk_offscreen_submit`：结束 pass，记录完整 copy 与 barrier，end command buffer、reset fence 并提交真实 queue。
5. `mtvk_vk_offscreen_wait`：0–30 秒 fence wait；0 为 poll。timeout 保留在途资源，不能当作完成。30 秒是传给 Vulkan 的上限，不是对故障 ICD／OS 阻塞行为的绝对保证。
6. `mtvk_vk_offscreen_copy`：只允许已确认完成的结果；输出宽×高×4 字节，按 Vulkan image 行顺序紧密排列。whole allocation map／invalidate 保证 non-coherent atom 对齐要求。失败不修改调用者字节，超出的 capacity 尾部也不写。
7. `mtvk_vk_offscreen_close`：不隐式等待；Pending／Uncertain 返回 NOT_READY 并保持所有权，Ready／Recording／Complete／Failed／Lost 可释放。调用者随后销毁应用自有 pipeline 等 child，再 close context。

target 存活期间，`mtvk_vk_close(context)` 返回 NOT_READY，不能先释放父设备。它只追踪本模块 target；应用自行创建的其他 Vulkan 对象仍须遵守原有“先销毁 child”标准。初始化失败回滚已成功创建的对象和父引用；失败调用的未定义 handle 输出不会发布为拥有的资源。

状态为 Ready → Recording → Pending → Complete，另有 Failed／Uncertain／Lost。未提交的 recording 可以安全销毁。重复 begin／submit、提前 copy、在途 close、非法尺寸／容量／等待参数都明确拒绝。Device loss 允许资源 teardown，但绝不发布成功像素。Complete 可以 begin 另一帧，复用目标和同一兼容 pipeline。

提交失败并不假设“GPU 什么也没做”：除明确 device loss 外进入 Uncertain。`mtvk_vk_offscreen_drain` 是**显式的 device-wide 错误恢复**，调用 `vkDeviceWaitIdle`，没有 deadline 保证；失败保留全部资源。成功 drain 可以确认原成功提交的结果完成；不确定的失败提交只转为可清理 Failed，不能 copy。验收客户端保留原始失败状态，不把恢复排空变成渲染 PASS。

## 完整构建与真实运行

需要 Vulkan SDK／Loader、clang、llvm-ar/ar、glslangValidator、spirv-val；原生 kernel build 仍由独立 macOS workflow 负责，Vulkan 不链接进内核。

Windows 已实测的完整命令：

```text
python tools/build_vulkan.py --sdk C:/VulkanSDK/1.4.363.0 --run --run-render --vendor 0x10de
```

构建脚本真实编译／验证三种 shader，原子替换完整 archive，构建 compute／render／offscreen test／fault test，再按明确开关执行硬件任务。无 `--run`／`--run-render` 时只构建和运行不需要 GPU 的输入测试，不伪装硬件验收。

主要产物位于 `target/vulkan/`：`libmetalvk-vulkan.a`、`vulkan-render[.exe]`、`vulkan-compute[.exe]`、`test-vulkan-offscreen[.exe]`、`test-vulkan-render-faults[.exe]` 与 `shaders/`。静态库和客户端仍需要目标平台的 Vulkan Loader／硬件 ICD；不是可独立运行的自研 NVIDIA 用户态驱动。CI 产物提供库／公共头／shader／验收客户端，但上传成功不代表执行成功。

## 验收与故障测试

2026-10-11，Windows RTX 4060 Laptop `10DE:28E0`、NVIDIA `616.92`、Vulkan `1.4.351`：

- 三帧真实 native graphics pipeline draw，每帧 4096 像素全部匹配；验证层 error／warning 均为 0。
- 真实 GPU 的 7×5 非方形 render-pass clear／回读：全像素、输出 guard 与 capacity 尾部通过。
- 多 target 上限、父 close 拒绝、重复调用／状态顺序、未提交 recording 销毁、释放后重新分配全部通过。
- 原有真实 compute 多尺寸／未写尾部回归仍通过。
- image OOM（包含失败输出 poison handle）、第二次 memory allocation OOM、wait timeout、drain failure、submit failure、map failure、invalidate failure 七类**测试专用注入**通过，零验证消息。invalidate failure 使用测试 memory-property override 强制经过该分支；不声称该设备实际使用了 non-coherent memory。

注入仅在独立测试对象编译中出现，生产 archive 没有 failure switch 或成功模拟。drain failure 测试刻意保留仍可能在途的 target／pipeline／context 至测试进程退出，不强制回收。它不是 kext 卸载安全证明。真正 OOM／GPU hang／device loss／跨进程退出仍需隔离硬件故障验收。

Linux、macOS 15／26 CI 编译／链接库与验收客户端，检查 C++ 头、静态分析和不需要 GPU 的 ASan／UBSan 输入回归。CI 的 macOS 宿主没有被当成 NVIDIA 硬件执行环境，也没有借 Apple 虚拟 Metal 或 MoltenVK 代替该路径。

## 安全审查范围

信任范围为应用参数／原生 Vulkan commands → ICD → 在途 GPU 资源 → CPU 回读。长度、尺寸、allocation 大小、target 数和 wait 值受限；输出 alias 拒绝保护 target／context 元数据，创建 handle 只在成功后进入拥有集合，所有部分失败回滚。无网络、UserClient、任意 MMIO／DMA 地址、privileged installation 或 shader upload 入口。

应用必须传入有效、仍存活的 context／target 和可写 output 存储，并遵守线程／句柄所有权；整数 span 检查不验证页权限，也不是对任意恶意进程内指针的沙箱。原生 commands 和 shader 必须满足 Vulkan spec；库不接收未验证的外部 shader 字节，验收构建对仓库 shader 执行完整 spirv-val。恶意 ICD、GPU firmware、应用自行破坏的 Vulkan 状态不在本模块可以隔离的信任范围内。

下一条完整 **macOS NVIDIA** 渲染管线仍必须接通可信固件、RM／GSP、设备 DMA／GPU VA／提交与 fence、Darwin ICD 和图形 compiler。其路线与真机访问条件尚未明确；本模块提供相同的真实 draw／readback 验收入口，不消除这些阻塞，也不替代 Metal 系统接入。
