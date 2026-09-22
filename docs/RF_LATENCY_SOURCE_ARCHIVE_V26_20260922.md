# 源测量记录归档修复 v26（2026-09-22）

本次修改 STM32 application、TX、RX 和 connect-monitor。RX build ID 为 `0x1926`，RF 空口仍为 v4，普通输入仍为 5/7 bytes。STM32 必须使用 `unlocked-development`。没有修改速率、跳频策略、IAP、地址布局或已验收烧录入口。

## 修复

- SPI 输入新增显式 v3 格式：保持 10-byte payload、按键/电池/CRC 位置，使用此前恒为零的 bytes 6..7 携带完整 16-bit 事件编号。源 sidecar 保持 20-byte payload，最后一个版本字节改为 2。格式定义在 `common/rf_source_trace.h`。STM32 和 TX 需要配套更新；旧 TX 不会识别新源 sidecar，混用时不能验收延迟。
- TX 源数据不再要求“第一份被 TX 软件观察到的输入序号”必须等于 STM32 的首帧序号。新版按完整事件编号匹配，随后使用源记录中的**原始 SPI 序号**查找同一完整事件的 NSS 边界；后续样本边界不能替代它。已有 RF 发送尝试在补档时保留。
- 暂时尚无对应输入的新版源记录进入独立 8 槽待匹配队列。一秒过期，重复副本不占新槽、不延长寿命、不修改已冻结记录。旧格式没有足够身份信息，只保留原来的立即严格匹配，不进行跨时间补档，避免 tag/SPI 序号回绕误配。
- NSS 在校验输入后，把同一 38-byte 事务的有效 sidecar 复制到独立环形缓冲（8 槽、可用 7 条），主循环再解析归档。DMA 主循环解析作为兜底继续保留；两路重复交付是幂等的。中断只处理固定长度复制和校验，不进行源记录匹配或 RF 发送。仅带 sidecar 的稀疏输入事务增加这段工作，测量关闭时不增加。
- 源队列的接纳、消费与测量开关/连接重置互斥。缺源 TX 槽也执行一秒过期，避免只能等 tag 复用才结束；RF TRACE 提交前复核事件、SPI 序号、槽出生时间和连接代次。
- RX 为等待超过一秒且仍不完整的记录增加 RLT2 bit6；监视器显示 `Source record timeout` 或 `USB completion timeout`，保留已确认的局部阶段。没有真实 NSS 边界时显示 STM32 四段及 `TX timing unavailable`，不生成总耗时。

## 诊断

测量开启时，TX 最多每两秒安排一份 `RFH_AUX_SOURCE_DIAG`（type 10，28 bytes）。沿现有附带分片传输，不新增独立 RF 控制时隙；会增加少量附带分片流量。RX 通过 `RLS1` 上报，monitor 解析为 `RFH_RLS1` packet 并进入现有日志。

| 字段 | 计数含义 |
| --- | --- |
| `rfSourceReceived` | 源记录接纳函数收到的有效格式副本数；包括三次 SPI 重复和 NSS/DMA 两路交付，不能当事件数 |
| `rfSourceMatched` | 首次成功绑定的源事件数 |
| `rfSourceExpired` | 独立待匹配队列的一秒过期数 |
| `rfSourceQueueDrops` | 待匹配队列满时的新记录丢弃数 |
| `rfSourceBoundaryMissing` | TRACE 发出时仍缺原始 NSS 边界的记录数 |
| `rfSourceSpiDrops` | NSS 归档溢出/复制失效与 SPI DMA 积压丢弃、FIFO 溢出操作数之和；不是精确丢失源事件数 |
| `rfSourceIdentityWaits` | 首次无法立即绑定而进入等待的新副本数，或旧格式立即身份拒收数；队列满尝试也计入 |

计数为本次 TX 启动以来累计值。`RHP2.rfTraceOverwrites` 继续表示 TX 测量槽覆盖/过期，不能与上述计数简单相加。RLS1 自身仍可能因无线分片丢失而暂时不上报。

## 编译和验证边界

交付目录：`.hbox/rf-latency-source-v26-20260922/`，哈希及构建状态见目录中的 `manifest.json` 和 `SHA256SUMS.txt`。

编译期间另一个任务在同一工作区加入 ACK v27 / 空口 v5 修改。为避免混合交付，最终 TX/RX 从基线提交 `15d255e` 加上本次源归档修复的隔离快照编译，保留 RX `0x1926` / 空口 v4。隔离源及其 SHA-256 清单一并归档；工作区中的并行改动继续保留。本目录不是当前工作区所有并行修改的合并版本。

编译入口：

```powershell
python tools/webconfig_local.py build --slot A --skip-web --unlocked-development --jobs 8
mingw32-make -C RF_PHY_Hop/RX -j8
# connect-monitor 目录
npm run typecheck
npm run build
```

最终 TX/RX 实际构建位置为 `.hbox/latency-source-v26-snapshot/RF_PHY_Hop/{TX,RX}`，使用 `mingw32-make -j8 SDK_ROOT_MAKE=../../sdk-source`；`sdk-source` 为指向现有 CH585 SDK 的目录联接，仅用于构建路径解析。没有修改冻结 Makefile。

增加源先到/输入后到、首帧 SPI 身份被后续样本越过、tag 复用不误配、缺物理边界不补零、队列过期/溢出及桌面超时/诊断的测试代码。按既有用户要求，本轮不执行回归或硬件采集，不将编译通过当作行为测试或实机验收。已有测试文件中触及的源记录 fixtures 同步了辅助函数依赖；不宣称历史整套测试已通过。

冻结烧录文件仅做静态 SHA-256 对照，未改动。没有烧录，没有修改任何保护位或锁定状态。

## 用户验证

本轮没有执行更新。后续验证须使用同一套配套产物：STM32 Slot A application、归档 TX 和 RX `0x1926`，以及新监视器。工作区烧录入口默认使用工作区构建文件，可能已经是并行任务的空口 v5；不能将其与这里的空口 v4 RX 混用。刷写前先核对完整配套版本，并沿既有无锁入口执行，TX 只写 0x1000 以上 Application。本次无需为此修改 bootloader 或任何硬件保护配置。

重新启用测量、清空旧记录，先低频单键按下/松开，再验证快速交替。检查完整率及 RLS1 增量：待匹配/队列丢弃增长指向源归档；缺 NSS 增长指向真实首帧边界捕获；TX 已绑定而 RX 仍超时，需要继续查 RF TRACE 分片。

本轮修复确定的源身份和生命周期失效路径，不承诺 100% 完整率。首帧的物理 NSS 边界确实未捕获、超过前六次 RF 尝试才被 RX 采用、三轮无线分片均缺片时，仍保留不完整记录。不能用按键值猜配、放宽 RF 序号校验或过滤不完整行来制造完整率。
