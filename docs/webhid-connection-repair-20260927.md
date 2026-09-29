# XORA WebConfig 连接故障排查

## 2026-09-28 重连时报端口 0x0A：待实机验证的修复

本轮用户控制台截图为 `phase=discovering/opening`、`command=none`、高速桥接未就绪，fault=26（0x1A，端口状态 0x0A）。失败在只读 Feature 就绪检查，早于加密会话。TX 的端口 0x0A 来自 `tx_dma_finish()`：NSS 释放时没有 CNT_END，被判定为发送中断。

源码存在可触发同一状态的仲裁路径：STM32 写入前拉低 NSS，在 ownership guard 后若发现 W_INT 已被 TX 占用，会不发 SPI 数据就释放 NSS；TX 主循环若在此期间记录了 `s_tx_nss_seen`，此前仍会把它当成半包中断，随后锁存高速链路故障。这是源码确认的缺陷，但截图没有保留首错瞬间的 SPI 标志/计数，不能断言现场唯一根因已闭环。

本轮修改只针对 WebHID 模式：CNT_END 未置位、FST_BYTE 未置位且 TOTAL_CNT 保持原长度时，保留已排队事件、DMA/FIFO 和 W_INT，清除本次 NSS 观察标记并等待真正读取。TX IRQ 在此模式下保留 FST_BYTE，避免真实半包被误认为未开始；完整发送和真实中途终止的处理保持原有语义。普通 Application 输入未启用此分支。网页新增 `bridge-not-ready` 分类，中英文提示明确需要设备完全断电、重新进入 WebConfig；不再仅提示反复重连。

验证记录：

- `make -C RF_PHY_Hop/TX -j4` 成功，1.8 秒；FLASH 110604 字节，RAM 128452 字节。链接器报告 RWX LOAD 警告。产物含当前工作区既有 TX 改动，本轮未回退或覆盖它们。
- 网页 V2 能力/帧测试与连接提示测试共 7 项通过；额外直连会话测试 1 项通过、1 项失败。失败测试仍使用 `HBox WebHID v1` HKDF 上下文，而实现使用 V2；在隔离导出的未修改 HEAD 基线上复现相同断言失败，未修改该测试或加密实现。
- `npm run typecheck` 成功，9.8 秒；`npm run build:hosted` 及产物隔离检查成功，37.4 秒。构建有 React Hooks 警告。
- 日志：`.hbox/webhid-hs/reconnect-{tx-build,web-tests,typecheck,hosted-build,direct-baseline}.log`。TX 打包镜像 SHA-256：`2d43de7e9806e39306ff8f17a655cfbdd3c5c57184a62f9a5f4a1841f18d7094`。

遵守暂停要求，未执行 TX 自动回归、设备采样或烧录；未修改保护位或锁定状态。浏览器控制工具仍不可用，设备实际连接恢复尚未验收。刷入修复版及浏览器连接验收为剩余步骤，不能将上述编译结果视为修复已在设备生效。

## 已取得的实机证据

- ST-LINK USB 接口在线。最小 HLA Cortex-M 配置可以读取 SRAM，不加载 Flash 驱动，不暂停处理器。此前 dapdirect 连接失败不能作为 ST-LINK 离线的证据。
- 用户确认 STM32 和 TX 均已刷写。运行代码与当前 STM32 产物一致，不能将问题归因于旧固件。
- 初始状态为 WebConfig `ErrorMaintenance`，主状态机进入 `SafeRecovery`。原来的旋钮重试仅设置 WebConfig 局部标志，恢复状态不会消费此标志。
- TX SPI DMA 寄存器使用 SRAM 总线偏移；与 `0x20000000` CPU 指针直接比较导致收包位置始终被拒绝。两侧统一为 17 位 SRAM 偏移后，实机能够枚举 `CAFE:4021`。
- 中间版本的只读 Feature 查询：协议 2、报告 1024 字节、USB 速度 HS、窗口 8，但桥接未就绪，fault=2（探测尚未提交）。启动记录停在 `HS_PREPARE`，原始帧捕获到有效确认 `5a85043101000015`（opcode=0x31、transaction=1、status=0）。
- 移除 DMA 换向整块清空并延长接收就绪截止时间后，实机通过握手：启动阶段 6（Ready）、HS 阶段 0x46（Committed）、探测接收块数 1、完整性失败标志 0；STM32 高速端口和链路 ready 均为 1。只读 Feature 返回 **HS / 1024 字节 / 15 MHz / 窗口 8 / bridge_ready=1 / fault=0 / epoch=1**。这仍不是浏览器连接或吞吐验收。
- TX 烧录入口末尾的状态读取会 halt、读取 QSPI、再 resume。持续 ADC 采样在这一操作后曾进入错误状态；普通复位后恢复。吞吐诊断不能复用该状态读取作为非暂停采集。

