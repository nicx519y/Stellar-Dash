# XORA 设备故障日志

## 实现范围

STM32 与 CH585 TX 独立记录，WebConfig 通过现有加密 WebHID 会话和 BoardLink 管理事务读取。RX、云端上传、自动烧录和部署不在本次范围内。旧 `LOG_*` 文本调用仍为空实现；结构化入口是 `XoraLog_Event`，`Logger_Flush` 只设置申请标志，不能直接擦写。

记录处理器异常、致命入口、角色启动失败、已有链路故障判定和恢复转换；不把正常断开、模式切换、休眠或单次丢包当作故障。原串口高频日志开关保持不变。

## 格式与隔离

| 项目 | 定义 |
|---|---|
| STM32 | QSPI `0x90560000–0x9056FFFF`，C/Python 的 `FAULT_LOG_STORAGE_*`；配置 bank B 仍在 `0x90580000` |
| TX | **Data-Flash** `0x1000–0x5FFF`；不是 Application 代码 Flash。metadata 在 `0x0000`，绑定区 `0x6000–0x7FFF` |
| 记录 | `common/fault_log.h`：V1 magic、序号、启动编号、uptime、固件 semver、事件、来源、级别、类别、重复次数、14 个现场 word、CRC、最终提交字 |
| 空间 | 96 字节正文占 128 字节槽；4 KiB 扇区循环，仅回收下一个最旧扇区 |
| 提交 | 写正文、回读校验、写最终提交字、再次回读；提交前断电不产生有效记录 |
| 损坏 | 有效记录继续可读；未知格式/损坏已提交记录所在扇区不自动擦除，读失败禁止追加；不提供清空接口 |

CRC 覆盖提交字之前的正文。未知格式可能阻止继续写入，状态必须如实返回，不能通过自动格式化恢复。序号达到 `UINT32_MAX` 时停止追加，不回绕为 0。

两端的启动编号、运行时间各自独立，不能据此跨芯片排序。固件号编码为 `major<<16 | minor<<8 | patch`。导出时间属于浏览器，不是事件发生时间。

## RAM、异常与写入窗口

- 每端两个 RAM 事件槽，重复事件合并计数；满时统计丢弃，致命现场使用独立保留 mailbox。排空后追加丢弃摘要。日志写入失败在本次启动锁存，不递归记录自身错误或无限重试。
- STM32 异常入口保存原始 `EXC_RETURN`，选择 MSP/PSP，处理浮点扩展帧，校验地址与 stacking fault 后读取寄存器。NMI、HardFault、MemManage、BusFault、UsageFault、`Error_Handler` 和 assert 进入同一最小捕获路径。
- 异常路径不使用堆、锁、格式化或 Flash 操作；保留 RAM 带 magic/CRC，STM32 在重启前清理对应 DCache。下次启动成功提交后才消费，复位发生在提交与消费之间时由不可变现场去重。
- STM32 mailbox 为 `.xora_fault (NOLOAD)`，在 BSS 后、DTCM 栈外；启动代码按符号复制/清零。bootloader 的数据、BSS 和栈在 DTCM。TX `.noinit.xora_fault` 在 `_ebss` 后，独立 IAP 的 load/clear 区不重叠；当前链接不含 `_sbrk`。每次改链接、SDK 或静态 RAM 分配后必须重新运行 ELF 布局检查。
- TX 日志状态、事件槽、保留现场和计时状态预算合计不超过 512 字节，不使用整扇区缓存。ELF 检查还要求保留区上方至少 2 KiB 栈空间；静态空间检查不等于实机栈峰值验证。
- 启动时在传输启动前提交保留现场。运行期只在 Maintenance/WebConfig 且 USB 未挂载时服务；STM32 同时排除升级占用。正常 USB/RF 输入及浏览器已连接期间不擦写日志。
- 服务调用在同步主循环、RPC 处理结束后执行，因此与配置、资源、升级、绑定写入串行。QSPI 写入临时移除等待回调，恢复映射并失效日志缓存；映射恢复失败转入 RAM 异常记录/复位，不能继续读失效映射。每次服务最多提交一条，底层擦除/编程等待使用原驱动的有界超时。
- 连续三次异常重启且未稳定运行 30 秒时进入维护限制。TX 拒绝业务角色并返回 `ROLE_LOCKED`，STM32 转入 WebConfig。维护路径再次异常时停止自动复位；持久 TX ISP 状态仍优先，不改变已验收 ISP 流程。

RAM 缓冲和保留现场只保证普通复位路径；断电、主控切断 TX 电源或启动前的严重异常可能丢失未落盘记录。日志不延长握手时限，不取消原有掉电恢复。已有提交记录与配置、绑定数据相互独立。

## 读取协议

`get_device_logs_list` 保留 `items: string[]` 和 `count`，返回 STM32 最近最多 50 条。新增 `get_device_logs_page`：

```json
{"source":"stm32","category":0,"severity":0,"limit":8}
```

