# 显示控制 API：厂商后端接入契约

入口：`#include "display/init.h"`（也由 `metalvk.h` 聚合）。实现：`src/display/control.c`、`validation.c`，编入 SDK-free `libmetalvk.a`；原生构建还将相同实现编入共享内核平台库并检查 kext 中的定义符号。该 API 是进程／内核内部的显示控制接口，不是稳定二进制插件 ABI、IPC 协议或 GPU 命令提交 ABI。

## 已实现与未实现

**已实现**：完整 progressive RGB 时序与刷新率运算、能力校验、SDR／PQ／HLG 与位深约束、PQ 静态 HDR 元数据校验及传递、厂商回调调用、配置事务状态、显示复位与配置恢复、热拔插／链路丢失／挂起通知、并发失效与关闭门槛。不是只有声明，也没有内建“模拟硬件成功”的后端。

**未实现**：EDID／DisplayID 原始字节解析、HDMI／DP 数据包编码或实际链路训练、动态 HDR、YCbCr／色度抽样、交错扫描、VRR、vblank／page flip、IOSurface／WindowServer／IOFramebuffer／AGDC 接线、实际显示寄存器写入、整 GPU reset／DMA 生命周期。这些不能由本接口单独完成。本轮不宣称屏幕或 HDR 已在真机可用。

## 接口与数据方向

| API／回调 | 数据方向与行为 |
|---|---|
| `mtvk_display_init` | 每连接器一个固定存储 manager，指定非零连接器 ID、后端上下文与所有者锁；没有动态分配 |
| `mtvk_display_connect` | 厂商将已核验的显示器／硬件能力传给通用层；只允许无在途工作且 disconnected／idle、无需清理时发布 |
| `mtvk_display_validate_capabilities/config` | 可独立预检，非法值不会触发硬件回调；硬件后端仍须再校验实际组合与链路带宽 |
| `mtvk_display_refresh_millihz` | 从 pixel clock 与 total timing 推导毫赫兹，采用整数向下取整；失败不修改输出 |
| `mtvk_display_apply` → `backend.apply` | 传递完整选定 mode + config，不只传分辨率；真实成功才发布 applied 状态 |
| `mtvk_display_notify` | 厂商／系统适配层报告 unplug、link lost、suspend，立即使旧 applied 状态失效，不假装硬件已停止 |
| `mtvk_display_recover` → `backend.reset` → `backend.apply` | 请求真实显示复位；成功后恢复此前已成功的 desired 配置，包含 HDR；没有 reset 回调则拒绝 |
| `mtvk_display_disable` → `backend.disable` | 真实停止并排空该输出；成功后清除 desired／applied 和待清理标记 |
| `mtvk_display_copy` | 同锁复制 snapshot：phase、generation、busy、needs_quiesce、last_error、能力和配置 |
| `mtvk_display_close` | 拒绝在途或未停止／状态不确定的硬件；不强制释放资源，不是 kext 卸载屏障 |

Apple 私有 HDR 元数据方法的参数方向与调用语义尚未逐 OS build 核验；这里规定的是**自有内部控制流**，不能据此推断 Apple 私有接口调用方向。

## 参数表示与边界

- v1 为 progressive、RGB 输出，mode ID 非零且在当前能力集合中唯一。最大 32 个候选 mode、活动尺寸 16384、total timing 65535、时钟 4,000,000 kHz；后端应显式选择可提供的集合，不能把被裁剪的原始 EDID 冒充完整解析结果。
- 时序含 width／height、htotal／vtotal、sync start／end、sync polarity 和 pixel clock（kHz），保留真实刷新率，不限制为整数 60 Hz。
- 位深范围 8–16；PQ／HLG 要求 BT.2020、至少 10 bit，并在 capabilities 中声明支持。范围不表示每个 mode／位深组合都可用，后端必须核验带宽、时钟、扫描输出和实际 sink 支持。
- `metadata_present` 仅允许 PQ + 明确声明 static metadata 支持。HLG／SDR 不接受静态 metadata；关闭 metadata 时其字段必须全零，避免继承上一配置的 HDR。
- primaries 按 R／G／B 排列，xy 单位 1/50000、坐标为正且 x+y≤50000；mastering luminance 为 milli-cd/m²、最大 10,000 cd/m²，min<max；CLL／FALL 为 cd/m²，零为未指定，非零 FALL 必须≤非零 CLL。
- 元数据是语义表示，不是 HDMI infoframe、DP SDP 或 Apple 私有结构体。具体编码、颜色矩阵、传递函数和硬件验证由厂商／平台模块实现；能力或参数非法不做静默降级。

## 事务、复位与热插拔