## 修复范围

- WebConfig 错误页短按执行退出，ADC 故障显示实际错误。独立的重试 API 改由主循环在屏幕回调返回后执行普通复位。
- 修正 CH585 DMA 地址归一化，保持启动、角色选择、IAP 协议及 Flash 布局。
- 高速提交之前保留启动交接间隔；PREPARE 使用最多三次、相同连接代次的重试，不重放业务请求。
- RX DMA 只发布 DMA_NOW 已推进的区域，去掉换向时无必要的整块清空；等待 RX-ready 使用有界截止时间，检测到就绪立即返回。
- 增加 ELF 可定位的 `g_webconfig_startup`。独占对齐缓存行并主动 clean，失败清理不覆盖启动原因。只记录启动控制帧，不记录密钥或业务密文。

## 当前状态与证据目录

排查仍在进行，不能宣称浏览器连接或高速链路验收完成。此前 A 槽写入时 SWD 通信中断，用户普通重上电后，已使用原入口恢复原事务，完成全部回读校验和 metadata 最后提交。内部 Flash 镜像一致，跳过擦写。TX 更新已返回 APPLIED、COMPLETE、100%。

本地日志和只读采集结果位于 `.hbox/webhid-hs/`；修改前的可恢复产物在 `.hbox/webhid-hs/connection-debug/pre-fix/`。已通过 V2 桥接、CRC/窗口、DMA 地址边界、DMA 环形回绕和旋钮退出的定向主机检查（7/7，1.8 秒）。完整无锁 manifest 构建成功；最终浏览器连接和旋钮退出仍待验证。相关刷写日志为 `release-ready-stm-resume.log` 和 `release-ready-tx-flash.log`。

首次通过高速就绪检查的 STM32 应用 SHA-256 为 `66125b2ef5f795e4be2e57676fb55681cc34f239705097af44b6f9741c2d049d`；TX 打包镜像 SHA-256 为 `0c8cb18c14abfd164461c75adfe5f73424c6b36efb0fdb1383268c2a02093a2c`。TX 打包镜像包含历史 IAP 区，但更新器仅写入其 Application 部分。

最终旋钮退出修复已通过普通 A 槽刷写和回读，STM32 应用 SHA-256 为 `2afbdb80b44c76d6c47aa20cddc44646eb84e11b93ff266d16d8c4e19cc382ee`，日志为 `knob-final-build.log` / `knob-final-flash.log`。随后只读检查仍为 Ready / HS Committed，Feature 返回 HS、1024 字节、15 MHz、bridge_ready=1、fault=0、epoch=2。

用户随后提供安全连接阶段超时截图。网页 `connect()` 使用 `getDevices()` 重连已授权的唯一设备，只有 `requestPermissionAndConnect()` 使用 `requestDevice()` 弹出选择器，所以未出现选择器本身不是枚举失败证据。最新启动后的 STM32 链路元数据快照显示只完成空探测，配置报告计数为零；该普通 RAM 结构没有主动缓存发布，不能仅凭 SWD 快照判定请求丢失位置。已请求使用同源 `/webhid-trace/` 页面观察下一次真实浏览器重连的命令名及 RX 是否出现，浏览器超时根因仍待定位。

