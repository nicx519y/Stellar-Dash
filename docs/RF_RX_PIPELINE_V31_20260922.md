# RX 8K 流水线与计时交付 v30/v31

代码和构建已完成；没有运行回归用例、开启新设备采集或烧录。**125us 是待实机验收目标，不是本轮已经测出的结果。**

## 交付版本

交付根目录：`.hbox/rx-8k-v30/`。

| 目录 | 内容 | 用途 |
|---|---|---|
| baseline | RX `0x1930` hex/bin/elf、APP 源码快照、构建日志 | 在 v29 行为基础上增加计时，仍为原主循环消费路径 |
| pipeline | RX `0x1931` hex/bin/elf、源码快照、构建日志 | 普通短包直接构包并排队，USB 完成接续 |
| tx-bench5 / tx-bench7 | 独立编译的 TX application bin/elf | 持续变化输入，分别固定5B/7B DATA；默认生产构建关闭 |
| monitor-dist.zip | 本轮 Electron/renderer 构建输出 | 解压到已有 connect-monitor 的 dist 后使用其现有依赖启动；也可直接使用工作区已构建版本 |
| source.zip / SHA256SUMS.txt / manifest.json | 最终相关源码、校验和及验证状态 | 复核与归档 |

两份 RX 都保持空口 v5；正常使用无需改 STM32/TX。TX 测试固件不是正常控制器固件，会自动产生按键模式；不得用于正常游戏。TX 只交付 application 镜像，后续更新仍走既有 `python tools/hbox.py flash tx` 受支持的构建/产物流程，不改 IAP 和烧录入口。本轮未操作设备，未修改任何保护位或锁定状态。

## 数据路径

普通5B/7B输入通过 RF 协议/序号检查后，在接收回调中完成固定18位按键到20B XInput映射。重启接收仍在这些工作之前。映射可被抢占，队列发布和端点装载使用短临界区。

`rx_report_queue.h` 提供4个待发槽和独立在途记录。每项携带本地32位输入身份、radio generation、neutral/reset epoch、报告字节、接收/准备/提交时间及相对延迟事件身份。构包不依赖普通主循环，下一包的到来不取消上一包；相同状态包也正常生成报告。

USB EP2 使用原专用DMA缓冲，报告装载用 RAM 固定长度复制。RF生产者和USB完成路径共用 `pipe_kick`；完成中断先处理旧报告关联，再清端点 busy/T_DONE，最后接续下一报告。装载失败不出队；在途记录和DMA缓冲不会被后续入队覆盖。

队列满或最旧待发报告超过500us时，取消旧待发积压并保留最新输入；待发中立释放不能被普通输入挤掉。500us 是待发积压清理条件，不是对主机停顿或在途传输的强行超时。只有一份最新状态时允许保留其真实年龄，恢复后不伪造新时间戳。

radio generation/epoch变动取消旧关联；已经交给USB的物理传输不强行撤销，其后续完成不再当作有效旧事件完成。USB复位清队列，重新枚举后补当前有效状态或中立。50ms失联保护在提交中立前重新核对最新RF活动，避免检查期间到来的新包被错误释放。

主循环每步最多消费一个辅助/控制 pending 项，延迟记录扫描每步最多4槽。无线维护的大临界区拆成分别原子执行的状态事务；ACK、重启接收和切换事务自身的原子性仍保留，未取消其安全门禁。源记录匹配、辅助重组、遥测构包留在后台。

原临时 latest→输入 FIFO→单一 prepared 的普通短包路径已移除。保留的旧格式控制输入也经同一队列/端点所有者提交，避免出现第二个 EP2 提交者。

## RXP1 诊断

monitor 的 Report Rate 面板新增可展开 RX 流水线区域，分别显示接收、接纳、报告准备、提交和完成频率；USB 未配置/FS/HS/挂起；队列当前与累计峰值；分类丢弃；各阶段直方图分位数、累计最大值、超过125us次数及最近4个超时/丢弃样本。

- magic=`0x31505852`，版本1，32B报文；bytes4..5为16位快照号，byte6为页号0..34，byte7为版本，bytes8..31是6个小端uint32。
- 每秒冻结210个uint32，共35页；发送失败保留当前页。只有完整、有序、同快照且3秒内的页组才发布统计。断开连接时清解析状态，缺页不补零。
- 前12个词依次为周期时钟、快照间隔us、USB状态、队列深度、峰值、pipeline标志、radio generation、最近丢弃身份/代次、最小计数器读取间隔cycles、cycles/us、样本总数。
- 后14个累计计数：received、accepted、rejected、sameMerged、changedOverwritten、ready、submitted、completed、congestionDropped、cancelled、submitFailed、auxDropped、crc、control。
- 后14组计时，每组12词：count/maxUs/over125，随后9个桶。桶上界为8/16/32/64/125/250/500/1000us及溢出桶。阶段顺序以 `rx_profile.h` 与 `shared/rx-profile.ts` 为准。
- 最后4组样本，每组 id/generation/us/cycles。us=`0xffffffff` 表示拥塞丢弃，`0xfffffffe` 表示复位/代次取消；其余为准备超时。
- 所有耗时分布和计数是启用诊断期间的累计值；吞吐根据两个完整快照差分。分位数仅显示桶上界。RXP1 明确标记为诊断，不能增加 DATA 吞吐统计。

