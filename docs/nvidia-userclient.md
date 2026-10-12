# NVIDIA 只读 discovery UserClient

范围：现有 NVIDIA `10de:28e0` kext 的只读 IPC 接线，不增加硬件写入或执行能力。
公共诊断 connect type 0 保留；NVIDIA 专用 connect type **1**、selector **0**。

## 数据链与使用

`capturePci` 在服务启动时读 PCI 配置并缓存快照 → 共享平台锁内 copySnapshot →
endpoint.c 验证设备策略并投影 → 独立请求本地 session → 版本 2 wire 编码 →
IOKit 同步结构请求 → 用户态校验／解码。

可导出的已观察字段仅 device ID、bus/device/function 与 PCI revision。
目标策略为离散 NVIDIA GPU；PCI domain 来源缺失，零不表示已经确认 domain 0。
SM、chipset、名称、拓扑、VRAM、BAR aperture、engine class、VA、RM 能力均未知／零。
可选 BAR0 boot probe 仍由既有 opt-in 控制，其身份属性留在 registry；未把它推断成 RM discovery。

工具在 `MetalVK.app/Contents/MacOS/metalvk-nvidia-discovery`，单独 ad-hoc 签名后纳入 App：

```sh
MetalVK.app/Contents/MacOS/metalvk-nvidia-discovery --list-services
MetalVK.app/Contents/MacOS/metalvk-nvidia-discovery --registry-id 0xYOUR_REGISTRY_ID
```

列表只列现有诊断服务，不加载 kext；查询必须显式选 registry ID，并核对真实 service class。
退出码：0=查询或列表成功，1=IOKit／解码／资源清理失败，2=无匹配服务，64=参数错误。
`PASS NVIDIA discovery IPC` 仅在真实 IOServiceOpen／CallStructMethod 返回且 codec 校验通过后打印。
没有模拟结果、默认 GPU 或 CPU 回退。成功是 **缓存 PCI 诊断 IPC** 证据，不是实时设备可执行保证。

## IPC 和生命周期标准

- 固定 input 24 字节、output 352 字节，不接受 scalar、async 或 memory descriptor 请求。
  IOKit 验证结构大小，callback 再验证；共享 transport/session/codec 再验证字段。
- 调用只允许本地用户权限，复用公共只读 default locking／单线程 external-method 标准。
  不提供 memory mapping、授权改变、固件上传、DMA、channel 或 MMIO 写入。
- client 在 start 后 retain service，free 时 release；close 原子置位并 terminate。
  callback 再检查 closed/inactive。每次请求创建私有 session，没有跨请求可变 GPU 资源。
- base lock 内检查 ready 与复制快照，锁外编码／分发。stop 将 ready 置 false；已取得快照的
  在途查询允许完成，只访问自有副本，不触碰 provider。服务 retain 并非 PCI ownership／D0 保证。
- 只有写出合法响应才返回 IOKit success；私有错误状态保存在 response envelope。
  不能将 transport 的错误响应当成 DEVICE_QUERY 成功；用户客户端必须同时检查两层状态。
- 工具在请求结束后关闭 connection，所有分支释放 service/iterator，清理错误导致非零退出。
  CI runner 不加载 kext：只能覆盖无服务与错误参数，不能替代真实 IPC 成功路径。

## 安全复核和残余风险

### 可复用 Darwin transport

`backends/nvidia/darwin/Discovery.cpp` 提供 `mtvk_nv_darwin_query`；原生构建产出
`libmetalvk-nvidia-darwin.a`，CLI 链接此库，不再复制 open/call/close 查询实现。
`endpoint.c` 的共享 response decoder 统一检查信封、状态、操作 mask、设备策略与能力裁剪，
CLI、native library 和 Mesa adapter 都使用它；失败不修改调用方输出。

返回值包含 status、stage、native_error 与 response_bytes。仅当 close、service release
和完整响应验证均成功后才复制响应给调用方；close/release 失败返回 CLEANUP_REQUIRED。
清理错误优先于先前查询错误，不能继续消费缓存的“成功”字节。调用者必须停止使用此次结果。
不返回 service/connection handle，不持有全局共享连接，不自动选择 GPU 或降级到 loopback。

Apple IOKitUser `323ead896d04424f87184d8f6ff0cce811aab106` 的
[IOKitLib.c](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.c)
显示 matching API 消耗 matching dictionary；IOServiceClose 调用 close 后无条件释放 connection
的 Mach send right。因此不在 close 失败后再次释放同一 connection，避免二次释放／名称复用风险。
框架内那次 release 的返回值未公开给调用方，不能凭 IOServiceClose success 声称全部内部清理已取证。

Mesa adapter 增加显式 registry-ID native ingress `mtvk_nvkmd_open_registry`，调用同一 Darwin
库并映射错误。它仍是独立 adapter，不是已挂接 NVK ICD 的 physical-device enumerator；
未知 RM 字段不够创建 GPU execution device，`create_dev` 继续拒绝。

主机回归编译真正的 Discovery.cpp，但用 `tests/fixtures/iokit` 隔离 IOKit fault provider，
覆盖 lookup allocation/missing、ID/class 不符、open/call/close/release 失败、超长／损坏响应
及成功提交；每个失败要求输出不变、预期清理次数一致。这是 fault-injection，不是真实 IPC。
该 mock include 路径只用于 test object，不进入原生库、App、kext 或 ICD。
Linux Mesa conformance executable 通过显式 test-only provider 编译同一 native ingress body，
验证成功映射、无设备、OOM、cleanup failure 和畸形响应，不能当作 macOS NVK 运行证据。
原生构建则使用 Apple SDK 和真实 IOKit framework，并运行无服务／参数拒绝路径。

资产是 kernel memory、缓存 PCI 身份完整性和 service/client 生命周期。入口是本地进程
IOKit 结构请求／连接类型与 CLI registry ID；内核不执行用户提供的指针、路径或命令。
长度有固定上限，局部栈数据，无请求堆分配；没有网络、文件解包或外部代码执行。
用户 ID 最多 16 个十六进制数字，服务列表最多 32 项；codec/投影失败不发布字段结果。

回归包括错误 vendor/device/class/BDF/phase/capability/registry ID、失败输出不变、
快照→session→wire→decode 的主机链及原有 mutation／alias 回归。
原生 CI 编译并分析 Driver/Client/共享 device/client，核对新类和投影符号链接，运行真实 CLI
无服务路径及参数拒绝。这些是源码与构建证据，不是加载／进程退出／热拔插实测。

尚缺：真实目标 macOS build、签名与 KC 批准、现有 PCI owner、加载恢复方案及隔离真机授权。
实机应分别验证 type 0 和 type 1、权限拒绝、并发查询／client close、服务停止、错误版本／
selector／大小以及真实 registry ID 的缓存字段一致性。本轮不主动加载、修改启动安全或探测第三方系统。
RM/GSP、ownership/PM、DMA/IOMMU、GPU VA、提交／同步与 Metal 翻译仍未实现。
