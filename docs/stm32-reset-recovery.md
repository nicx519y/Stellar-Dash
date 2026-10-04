# STM32 NRST 恢复连接修复（2026-09-25）

这是独立的主机烧录流程修复，不改变固件、Flash 布局、保护状态或冻结契约哈希。

原身份探测及 QSPI 回退只传入 `reset_config connect_assert_srst`，沿用默认的
`none` 复位线配置，产生 `BUG: can't assert SRST`。板上 NRST 已通过持续复位及
用户观察重启确认有效。

两个入口现在共用 `BuildTool._openocd_reset_recovery_commands()`：显式声明
`srst_only srst_nogate`，连接时拉低 NRST，延后目标检查，释放 NRST 后才检查并
暂停 CPU。仓库的 STM32H7 配置通过 HLA/AP0 访问 DBGMCU，复位保持期间 AXI
不可访问，因此不能只补 `srst_only` 后继续在复位中检查目标。

初始化、目标检查或暂停失败时仍尝试释放 NRST，保留阶段错误并停止；不会继续
执行调用方的目标身份检查或 Flash 操作。正常连接路径、目标绑定、镜像范围和
哈希检查、回读以及 metadata 最后提交顺序保留。QSPI 回退恢复后仍执行原有的
`reset init` 和目标身份检查，再进入原有写入/校验脚本。

主机验证入口（不访问硬件）：

```powershell
python -m unittest tools.tests.test_stm32_reset_recovery tools.tests.test_webconfig_flash tools.tests.test_qspi_chunked_flash tools.tests.test_flash_bootloader_unlocked tools.tests.test_development_bootloader_flash -v
python -m unittest tools.tests.test_frozen_flash_contract -v
```

新增测试实际执行生成的 Tcl，模拟初始化、复位释放、目标检查及暂停故障，验证
复位清理与停止后续阶段。既有测试覆盖目标校验、写入范围、提交顺序和回读。

实机只读验证中，SRST 配置错误已消失；当前设备仍无法完成 CPU 检查，因此未
进行 Flash 擦除/写入，完整烧录及重启验收尚未完成。不能把此修复描述为设备已
恢复下载，也不能据此认定原 CPU 连接故障的原因。

修改前冻结契约仅 `tools/hbox.py` 已有哈希差异；修改后新增 `tools/build.py`
与 `tools/webconfig_flash.py` 两项待验收差异。不得通过更新哈希消除失败；待
完整实机验收后再按项目规则更新契约。

## QSPI 回退脚本初始化顺序修复（2026-10-04）

本次作为独立烧录流程修复处理。NRST 回退在加载整文件烧录脚本前已执行
`init`，而该脚本再次设置 `gdb_port disabled`，OpenOCD 因配置命令只能在
初始化前执行而停止。现在将 GDB/Tcl/Telnet 端口配置移到公共命令前缀；正常
路径和回退路径都在初始化前配置，烧录脚本不再重复设置端口。

新增回归实际在 Tcl 模拟器中执行生成的命令与脚本，拒绝初始化后的端口配置。
修改前回退场景复现同类错误，修改后正常及回退场景通过。上方五个主机测试模块
合计 57 项通过，覆盖复位失败清理、目标绑定、写入范围、回读、事务恢复及
metadata 最后提交；这些测试不启动 OpenOCD，也不访问设备。

修改前已核实冻结哈希存在四项差异：`RF_PHY_Hop/TX/Makefile`、`tools/build.py`、
`tools/hbox.py`、`tools/webconfig_flash.py`。本次不更新冻结哈希；修复后的
`tools/build.py` 仍须实机验收。日志中的 DBGMCU `examine-end` 读取失败是否
仍会出现，尚未通过设备复测确认，不能据主机测试宣称已恢复烧录。

中断事务仍保留在 `.hbox/webconfig-local/flash-transactions/`。现有产物、签名和
事务摘要一致时，`python tools/hbox.py flash app A`（明确选用 B 槽时改为 B）
会经过目标及状态验证自动读取该事务的本地恢复令牌。恢复同一事务时不加
`--build`，不清理事务目录或重新生成产物；命令会实际写设备，不作为主机验证。
