# XORA WebHID 高速链路实施记录

日期：2026-09-27。状态：**核心协议和诊断实现已落盘，整体计划未完成，不是已验收固件。已开始普通实机烧录和连接排查，未发布线上网页。** 当前进展见[连接故障排查](webhid-connection-repair-20260927.md)；下文主机验收表及未烧录记录属于首次实施阶段。

## 已实现

- 唯一 V2 报告：1024 字节报告、16 字节认证头、996 字节载荷、12 字节 GCM 标签；独立 V2 KDF 上下文、双向密钥和 nonce 前缀；检查代次、序号、长度及零填充。
- TX 保持接口 0、端点 0x81/0x02、既有 VID/PID/Usage，设备版本 3.00。HS 端点 1024 字节、bInterval=1；FS 合法枚举，但拒绝配置会话。EP0 只读 32 字节能力查询，网页先验证版本、HS、桥接 ready。
- 新 SPI 配置块独立于启动/IAP/XInput，16 位长度、连接代次、序号、累计 ACK、绝对 credit 上限及 CRC32，最多 3 个完整 HID 报告。双向窗口 8，数据最多占 7 槽；TX 透明转发密文。
- 删除旧配置分片发送状态、旧配置 credit 查询/运行时路径及 8 ms pacing；旧配置 bulk 被拒绝。启动、角色、IAP 控制布局保留。历史技术标识/旧向量文件不代表运行时兼容旧 HID。
- SPI 准备/确认/切换/探测/提交及 15 MHz DMA 路径；初始化失败保留能力查询，不回退旧配置协议。DMA/CRC 故障会使加密会话失效。
- 浏览器 RPC 分片、图片、固件/导入流最多并行准备/提交 4 个报告，按序发送、有界等待；保留业务 credit、接收确认和最终提交。
- 图片能力尾部扩展为 16 位长度；Mock、V2 黄金向量、描述符、命令契约同步更新。
- 本机 `/webhid-benchmark`：RAM 加密上传/下载、并发小 RPC、2 秒预热、10 秒三轮、10 分钟运行、取消及 JSON 导出。`benchmark.start/status/stop` 只在无锁开发固件启用，接收字节数/顺序/CRC 一致后才成功。
- ELF 定位的 160 字节 `g_webhid_benchmark` RAM 结构及 [ST-LINK 采集脚本](../tools/webhid_benchmark_capture.py)：结束后两次非 halt SRAM 读取，匹配 run ID/字节数/CRC/配置，输出 JSON/CSV/Markdown。不读保护寄存器、不加载 Flash 驱动、不记录密钥。

协议布局见 [V2 协议说明](webhid-hs-protocol-v2.md)。

## 尚未完成

1. STM32 DMA 目前同步轮询完成，尚未实现计划要求的异步双缓冲流水线；TX 双缓冲 DMA 也未完成板级验证。
2. SPI 独立 RAM 吞吐测试未实现；15/7.5 MHz 板级比较、512 字节/窗口 4 对照及最终选型未完成。固定 15 MHz 只是当前候选。
3. 协商中断、NSS 截断、DMA 超时、队列满、取消、重连和复位的硬件故障注入未完成。部分 SPI 块中断后的重新同步仍需验证。
4. 浏览器已记录加密/提交等待/native send 时长，但 USB NAK 混在 native send 中，不能单独量出桥接 credit 等待。TX 计数覆盖连接生命周期，不是单次 run 增量；并发阶段累计时长有重叠。
5. 三端完整 manifest 构建、槽位确认、烧录、真实浏览器测速、ST-LINK 完整取证、图片上传和 10 分钟稳定性验收均未完成。没有前后吞吐、P95 或最慢环节 70% 比较结果。

## 阻碍和设备状态

浏览器工具多次返回 `Browsers: Error: nodeRepl.fetch request failed`，可用浏览器列表为空，无法取得原网页只读基线或运行真实 WebHID 测试。未用 Mock/原生 HID 结果替代验收。

`python tools/hbox.py web local-ch585-status` 只读查询成功，旧 TX 暂存状态 `APPLIED`。这不证明新版高速链路可用。没有烧录、没有访问或修改保护位、没有重刷 bootloader/IAP，没有恢复 RF 自动回归/采样/跳频。

旧 STM32/TX 构建产物备份于 `.hbox/webhid-hs/baseline/`，尚未证明等同实机镜像。用户已有 `application/www/components/ui/loading-modal.tsx` 改动保留。

## 实际验证

| 检查 | 结果 |
| --- | --- |
| Fast link、真实 TX bridge 主机测试、描述符、C/JS 黄金向量、采集边界、binary ACK、配置写策略/原子日志 | Python 定向套件 23/23 通过，7.2 秒 |
| `npm run test:webhid` | 183/184 通过，3.4 秒；唯一失败是已有 layout 文案源码断言，HEAD 已使用 `connectionErrorMessage(...)`，旧断言期待 `deviceError?.message` |
| `npm run test:mock` | 53/53 通过，6.6 秒 |
| `npm run typecheck` | 通过，6.6 秒；最后网页改动又经过 hosted 构建类型检查 |
| `npm run build:hosted` | 构建及隔离检查通过，59.7 秒。临时 runner 打印 Unicode 日志时发生 GBK 编码异常；子进程退出码 0，postbuild 日志通过，未重复构建 |
| STM32 无锁 make | 通过，16.2 秒；仅编译，不是完整可烧录 manifest |
| TX make | 通过，1.8 秒；RAM 121100/131072 字节，剩余约 10 KB，无实机栈水位证据 |
| ELF/map | STM32 DMA 缓冲在 SRAM D2、32 字节对齐；诊断符号 160 字节，在 D3 `0x38000000`，未占 boot-profile；固件链接 RWX 警告仍存在 |
| Scope matrix | 2 个旧 image stream 源码断言失败，HEAD 也已无这些分支 |
| Command manifest | 6 项中 3 项失败；用 HEAD 的 manifest/dispatcher/registry/scope 复跑同样 3 项失败，涉及此前 RF binding 命令未同步旧清单，未扩大修复范围 |
| 冻结契约 | build.py、hbox.py、webconfig_flash.py 三项已有待验收哈希差异；本任务未修改它们或冻结哈希 |
| 差异检查 | `git diff --check` 通过 |

日志位于 `.hbox/webhid-hs/`，最终主机套件为 `host-release-check.log`，最终编译为 `stm-release-check.log` / `tx-release-check.log` / `hosted-release-check.log`。这些名称不表示发布或实机验收。

## 后续采集

完成剩余实现、配套构建和既有无锁烧录门禁后，通过本地网页导出 JSON。每轮结束立即采集，RAM 仅保存最后一轮：

```text
python tools/webhid_benchmark_capture.py --elf <STM32.elf> --tx-image <TX_application.bin> --manifest <无锁manifest.json> --browser-report <网页JSON> --run-id <本轮ID> --serial <ST-LINK序列号> --output <证据目录> --openocd <OpenOCD路径>
```

脚本哈希标识传入产物，不是固件远程证明。吞吐期间不高频读取 SWD；实际烧录仍须逐次通过目标、地址、无锁 manifest 和回读检查。本记录未放宽硬件安全规则。