根据用户再次反馈，手动“重新连接设备”按钮已改为始终调用 `connectDevice()`，通过现有 client/transport/lease 进入 `requestDevice()`。之前按钮仅在权限错误后弹出选择器，超时错误会反复使用已有授权，用户无法在该按钮上重新选择设备。页面加载、设备重新出现和固件更新后的自动重连仍不请求选择器。此修改提供明确的手动选设备入口，尚不能证明安全会话超时已解决。

用户确认选择器恢复后，安全连接仍超时。该次运行的 STM32 链路快照出现 `failed=1 / protocol_errors=6 / rx_accepted=0`，TX Feature 仍为 ready。接收块头为 `5b02200402000000080000000600000000000000080000000180000075cb8fe0`：长度 1056、epoch=2、块序号 8，保留字节 25 为 `0x80`。这把后续调查集中到 SPI 收包完整性，但普通缓存快照不足以证明首次故障原因。

已增加对齐并主动 clean 的 `g_webhid_link_fault[32]`，只保留首个拒收块的头、链路计数及计算 CRC，不保存报告载荷或密钥。同时把当前对照构建的 SPI 档位设为受支持的 7.5 MHz，USB 仍为 HS / 1024 字节。协议定向检查 3/3 通过；构建首次因临时 PATH 选择了缺少 requests 的 Python 而未启动，恢复标准环境后完整无锁构建通过（51.7 秒）。A 槽普通烧录和回读通过，metadata 最后提交，内部 Flash 一致而跳过写入；应用 SHA-256 为 `6261b3e8ec3057d2da238aa96720f3ee7a2b2621dff2ba0013d309a9c5b9eb9f`。日志为 `spi-7m5-host.log`、`spi-7m5-build-fixed-env.log`、`spi-7m5-flash.log`。烧录后 Feature 确认 7.5 MHz / bridge_ready=1 / fault=0；等待浏览器重连对照结果，尚未据此选定最终 SPI 档位。

没有修改烧录工具或冻结哈希，没有刷写 bootloader 或 CH585 IAP，没有执行保护状态操作，没有恢复 RF 自动回归或采样。

## 7.5 MHz 浏览器对照后的进展

用户截图显示安全连接已通过，停在检查配置版本后失败。该次 STM32 首错诊断未触发，链路记录接收 2 个报告、发送 5 个报告，CRC/协议错误均为零；TX Feature 为 bridge_ready=0 / fault=3。不能将这一结果解释为 SPI 已稳定：故障已出现在对端。

为分类 TX 首错，Feature 的既有 fault 字节增加 CRC、长度/版本、序号/代次/窗口及原始端口状态分类，保持报告布局和保留字节不变。后续错误不会覆盖首因，新的 PREPARE/STOP 清除。定向桥接/协议测试 3/3、网页 V2 测试 2/2、TX 编译和 hosted 构建通过。`hbox build tx` 不是支持的入口（解析阶段退出，无写入），改用规定的 `make -C RF_PHY_Hop/TX -j4` 成功。

经 `python tools/hbox.py flash tx` 更新，镜像 SHA-256 为 `b27575073118ee002593dca4e2e3cc2c5bd30dba2629e2206a3e1ce3db15114a`，108916 字节；前 4KB IAP 内容与原产物一致且未写入。既有更新流程返回 APPLIED / COMPLETE / 100%，日志 `tx-fault-flash.log`。随后普通复位，Feature 再次确认 HS / 1024 / 7.5 MHz / ready=1 / fault=0，等待下一次浏览器连接记录。浏览器控制工具返回 `nodeRepl.fetch request failed`，无法代用户操作选择器；未把原生 HID 能力查询算作浏览器端到端验收。

用户回复“已试”后，TX 首错为 **5：块长度/版本不合法**，STM32 仍无拒收首错。检查 TX 环形 DMA 收包发现：原代码把 CNT_END 与 DMA_END 合并为回绕标志，并且在读取标志与 NOW 之间没有检查新增回绕。已改为仅用 DMA_END 推进环形代次，在 NOW 采样后重新检查回绕，使用单调已生产/已消费字节数计算差值；差值超过 DMA 容量时明确报溢出，不发布被覆盖数据。新增边界测试覆盖采样中回绕、同位置整圈、重复读取和无符号计数器回绕，定向检查 3/3 通过，TX 编译及 RAM/map 检查通过（RAM 121108/131072 字节）。日志 `dma-wrap-host.log` / `dma-wrap-tx-build.log`。这处修复是否消除实机首错仍需浏览器对照，不能仅据源码推断根因已闭环。

