# kext 与 App：职责与首轮研究

## 范围与状态

本轮源码复核涵盖 `backends/nvidia/kext/Driver.cpp`、`Probe.cpp`、
`kexts/Platform/src/platform/device.cpp`、`client.cpp` 与 `app/`。
这是静态研究，不是目标系统加载、断电／热拔插或 GPU 执行证据。

## 当前调用链

```text
App main.mm（GUI／CLI 入口）
  → Diagnostics.mm（IOKit 枚举、连接、请求与报告）
  → ipc/diagnostics（固定大小字节协议）
  → MetalVKDiagnosticClient（权限与参数检查）
  → MetalVKPlatformDevice（受锁保护的缓存 PCI 快照）
```

NVIDIA `Driver.cpp` 保留精确 PCI 匹配，并可在显式 Boolean 开关启用时调用
`Probe.cpp` 做只读 BAR0 身份检查。probe 不启用 bus master、不上传固件，
释放映射并关闭 provider 后才进入公共服务启动；它不产生完整 NVK discovery。
`session.c` 虽纳入内核 archive，尚未挂接此 UserClient；App 也没有提交／渲染接口。

## 模块维护标准

- 中立核心不依赖 NVIDIA、Vulkan、AppKit 或内核类型；不迁移厂商代码到通用库。
- `transport.h/.c` 负责信封、目录与字节校验；`discovery.h/.c` 负责设备模型和能力投影；
  `session.h/.c` 负责会话状态与分发。头／源镜像，由 `nvidia/init.h` 聚合。
- 公共内核 device 管理服务生命周期，client 管理请求权限与参数；
  厂商 Driver／Probe 仅处理匹配及厂商硬件。未来执行 client 不能挤进只读诊断 client。
- App 保持诊断入口与 IOKit 访问分离。现有界面体量无需为文件数量而强拆；
  后续增加安装、设备管理、渲染验收时必须各自成模块，不往 `main.mm` 堆功能。
- 平台管理器协调已有内存、工作、生命周期模块；RM、DMA、VA、提交、同步、显示分别管理。
  未实现的管理器不建立假成功入口。

## 已检查的资源与权限

- App 用不可复制 RAII 对象释放 iterator／service／connection，最多枚举 64 个服务；
  IOKit 请求放后台队列，UI 更新回主线程，导出使用用户选择的 URL 与原子写入。
- client 要求 local-user 权限与指定 connect type；固定结构大小，拒绝 descriptor／异步请求，
  检查关闭状态。device 被 retain，最终由 client free 释放。
- 公共 device 在发布服务前完成快照与属性设置，锁保护 ready／snapshot；
  stop 标记不可用。快照只证明过去的 PCI 状态，不构成持续 D0 或 DMA 安全保证。
- probe 的 open／映射由 RAII 清理；映射长度、对齐、只读／缓存选项及前后配置均检查。
  连续快照与 isInactive 检查无法排除检查后断电或热拔插，不可据此放行写入／DMA。

## 后续研究顺序与未解决事项

生命周期、PM、provider open／close 与 RM／DMA 的公开源码研究见
[Darwin GPU kext 研究](kext-lifecycle-research.md)，包含固定来源与待实现的接线标准。

1. 核实目标 macOS build、加载授权与内核依赖；CI ad-hoc 签名不等于真机可加载。
2. 固化 provider ownership、PM／终止协调、停止接受请求与排空的顺序；
   未核实电源与设备移除协调前，不增加寄存器写入。
3. 先完成 discovery 显式编码与语义校验，再设计独立 NVIDIA client 接线；
   只向用户态返回真实可查询字段，未知引擎／拓扑／VA／内存值保持未知。
4. 执行接口明确每个连接的句柄归属、权限、配额、关闭／退出回收及复位失效。
   禁止裸内核指针；不得靠 mutable mask 启用不存在的 handler。
5. App 通过诊断、discovery、执行验收三种不同结果报告能力；
   无设备与请求失败保留不同退出码，不能把连接成功标成 Metal 加速。

风险范围主要是本地 IPC 输入、能力伪造、资源耗尽及设备生命周期竞态；
当前无网络、shader 上传或 DMA 接口。动态加载／卸载、睡眠／热拔插、App 连接故障注入，
以及 macOS 15／26 原生构建仍需在新修改上独立核验；不宣称仓库全量安全审计完成。
