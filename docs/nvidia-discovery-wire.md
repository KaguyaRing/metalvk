# NVIDIA discovery 字节标准

范围：`MTVK_NV_OP_DEVICE_QUERY` 的共享载荷 codec、session 分发与 Mesa 主机 adapter。
不是新跨厂商 ABI，也不是 NVIDIA 硬件 query／RM 资源语义的验收。

## 版本与布局

外层 transport 版本为 **2**，信封仍为 24 字节；拒绝旧版本 1，不能解析旧 C struct 载荷。
内层 discovery schema 版本为 **1**，大小固定 **328 字节**。两端需同时更新；历史
`v0.1.0` 的二进制不改写、不与新 codec 混用。

全部整数小端；窄标量也占 4 字节，并在收窄前检查范围。64 位值占 8 字节。
不把 host struct、padding、指针或 kernel handle 写进载荷。

| 字节偏移 | 内容 |
| --- | --- |
| 0 / 4 | schema version / payload size（u32） |
| 8..92 | 以下 22 项，各 u32，按下列顺序 |
| 96 / 104 | vram_size_B / bar_size_B（u64） |
| 112 | max_smem_per_wg_kB（u32） |
| 116..152 | sm_smem_sizes_kB，10 项，各 u32 |
| 156 | sm_smem_size_count（u32） |
| 160..204 | zcull_info，12 项，各 u32 |
| 208..271 / 272..287 | device_name / chipset_name（固定 64 / 16 字节） |
| 288 / 292 / 296 / 300 / 304 | has_alloc_tiled / has_map_fixed / has_overmap / has_compression / has_dma_buf（u32） |
| 308 | bind_align_B（u32） |
| 312 / 320 | va_start / va_end（u64，结束地址不包含） |

22 项顺序：device_type、device_id、chipset、pci_domain、pci_bus、pci_device、pci_function、
pci_revision、sm、gpc_count、tpc_count、mp_per_tpc、max_warps_per_mp、has_transfer_queue、
has_zcull_info、nc_atom_size_B、cls_copy、cls_eng2d、cls_eng3d、cls_m2mf、cls_compute、cls_gpfifo。

代码的单一字段序列供 encode／decode 使用，静态断言核对大小；独立黄金字节测试不调用 codec
生成期望值，避免两个同错的实现互相证明。

## 字段与资源标准

- 0 保留为未知硬件值；device_type 按已有枚举 0=IGP、1=discrete、2=SOC，不能单独用
  device_type=0 证明发现了 IGP。全零记录允许编码，但不构成有效执行设备。
- device ID／chipset／domain／engine class／shared memory 值必须可表示为 u16；
  bus／revision／SM／GPC／MP／warp 必须可表示为 u8；PCI device≤31、function≤7。
- 布尔值仅 0／1，shared-memory count≤10，未使用的数组项必须零；无 zcull 信息时数组必须零。
- bind alignment／noncoherent atom size 为零（未知）或 2 的幂；VA 两端同时零表示未知，
  否则 start<end。已知 bind alignment 时两端必须对齐。这里只验证形状，不宣称 RM 支持此范围。
- 名称仅可打印 ASCII、必须有 NUL。encoder 将 NUL 后字节规范为零，decoder 拒绝非零尾部，
  避免泄漏主机尾部数据及多种等价 wire 表示。未知名称为空字符串，不编造名称。
- session 先能力裁剪，再验证／编码，再写信封和载荷；设备字段非法时仅返回错误信封。
  Mesa 解码成功后再裁剪，拒绝不带 DEVICE_QUERY 支持或广告未实现操作的响应，不发布 pdev。
- request.response_bytes 是载荷预算（不含 24 字节信封），0=未指定；非零小于 328 时返回
  BUFFER_TOO_SMALL 错误信封。实际输出缓冲区容量始终独立检查，错误信封仍需 24 字节。

## 生命周期与安全复核

资产是 host/kernel 内存、discovery 字段完整性与 Mesa pdev 生命周期。不可信入口是 wire 字节、
缓冲区长度和 transport 声明；敏感写入为 host decode 输出与 session reply。

- 固定上限，无分配、递归、外部 I/O 或硬件访问；byte load/store 支持未对齐输入，
  不通过不兼容的类型指针访问 wire。显式 little-endian 不依赖宿主字节序。
- encoder 先检查参数／字段再写；decoder 只在所有检查通过时提交本地记录。
  失败不修改 codec 输出。短载荷拒绝，长载荷拒绝，不忽略后缀。
- codec 拒绝输入／输出重叠；session 拒绝 reply 与 request／session 重叠，及 written 与上述
  缓冲区重叠。重叠错误不写 written；普通分发失败将 written 清零或返回已写错误信封长度。
- 指针可表示性检查不是访问权验证；调用者必须提供有效可访问内存。未来 UserClient 需使用
  IOKit 受控拷贝和权限校验，不能把用户指针直接传给共享函数。现有专用端点使用固定大小
  synchronous struct method，并拒绝 descriptor／async，详见[接线标准](nvidia-userclient.md)。
- session 不自带锁，必须由拥有者串行访问；禁止在 codec 执行中并发修改输入，
  OS 同步由后续端点管理。初始化先快照，允许以自身 device 重新初始化。

回归覆盖固定黄金字节、所有字段、未对齐输入、host padding、字符串规范化、逐长度截断、
高位收窄／错误布尔／非法 PCI／数组／VA／对齐、输出不变、预算和缓冲区 alias；
20,000 次有界载荷变异要求每个成功解码可原样再编码，每个失败保留输出。

残余风险：UserClient→真实 IOKit 工具已有源码接线，但尚无加载或真实 probe→UserClient→Mesa 往返证据；未在 big-endian 硬件运行，
字节序正确性依据显式 byte 操作和独立黄金字节。名称 ASCII 是此私有 schema 的限制。
engine class、拓扑、VA 与 RM 的对应仍需真实设备来源逐项核验；本轮不启用 GPU 执行能力。
