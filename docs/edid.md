# EDID／CTA／DisplayID：真实字节解析与能力交集

本模块属于 `metalvk` 通用层，不需要厂商 SDK／Vulkan／IOKit，也不调用硬件。厂商后端取得不可变的真实 DDC／AUX／系统 EDID 字节快照后调用这里的解析器；不得把解析成功当作链路训练或 HDR 输出成功。

## 已实现

- `include/display/edid.h`：`mtvk_edid_parse`、`mtvk_edid_select_capabilities`；由 `metalvk.h` 聚合。
- `src/display/edid.c`：EDID 1.3／1.4，完整基础块与声明的扩展块长度／checksum，厂商、型号、年份、数字接口、声明位深、物理尺寸、名称和详细时序。名称移除控制字符并保证 NUL 终止；CLI 不输出序列号。
- `cta.c`：CTA revision 3 的有界 data block collection、详细时序、RGB BT.2020、HDR EOTF／descriptor、HDMI VSDB 的 RGB deep-color 声明。已提供 VIC 4、16、19、31–34、63–64、93–97 的名义时序表。
- `displayid.c`：EDID 封装的 DisplayID 1.2／1.3／2.0、独立内部 checksum、块边界及 section 计数／版本一致性；Type I／VII 20 字节 progressive 时序。正确区分 Type I 的 10 kHz 与 Type VII 的 1 kHz、N−1 编码和同步极性；可解析内嵌 CTA collection。
- `edid-capabilities.c`：只取屏幕已解析完整时序与后端已核验 RGB 能力的交集，保留后端 mode ID，不合成时序、不猜测位深或 HDR。结果可传给已有 `mtvk_display_connect`，不是自动硬件接线。
- `tools/edid-inspect.c`：只读、有输入上限的原始二进制 EDID 检查工具。

相同解析器进入 SDK-free 库与共享内核平台库；生产路径没有模拟 EDID、伪造 checksum 或“读取不到就返回默认屏幕”的实现。

## 严格输入与事务性输出

输入最长 4096 字节／32 个 128 字节块，必须与 base extension count 完全一致。拒绝不足、多余、非块整倍数、错误 header、任一外层 checksum、DisplayID 内部 checksum、越界 DTD／data block、冲突 HDR 声明。结果超出 32 个完整时序返回 LIMIT_EXCEEDED，不能静默截断并冒充完整集合。

逐字节读数值，不 cast packed 指针、不依赖宿主端序或对齐。失败不修改输出；拒绝输入／输出重叠。解析器无动态分配、无全局可变状态，独立快照可并行解析；调用者保证源字节在调用期间不被修改。checksum 不是认证，不能证明 EDID 来源可靠；后端必须独立复核当前 sink、带宽和硬件支持。

`mtvk_edid_info` 的 `omitted_timings`／`unhandled_blocks` 显式报告未处理的描述符／块；**OK 不等于所有 EDID 功能都已解释**。不知道的时序不会替换成相同分辨率的猜测模式。`preferred_mode_id` 为第一个已识别 preferred／native 时序，不表示完整的偏好排序。

## HDR、位深与模式的边界

CTA HDR luminance 字节保留编码值及每个字段的 presence，不误标成 cd/m²，也不直接当作内容 mastering metadata。EDID 描述的是接收端能力，之前显示配置 API 的 mastering primaries、CLL／FALL 描述的是输出内容元数据，两者不能互相复制。

PQ／HLG 只有在屏幕明确声明相应 EOTF、RGB BT.2020、可用至少 10 bpc，且后端支持时，才进入能力交集。static metadata 还要求 type 1 descriptor 和后端支持。位深不明时返回 NOT_SUPPORTED，不默认为 8／10 bit。EDID 1.4 声明位深保留确切值；HDMI VSDB 独立增加明确声明的深色选项，不能推导出未声明的中间位深。

`mtvk_display_capabilities.depth_mask` 非零时在 min/max 之外约束精确位深，避免把 8／10／12 bit 误扩大成 9／11 bit；零保留原有 min/max 策略以兼容源码调用者。它是内部源代码 API，所有使用者必须按同一头文件重新编译，不保证旧二进制结构体布局兼容。

交集要求时钟、active／total、sync endpoints 和 polarity 全部相符，而非只比较 width／height／Hz。后端仍须验证每个 mode 的深色组合、压缩、链路类型与当前显示器身份；HDMI VSDB 的能力不能无条件推广到 DP。