DMA 回绕修复版 TX 经既有入口烧录成功，APPLIED / COMPLETE / 100%，应用写入进度 104824 字节；打包镜像 SHA-256 为 `d94af605c19a177bf9b449b4427c20fc9a8e5bfd544e1846d19e976c5dbc4b36`，日志 `dma-wrap-tx-flash.log`。已执行普通复位，保持 STM32 7.5 MHz 对照版本。

下一次浏览器重连仍失败：TX fault=0x19（端口 QUEUE_FULL），epoch=2；STM32 首个块拒收诊断仍为空，但链路已失效。尚不能判定是硬件 FIFO、DMA 覆盖还是软件环容量问题，因此补充端口首次溢出的 detail/produced/consumed，通过既有 32 字节只读 Feature 的最后 12 字节发布，正常就绪时仍全零。定义和各偏移见协议文档；只发布来源、硬件状态和计数，不包含报告内容。首因不被覆盖的主机测试通过，TX 构建通过，RAM 121124/131072 字节。诊断版 SHA-256 `2a31791fa9189328da04d2ce85bf99a9bf42b73cee096a41fa7f706ffa6a9189`，109220 字节，`port-detail-flash.log` 记录 APPLIED / COMPLETE / 100%；普通复位后 Feature 为 ready=1 / fault=0 / 7.5 MHz。等待浏览器再次触发，问题仍未解决。

端口诊断取得明确计数：detail=0x00008302（来源 2，SPI flags=0x83，FIFO count=0，TX armed=0），produced=164、consumed=3200。生产计数倒退导致无符号差值下溢，并非软件环真正占满。RX SPI ISR 的通用分支清除了 DMA_END 却未累计环形代次；仅改善主循环采样不足以保留已被 ISR 消费的标志。已为 RX 开启 DMA_END 中断，ISR 在确认该标志时累计代次；RX 分支不再通用清除 DMA_END。主循环/NSS ISR 的短快照区屏蔽全部中断，避免 SPI ISR 在读取代次和 NOW 之间再次推进代次。测试加入该现场数值：4092+164-3200=1056 字节，且保留前述跨圈和重读边界。定向测试及 TX 构建通过，日志 `dma-irq-host.log` / `dma-irq-build.log`；实机结果仍待验证。

上述 IRQ 交接修复经普通 TX 更新完成，`dma-irq-flash.log` 记录 APPLIED / COMPLETE / 100%。打包镜像 109348 字节，SHA-256 `5e1a8fbb6c3039da51f4b840ad8909a4a90dab2e58bb53cd367df9cdd538b46d`；IAP 前缀保持一致且未刷写。已普通复位，等待浏览器连接验收。

## LED / 氛围灯设置后断开（继续调查）

用户确认 DMA IRQ 交接修复后可以进入 WebConfig；后续“开启氛围灯”使右上角同步持续等待，最终断开，网页本身仍可操作。首次只读采集：TX HS / 1024 / 7.5 MHz / ready=1 / fault=0；STM32 链路 tx_produced=tx_sent=tx_acked=115，rx_accepted=rx_released=60，CRC/协议错误为零，块首错记录为空。CPU 三次 PC 分别位于 WebConfigState::tick、WebConfigLedsManager::update、BoardModeManager::readRaw；CFSR/HFSR=0，未见 HardFault。板链路对象显示 ResetRequested、resetAttempts=3，加密会话已清空。不能据此认定灯效驱动死锁，也不能确认 reset 的首因。

