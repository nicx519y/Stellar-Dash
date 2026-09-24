# tools 协作规则

继承 [根目录规则](../AGENTS.md)，尤其是硬件保护禁令、无锁模式和已验收烧录契约。本目录包含构建/烧录/发版工具，也包含会访问设备或远端服务的脚本。

## 工具边界

- 日常统一入口是 [hbox.py](hbox.py)。只读状态、构建、烧录和发布是不同操作；不得因为名字含 `build`、`test` 或 `verify` 就假定没有副作用，先核对实际调用路径。
- 无锁开发板单独重刷 STM32 bootloader：`python tools/hbox.py build bootloader` 生成带无锁 manifest 的整扇区产物；`python tools/hbox.py flash bootloader` 只烧录并回读该产物，`--build` 才先重新构建。它们调用 [flash_bootloader_unlocked.py](flash_bootloader_unlocked.py)，写入内部完整 128KiB sector 0 会清空设备身份和最低安全版本。低层 `build.py flash bootloader` 仍是拒绝入口。
- `hbox.py` 的上述分发变更待实机启动验收；冻结契约仍保留 2026-08-30 的旧 SHA-256，因此当前契约检查只会在 `tools/hbox.py` 项报告待验收差异。完成验收后再更新契约版本、日期和哈希。
- [冻结契约](frozen_flash_contract.json) 中的文件不能夹带修改，不能为通过测试直接更新哈希。若任务确需改变它们，作为独立烧录流程变更处理并重新验收。
- 完整 STM32 开发构建使用 `python tools/hbox.py web local-build --unlocked-development`；不要绕到默认 production 的旧构建示例。工具涉及产物模式时保持 manifest 检查，不能以命令成功代替镜像检查。
- [webconfig_flash.py](webconfig_flash.py) 中的目标绑定、地址/大小校验、回读及提交顺序必须保留。`--execute` / `--simple-execute` 是实际硬件操作，不用于“测试一下命令”。
- TX 普通更新仍走 `python tools/hbox.py flash tx`。IAP 安装、身份置备、保护状态转换与普通 Application 更新不可混为一谈；根目录禁止的操作直接拒绝。
- [release.py](release.py) 包含构建、刷写、上传、删除等不同子命令；不能把发版或服务端写操作作为编译检查的附带步骤。
- 密钥、令牌、设备身份与本地数据库不写进日志或测试 fixtures；修改认证/签名工具时使用测试材料，保留生产与本地环境边界。

## 验证与诊断

- 命令从仓库根目录执行。烧录契约检查：`python -m unittest tools.tests.test_frozen_flash_contract`，当前实现只做主机哈希和模拟调用。
- 其他测试按改动选择 [tests](tests/) 中的具体模块，先确认依赖和副作用。不要默认 discover 全部测试：RF/monitor 的暂停自动回归与采样要求仍适用，见 [RF 规则](../RF_PHY_Hop/AGENTS.md)。
- 定向示例：配置写入策略用 `python -m unittest tools.tests.test_webhid_config_write_policy`，配置日志用 `python -m unittest tools.tests.test_config_journal_atomicity`；选择与实际改动相关的模块，模型测试不替代生产实现或设备验证。
- 按根目录规则管理超时与进度。新增或修改主机测试 runner 时，分别约束编译和执行的超时，输出阶段/耗时并保留失败日志；已有 `subprocess.run(..., capture_output=True)` 不意味着实时进度或超时已实现。未经 runner 改动与检查，不宣称它已具备这些能力。
- 不因测试进程卡住修改冻结烧录脚本；先定位主机子进程、工具链或测试依赖。必须清理时只处理本任务进程与临时文件，不以全量重建/重试代替诊断。
- 跨端命令定义和 fixtures 见 [webhid_command_manifest.json](webhid_command_manifest.json)、[device_command_contract_cases.json](device_command_contract_cases.json)；修复行为后核对消费者，不以更新预期掩盖不兼容。
- [rf_link_report.py](rf_link_report.py)、[rf_ack_report.py](rf_ack_report.py)、[rf_short_report.py](rf_short_report.py) 用于已有日志分析；离线分析不应顺手启动新 HID reader 或设备采集。
- 操作 `.hbox` 临时目录前检查引用、唯一源码/产物及归档。目录联接可能指向仓库外 SDK；不得递归跟随联接删除。删除展开的构建目录不等于删除对应交付归档。