来源为 `stm32` / `tx`，类别 0=全部、1=异常、2=链路、3=恢复；级别 0=全部、2=警告、3=错误、4=致命。响应 V1 包含 `status`、`upper`、`oldest`、`nextBefore`、`more`、`overwritten`、`pending`、`dropped`、`firmware`、`restricted`、`items`。

下一页传回同一来源的 `upper`、`oldest` 和 `before=nextBefore`。上界固定、序号向下推进；保留 RAM 仅在首页读取。循环覆盖使原始 oldest 消失时返回 `overwritten`。RAM 快照不强制落盘，也不与后续页重复合并。

状态区分 `ok`（可能为空）、`unsupported`、`unavailable`、`storage_error`。单个损坏/不可读槽不妨碍返回其余有效记录；整端断线不阻断另一端。读取不能触发日志写入。

TX 使用 `USB_BOARD_CONTROL_FAULT_LOG`，INFO 返回 `fl_info` V1；READ 请求为 upper/before/category/severity/pending-index/offset，96 字节记录分两帧传输，沿用事务号校验并额外核对 offset、长度与 CRC。单字节 `0xFF` 表示没有匹配记录，与超时不同。没有新增 RF 空口协议。

## WebConfig

所有已连接用户可进入“设备日志”。默认按来源分组、组内倒序；提供来源/类别/级别筛选、现场展开、手动刷新、分页和 JSON/TXT 完整快照导出。首次初始化就绪读取一次，无后台轮询；无手动清空入口。

导出在当前筛选下重新读取每端完整分页；部分失败、损坏、覆盖或丢弃设置 `complete=false`，保留可读结果与各端状态。待落盘标记始终说明断电丢失风险。旧固件不支持新命令时显示兼容提示。

Mock：`/view-logs/?mockLogs=normal|empty|unsupported|tx-unavailable|corrupt|overflow`，只在 Mock 变体使用。

## 验证与实机验收

纯主机入口（不访问设备）：

```text
python -m unittest tools.tests.test_fault_log tools.tests.test_fault_log_rpc
make -C application HBOX_SECURE_BOOT_REQUIRED=0
make -C RF_PHY_Hop/TX
python -m unittest tools.tests.test_fault_log_layout
python tools/generate_webhid_command_policy.py --check
```

WebConfig 目录运行 `npm run typecheck`、`npm run test:logs`、受影响 Mock/队列/路由测试和 `npm run build:hosted`。UI 另用 Mock 浏览器验证桌面、窄屏、中英文、筛选和实际下载。

实机验收须另行明确授权：注入异常后复位读取、断电读回已提交记录、TX 单端失联、损坏/覆盖提示、正常切换不误报、三次崩溃限制、维护再次崩溃停机、栈峰值、启动握手与输入延迟。主机模型和编译通过不能替代这些结果，当前不运行暂停中的 RF 回归或设备采样。

回退上一版本应用固件与网页即可停止功能；不清空日志区，不迁移配置或绑定，不修改 bootloader/IAP 镜像、冻结烧录脚本或保护状态。

### 本次交付检查（2026-10-21）

- 存储/RPC/ELF 共 5 个主机测试通过：正文和提交字逐字节中断、回收扇区中断、CRC/读取失败、循环覆盖、队列合并/溢出、保留现场重复消费、分页上界、TX 分块 CRC/偏移、单端失败与维护限制。C/Python 分区一致性及 TX metadata/绑定边界有编译断言。
- STM32 显式 `HBOX_SECURE_BOOT_REQUIRED=0` 与 TX 编译通过。TX 日志静态 RAM 合计 358 字节，mailbox 以上栈保留 2224 字节；STM32 mailbox 在 `0x2407F2E0`，AXI 尾部余量 3248 字节。数值仅对应此次产物，后续以 ELF 为准。
- 前端日志、Mock 适配器、请求队列、状态订阅与路由定向测试 110 项通过（约 14 秒）；类型检查通过（约 15 秒）；最后一次 Hosted 构建与变体隔离检查通过（约 65 秒）。构建仍有其他组件 Hook 依赖、固件宏重定义/未使用变量、TX 旧入口缺少返回值和 RWX 段警告，未将这些警告当作本次功能验证。
- Mock 浏览器验证了中英文、390px 窄屏、来源/类别筛选和实际下载：正常 JSON/TXT 两端各 19 条；TX 不可用时保留 STM32 19 条且 `complete=false`；溢出场景带 pending/dropped 并标记不完整。空记录、旧固件、损坏和游标异常另有主机 fixture 覆盖。
- 全量旧命令契约测试仍有两项既有失败：缺少资源/RF 绑定用例与相关链接 stub。用改动前源码/manifest/test 的独立临时基线复现了相同失败。新增日志 RPC 已使用生产 handler 独立验证，未降低旧断言。
- 冻结哈希检查仍失败；契约及其全部受保护路径与本任务基线的 Git 归一化字节一致，未修改这些文件或更新哈希。模拟的 TX 固定烧录路由检查通过。不宣称仓库全套检查通过。
- 未连接设备、烧录、部署、修改保护位，未运行暂停中的 RF 回归/采样。断电、异常复位与实际输入性能仍是待授权的实机验收项。