增加 `g_webhid_session_fault[32]`，对齐 32 字节并主动清理 DCache，仅锁存首个拒收原因。前八个 u32 为 magic=0x57534632、tick、reason、report sequence、previous sequence、detail(report type 或 RX 队列深度)、command class、transaction ID；余下保留。reason 1=头、2=填充、3=序号、4=会话加密状态、5=代次、6=GCM、7=明文类型/tag、8=请求类型、9=逻辑消息/命令失败、10=接收队列已满。command class 1=push_leds_config、2=update_profile、3=session.end、4=其他。没有报告载荷、配置内容或密钥；普通复位清空记录。读取脚本 `.hbox/webhid-hs/read_session_fault.py` 根据当前 ELF 查地址，使用最小 Cortex-M SRAM 读取，不 halt。

完整无锁 A 槽构建成功（58.3 秒，`led-session-build.log`）；外层临时 runner 在打印日志时遇到 GBK 编码异常，底层构建退出码为 0，产物和 manifest 已另行核实。`python tools/hbox.py flash app A` 烧录并物理回读通过，metadata 最后提交、内部 Flash 完全一致而跳过写入；应用 370812 字节，SHA-256 `160d1e00929c12ce733209cf85a2daa465b4c69b5d20dd1221856362e713d69f`，日志 `led-session-flash.log`。TX 和网页未部署变化。烧录后 session fault 全零，Feature ready=1 / fault=0。等待网页再次触发读取首因，尚未修复或验收 LED 设置断开问题。

未修改任何保护位或锁定状态。

## LED 命令触发的四报告并发故障与修复

浏览器再次触发后，首次会话失败为 reason=6（GCM 调用失败），sequence=15 / previous=14，上一命令类为 push_leds_config、transaction=13；SPI CRC 和块协议首错均为零。只读检查残留报告头显示载荷 996 字节、fragmented 标志。新增临时主机对照用公开测试密钥生成满载 996 字节双向向量，Node AES-GCM 与固件公共 C/mbedTLS 加解密及篡改拒绝检查通过（`full-crypto.log`），未改变黄金向量基线。

为避免反复要求用户操作，使用已安装 node-hid 做独立硬件定位（不是浏览器验收，也不是性能成绩）。新建短期直连加密会话，密钥仅在进程内存中；只读 get_global_config 的 JSON 尾部增加合法空白，覆盖 128/512/995/996/1200/1992/3984/12000 字节。逐包发送全部成功；开启临时氛围灯预览并启动按键监测后仍成功，无配置 Flash 写入。随后通过 Win32 overlapped WriteFile 同时提交最多四个报告，复现 3984 字节请求失败：一次 sequence=13 GCM 失败，另一次 sequence=15 / previous=13 丢失序号 14。原始脚本仅记录帧摘要，不记录密钥；第二次 SHA 分块取证准备期间先捕获到序号缺失，未进行完整发送/接收摘要比对。日志 `native-lengths.log`、`native-led-lengths.log`、`native-led-monitor.log`、`native-parallel.log`、`native-parallel-hash.log`。

TX USB OUT 主循环每移走一项软件队列数据，原来都会在屏蔽 USB IRQ 后对 EP2 RX_CTRL 做 ACK 的读改写。CPU IRQ 屏蔽不能阻止 SIE 收包；若寄存器读取时 DONE=0、写回前完成新包，旧值会清掉新 DONE，导致后续 DMA 覆盖尚未交付的数据。修改为仅当控制寄存器已为 NAK 且 DONE=0，并满足 HS/桥就绪时恢复 ACK；已启用的 ACK 端点由 ISR 管理，不再被主循环重复改写。协议格式、报告长度、窗口、SPI 时钟均未降级。

TX 构建通过（1.8 秒，FLASH 105276 / RAM 121124 字节），三项端点定向源码契约通过；已有 RWX LOAD 链接警告仍在。既有入口 `python tools/hbox.py flash tx` 返回 APPLIED / COMPLETE / 100%；应用打包 109372 字节，SHA-256 `afec2b25148096ee8f68a2d0cff080035192550d6534f4630f415a7cab8a0421`，IAP 前缀与上一产物一致且未刷写。日志 `usb-out-rearm-build.log`、`usb-out-rearm-contract.log`、`usb-out-rearm-flash.log`。烧录后普通复位，再做同一四包并发测试，所有长度通过；随后同一会话连续 10 轮 3984/12000 字节查询，共 20 次请求全部成功，临时氛围灯预览和按键监测同时开启。ST-LINK 结束后读取会话/块首错均为零，rx_accepted=rx_released=201，CRC/协议错误/重复块均为零。日志 `native-parallel-fixed.log`、`native-parallel-repeat.log`。