## 尚未覆盖的格式

- Established／standard timing 的 DMT／GTF／CVT 展开，完整 VIC 表及 VIC fractional-rate 派生。
- 交错、stereo、border／composite sync、YCbCr／420、VRR／DSC／FRL／动态 HDR 能力解释。
- DisplayID 的公式／短时序、非 20 字节 Type VII、高版本的新结构、原生独立 DisplayID transport，以及完整 mandatory-block 语义合规检查。
- EDID 1.0–1.2，CTA revision 1／2，损坏屏幕的自动 quirk／checksum 忽略；未知扩展只验证 checksum 并报告未处理。

这些是明确的增量覆盖范围，不是假实现。不完整的 EDID 可以解析出已知安全子集，也可能没有可用时序；交集不足时返回错误，而不是构造默认模式。DDC／AUX 读取、覆盖系统 EDID、链路训练、mode set、flip／vblank、硬件 HDR 和 reset 仍由后续厂商／平台实现。

## 验证与实测（2026-10-11）

宿主回归包含构造的 EDID／CTA／两代 DisplayID wire fixture、校验和／truncation／块越界／别名拒绝、模式去重、精确位深交集、容量超限，以及 20,000 次修复外层 checksum 后的变异输入。检查所有失败保持原输出、所有成功返回的时序可通过显示模式校验。构造 fixture 和测试替身不是物理显示证据。

Windows 注册表只读采集的六份不同缓存快照（未提交原始序列号数据）实际运行同一 `edid-inspect`：

| 缓存样本 | 结果 |
|---|---|
| AOC 25G3Z，256 字节 | 基础／CTA 成功，8 个完整模式；解析到约 239.964 Hz、HDR EOTF 0x07、type 1 和 RGB BT.2020 |
| CSO 内屏，384 字节 | 基础／CTA／DisplayID 成功，2560×1600，60 Hz 与约 240.002 Hz；HDR EOTF 0x05、RGB BT.2020 |
| Dell S2719DGF，128 字节 | 基础块成功，2560×1440 约 144.477 Hz；没有捏造 HDR |
| 两份截断缓存、另一份含额外块缓存 | 与声明长度不符，明确拒绝；没有悄悄补零或裁剪 |

CSO 基础时序的 293,760 kHz、2560×1600、160／200 blanking 和 60 Hz 与 Windows WMI 模式报告一致。高刷／HDR 的上述结果是**能力字节解析**，不是已经显示这些模式／HDR，也不是实际 DDC／AUX 事务或 macOS 驱动证据。缓存可能来自历史设备或系统覆盖，不能据此宣称所有设备当前在线。

工具用法：`target/Release/edid-inspect[.exe] <raw-edid-file>`；`make test CONFIG=Release` 会构建工具。输入是原始二进制，不是十六进制文本。退出 0 为解析成功，1 为读取／解析失败，64 为参数错误；仍需检查 omitted／unhandled 信息。

## 安全审查范围与依据

资产为内核／宿主内存与后续可触发的模式／HDR 配置。信任边界是可能恶意的显示器／缓存字节 → 解码数据 → 硬件能力交集 → 已有控制 API；大小、块计数、checksum、长度、索引、数值和结果容量均受限，未知结构不授权硬件功能。无网络、命令执行、新 UserClient、地址导出或特权文件写入；CLI 只读取调用者指定文件的一次有界快照。回归、变异测试、静态分析及 CI sanitizer 提供证据，不是安全证明。

格式依据为公开协议字段，并交叉核对 Linux DRM 的 `drm_edid.h`、`drm_displayid_internal.h` 与 edid-decode 的 CTA／DisplayID 字段解释；本实现自行编写，没有导入驱动实现或第三方依赖：

- <https://github.com/torvalds/linux/blob/master/include/drm/drm_edid.h>
- <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/drm_displayid_internal.h>
- <https://github.com/gjasny/v4l-utils/blob/master/utils/edid-decode/parse-displayid-block.cpp>
- <https://github.com/gjasny/v4l-utils/blob/master/utils/edid-decode/parse-cta-block.cpp>

仍需覆盖更多真实屏幕和独立解码器差分比对；OS 缓存不证明硬件身份。后续首先扩充通用时序覆盖与 flip／vblank 完成跟踪，然后由 `metalvk-nvidia` 接实际读取、链路、扫描输出和整卡恢复。