测量限制：RF 时间起点在 SDK 进入应用回调之后，SDK前置中断延迟无法由这个钩子测出。CPU估计只扣除已插桩RF/USB/TMR1/TMR2的嵌套执行，不冒充完整硬件CPU占用或WCET。最长关中断区只覆盖项目插桩调用；SDK内部关中断仍不透明。最小连续计数器读取间隔只是校准参考，不等于整套诊断成本；未从延迟中减常数。

计时基线保留旧消费者行为，队列水位字段为0。初始/中立补发和取消使接收/准备/USB计数不能在任意跨复位窗口机械相等。按同一连接代次、正常输入窗口比较，并保留异常分类。基线与流水线共用RXP1格式，但流水线新增的拥塞记录在基线中自然不可产生。

原RLT2的RX=事件接收至首次报告准备、USB=准备至完成保持不变。超时/拥塞/取消的关联标为无效；丢失身份不借用后来同tag报告伪造完成。原 `rfInputEdgeDrop` 无生产者，monitor不再把其零值作为有效丢弃数据。

## 构建与功能用例

已完成构建/静态检查：

- RX计时基线及流水线，正常TX、独立5B/7B测试TX均编译通过。
- monitor TypeScript typecheck、renderer/electron build通过。
- `rx_report_pipeline_test.c` 编译为主机可执行文件，未执行；使用生产队列和映射，覆盖逐键映射、8000份不同输入顺序、时间回绕、USB busy/失败、在途不可变、满队列、500us清理、中立优先、代次/epoch过期及重复取消。
- `rx-profile.test.cjs` 已编写并通过语法检查，未执行；覆盖完整/缺页/乱序/重置/超时/版本及计数回退。旧诊断测试已更新无效edge-drop字段预期。
- ELF核对构包、pipe_accept/pipe_kick/pipe_complete、EP2上传、RF回调、USB ISR及计时热点位于RAM。
- `git diff --check` 和固定烧录契约7个文件SHA-256检查通过。

构建仍有旧延迟兼容函数未使用警告和Vite大chunk提示；不影响编译成功。没有运行自动回归、实际8K负载或HID采集。

复现编译命令：

```powershell
mingw32-make -C RF_PHY_Hop/RX -j8
mingw32-make -C RF_PHY_Hop/TX -j8
mingw32-make -C RF_PHY_Hop/TX -j8 OUT_DIR=build_bench5 EXTRA_DEFINES="-DRF_RX_BENCH_ENABLE=1 -DRF_RX_BENCH_BYTES=5"
mingw32-make -C RF_PHY_Hop/TX -j8 OUT_DIR=build_bench7 EXTRA_DEFINES="-DRF_RX_BENCH_ENABLE=1 -DRF_RX_BENCH_BYTES=7"
npm --prefix connect-monitor run typecheck
npm --prefix connect-monitor run build
gcc -std=c11 -Wall -Wextra -I RF_PHY_Hop/RX/APP/include tools/tests/rx_report_pipeline_test.c -o .hbox/rx_report_pipeline_test.exe
```

测试TX保留正常ACK和连接协议，但固定DATA长度会替代正常辅助分片；7B模式使用现有计数锚点，5B模式不传辅助元数据。RX本地RXP1仍可用。开启测量时测试输入逐包变化tag，用于覆盖事件建档成本；不提供STM32源阶段，不能把这些测试行当完整ADC→USB测量。

## 用户实机验收步骤（本轮未执行）

先用同一现有TX、频道、速率和主机条件分别比较RX `0x1930` 与 `0x1931`，再使用独立测试TX的持续不同输入负载。1K/2K/4K/8K均检查，8K每场景60秒×3次；5B/7B、测量开/关分别记录。正常ACK、主机停顿、拔插/重枚举和重连保留为独立场景。

稳态验收：每份有效接收输入的报告准备最大值≤125us；无不同状态覆盖、队列溢出、重复/乱序提交；队列不持续增长；实际HS下USB完成率跟随有效接收输入率。累计计数应使用前后差，并按代次区分中立/复位。比较RF接收率和ACK失败，不能以增加无线丢包换取RX时间改善。

RF丢包、控制事务和USB停顿必须保留并分类，不能按耗时大小事后剔除。若最大值仍超125us，根据RF/USB/临界区/后台计时及尖峰身份继续定位，不能把编译通过写成8K验收通过。