以上原生硬件对照完成后，用户确认“可以了”，灯光配置断开问题已获得浏览器使用确认。这些短请求结果仍不计为原高速吞吐计划的验收。未修改任何保护位或锁定状态。

## 2026-09-27 再次连接失败：高速能力查询未完成

本轮现场 Windows 仍枚举 VID CAFE / PID 4021，输入/输出 HID 长度 1025（含 Windows 的 report ID 占位），Feature 33。只读 Feature 返回 HS、1024 字节、bridgeReady=0、fault=1、spiHz=0、epoch=0；STM32 主动发布的启动记录为 stage=6、HS stage=0x41，双方链路尚未建立，块首错为空。说明失败发生在高速能力查询阶段，早于 PREPARE 和浏览器加密会话。保留现场后一次普通复位即恢复 ready=1，不能据此把偶发问题当作解决。

修复 `usb_board_link.cpp`：

- 对只读 HS_CAPS 查询的传输失败、NOT_READY、BUSY 最多尝试三次；有效但不兼容的能力及明确拒绝仍立即失败，不切换 SPI、不启用旧配置协议。
- 通用控制事务在发送前排空和发送后等待两个阶段均核对 opcode/transaction；不同事务的迟到回复继续排空，不再提前返回并阻止新请求发送。该缺陷由生产函数主机对照复现；原现场没有保留 HS_CAPS 回复细节，不能断言这次首次失败一定由旧回复引起。
- 将 HS_CAPS 收发结果纳入已有启动诊断；若在该阶段最终失败，保留响应状态和事务编号。后续成功的 PREPARE 会复用这些诊断槽位。

验证：`python -m unittest tools.tests.test_webhid_startup tools.tests.test_webhid_fast_link` 的定向五项通过；测试使用实际生产函数，覆盖发送前/后旧回复、事务编号回绕、超时、有界重试、协议不匹配和角色拒绝。相同两项新增测试应用到修改前函数均正常报告断言失败。第一次测试收集误带入既有 LED fixture，其缺少 `LedStripController.stop` 的编译桩而失败；随后修正测试模块的导入方式，只收集本次目标，没有修改 LED 测试或生产代码。最终新增两项再次通过。

完整无锁 A 槽构建 61.4 秒退出 0；临时日志包装器打印 GBK 无法表示的字符时报错，底层构建完成及 manifest 另行核实。`python tools/hbox.py flash app A` 退出 0，371260 字节，SHA-256 `70819400680a82e8eaab00e1254f8bb7eda8852ef797997b2b8f69f39adafb59`，目标 0x90000000，物理回读通过、metadata 最后提交，内部 Flash 一致而跳过擦写。没有更新 TX、网页或 bootloader。

刷写启动及随后两次普通复位均为 HS / 1024 / 7.5 MHz / ready=1 / fault=0。后两次控制事务编号比首次多一次，说明存在额外尝试，但现有最终快照不足以单独区分 CAPS 或 PREPARE 的重试。原生 HID 建立加密会话后，128、512、995、996、1200、1992 字节的只读 get_global_config 请求全部应答成功，随后普通复位清理诊断会话。日志为 `.hbox/webhid-hs/startup-host-final.log`、`startup-host-final2.log`、`startup-before.log`、`startup-build.log`、`startup-flash.log`、`startup-encrypted-read.log`。这些不是带宽验收；浏览器控制返回 `nodeRepl.fetch request failed`，已请求用户确认网页连接。USB 普通输入的问题尚未继续实机定位，RF 回归/采样未运行。

未修改任何保护位或锁定状态。