apply／disable 成功必须表示**同步完成真实硬件操作**，不能仅表示已把请求排队。普通失败必须保留先前硬件配置（自行事务回滚）；不能保证回滚时返回 `MTVK_CLEANUP_REQUIRED`。未知返回码同样视为 CLEANUP_REQUIRED，进入 FAULT。reset 成功必须使输出处于停止状态，随后由 apply 恢复；reset 或恢复失败均进入 FAULT，不宣称先前状态仍已生效。

通用层在同一所有者锁内校验并进入 busy，锁外调用硬件，再同锁发布结果。其他 apply／disable／recover／connect／close 在 busy 期间返回 Busy，但 notify 可递增 generation 并更新状态，阻止在途完成发布过期成功。reset 完成后已观察到失效时不会调用恢复 apply。检查与实际硬件操作之间仍可能发生物理拔插：**generation 只防止过期状态发布，不提供硬件取消或物理原子性**；后端须用自己的硬件／链路同步及独立身份复核处理此边界。

若通知与操作竞态，即使回调返回 OK，API 仍返回 CLEANUP_REQUIRED、保留 `needs_quiesce`。不能直接重新 connect 或 close，须先成功 disable/drain。unplug 清除 desired 与 capability，避免重新插入不同屏幕后回放旧 HDR；单纯 suspend／link loss 保留已成功的 desired。wake 用 RESET_WAKE；不会自动宣称恢复成功或自行安排线程。

generation 达到 UINT64_MAX 后不回绕，拒绝新配置／恢复。无法递增的信号仍进入 FAULT 并使在途阶段失效；只允许 disable 排空和 close。重新建立 manager 之前必须停止生产者并完成真实清理，不能重置发布中的对象。

## 厂商接线与线程契约

1. 主设备管理器为每连接器持有 manager、稳定的 backend context 与专用状态锁。锁／解锁回调是必需项；硬件 apply／disable／reset 缺失时相应操作返回 NOT_SUPPORTED，绝不默认成功。
2. 后端读取、解析并验证 sink 能力，再调用 connect。配置请求由 macOS 平台适配层或受控内部调用者发起；当前没有新 UserClient 入口。
3. IRQ 通知应通过已存在的工作队列移交合适线程。仅在状态锁明确 IRQ-safe 且该路径不睡眠时才能直接 notify；apply／disable／recover 的回调上下文须满足实际硬件操作要求。
4. 硬件回调可重入 copy／notify；不持有 manager 锁。重入配置操作返回 Busy，不等待自身。backend context／函数表不可变，输入对象调用期间不得被另一线程修改，manager 不可复制或公开修改字段。
5. 回调必须有厂商超时、错误传播和回滚／清理协议；通用 C 无法抢占永久挂起的回调，也不会超时后强行释放正在使用的资源。连接器关闭后仍须停 IRQ／workqueue，并确保所有进入者静止，再释放锁／context。
6. 多输出共享 display engine 或整 GPU reset 必须由厂商父管理器统一暂停和恢复所有连接器、队列与 DMA；此单连接器 manager 不负责跨设备事务，不允许把这里的 reset 当作完整 GPU 故障恢复。

## 验证与安全范围

`tests/test_display.c` 使用**仅测试内的回调 spy／故障注入**，验证模式与 HDR 转交、SDR 清除元数据、恢复失败、未知错误码、unplug 阻止 replay、参数拒绝、20,000 次非法参数变异、generation 耗尽和清理门槛。`test_display_threads.cpp` 使用真实宿主 mutex／线程与受控阻塞回调，验证 busy、锁外回调、信号使在途成功失效及后续清理；不是物理显示测试。ASan／UBSan／TSan 与静态分析检查本通用实现，不证明厂商回调正确。

安全边界：可信内部对象／回调；外部输入是解析后的屏幕能力、配置和事件。大小、枚举、掩码、时序、metadata 均在硬件回调前校验，mode 数量及校验循环有硬上限，64 位刷新率计算无乘法溢出；拒绝 manager 内部字段与参数／snapshot 输出重叠；结果发布与通知同锁，不持锁执行硬件。assets 是输出状态、后端对象生命周期及可触发的硬件操作，不新增认证、用户态执行入口、路径、网络访问或裸地址导出。

残余风险与验收：后端提供虚假能力、未回滚失败、未设超时、未经同步操作硬件或违反对象生命周期，不能由通用层修复。接入 IPC 时还须独立序列化、权限／连接器所有权和配额检查，不能直接 memcpy 此结构体。真实模式切换、HDR 信号分析、热插拔／唤醒、reset 与多头恢复，必须在隔离真机验证；当前都没有硬件通过证据。
