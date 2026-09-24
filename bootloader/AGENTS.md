# bootloader 协作规则

继承 [根目录硬件安全与烧录规则](../AGENTS.md)。本目录负责 STM32H750 启动、QSPI 双槽校验和 application 跳转。

## 实现入口与边界

- 启动流程见 [main.c](Core/Src/main.c)，metadata、槽有效性与地址选择见 [dual_slot_manager.c](Core/Src/dual_slot_manager.c)。修改回退行为时分别核对无锁开发分支和安全构建分支，不把其中一条路径的行为泛化到另一条。
- 外部布局与 metadata 取自 [firmware_metadata.h](../common/firmware_metadata.h)，内部布局取自 [internal_flash_security_layout.h](../common/internal_flash_security_layout.h) 及 [linker script](STM32H750XBHx_FLASH.ld)。保持结构校验、向量表/栈地址检查与交接顺序。
- H750xB 的 bootloader、设备身份与版本日志共享内部 Flash 擦除扇区。不得用“重刷 bootloader”代替整扇区事务验证，也不能通过擦除身份或版本日志解决启动失败。
- 源码包含安全生命周期分支，不表示可在开发板上启用。保持 `HBOX_SECURE_BOOT_REQUIRED=0`；禁止为了通过校验而执行根目录禁止的保护位读取/操作、RSS secure-area 初始化或隐含解锁。
- 身份、签名和版本验证是软件契约；修改它们需检查 application、common、构建工具及 server 对应逻辑，不能以保持硬件无锁为由删除软件认证。

## 构建与验证

- 从仓库根目录仅编译：`make -C bootloader HBOX_SECURE_BOOT_REQUIRED=0 BUILD_DIR=build-unlocked-check`。切换安全宏或编译选项时使用独立目录或完整重编译，不能复用另一模式的旧对象文件。
- 完整开发产物使用根目录的 `python tools/hbox.py web local-build --unlocked-development`；单独编译的 ELF 不替代 manifest、槽位和镜像验证。
- [Makefile](Makefile) 的 `flash` 目标已禁用。不要恢复直接烧录，也不要执行其错误提示中提及的 production provisioning；实际刷写遵循根目录已验收入口。
- 改动启动/布局后检查 ELF/map 中入口、段范围和保留区域，按需要选择 [tools/tests](../tools/tests/) 中的相关主机契约检查。编译或静态检查不等于上电、回退或掉电恢复已通过实机验收。
