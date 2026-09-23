# WebConfig RX 接收器绑定（2026-09-23）

## 用户行为

HBox 物理开关置 USB，进入 WebConfig；HBox 和 RX 同时插在电脑上。在 Global Setting 左侧 Platform／平台卡片底部，点击“选择接收器”完成首次 WebHID 授权。只对已授权设备自动识别插拔；多台 RX 时需要手动选择。

区域显示 RX 设备标识、Connection Address 和配对状态。无绑定或两端不匹配时显示“配对”；保存并回读两端后显示“已配对”。刷新可恢复已提交状态或未完成事务。提交前提供取消候选入口；RX 已提交而 TX 未提交时只能继续完成。

**绑定不会修改 connectionMode/inputMode、退出 WebConfig、重启或启动 TX RF。只有用户主动拨动物理开关才进入无线模式。** “已配对”表示两端持久化记录一致，不代表当前 RF 已连接。

RX 设备标识沿用正常 MAC/芯片身份的角色散列。连接地址由 RX 分配，首次配对或换绑时变化；普通上电不变。地址相同之外还必须校验互为对端的身份、绑定代次和事务编号，两端均已提交且没有待处理候选才显示已配对。

## 存储与恢复

- Data Flash 仍使用 `0x6000/0x7000` 双 bank，Code Flash 与 IAP 布局不变。
- 新绑定使用 bond version 3、reserved=1；沿用原记录长度、校验和及 journal 格式。`pair_counter` 保存 RX 绑定代次，`bond_confirm32` 保存非零事务编号。旧 version 2 仍可读取并运行，UI 标记未按新方案配对。
- RX 的地址是设备种子与持久化 journal generation 的可逆映射，跳过无效/保留地址，推进同一个 generation；不允许 generation 回绕。未提交候选不会在空口生效。
- 每次请求带 `expectedRevision`，拒绝过期页面覆盖；已持久化操作可幂等重试。读取 Flash 失败必须返回错误，不能当作空 bank 并擦除。
- 顺序为 RX PREPARED → TX PREPARED → 回读 → RX COMMITTED → TX COMMITTED → 回读。RX 提交为旧绑定失效点；此后不自动回滚。
- 无论是回复丢失、浏览器刷新还是掉电，恢复依据均来自设备记录，不依赖浏览器保存事务。只写成功一端不能显示成功。
- 旧无线配对状态机不得提交、取消或启用网页候选；RF 启动仅加载网页已提交记录。存在网页候选时旧配对／解绑入口返回失败。
- 配对记录独立于普通配置，不参与配置备份、导入、导出。

当前为功能性独占配对，不提供抗主动伪造的加密身份认证。换绑后旧 TX 使用旧地址，RX 仅启用新地址。

## 接口

共享定义：`common/rf_binding_protocol.h`。所有多字节整数小端。

| 请求 | 功能 |
|---|---|
| GET_ACTIVE / GET_PENDING | 读取版本、能力、身份、当前记录／候选、持久化 revision |
| PREPARE | 准备候选；RX 分配地址，TX 接收 RX 的地址和绑定代次 |
| COMMIT | 以事务编号和预期 revision 提交本地候选 |
| ABORT | 取消候选；禁止取消已提交事务 |

请求 32 字节 `RBP1`，回复 48 字节 `RBS1`，带 FNV-1a 检查和、请求序号及明确状态码。该检查和用于传输完整性，不是密码学认证。

RX 使用已有 vendor HID 的 report ID 0：Feature Report 请求，两个 32 字节 `RBH1` Input Report 回复（每页 24 字节正文）。USB 中断只复制请求；主循环执行存储操作。现有监视器 GET_REPORT、描述符、遥测开关、XInput 接口保持原语义。回复不依赖遥测租约。换绑期间取消旧 RF 调度、清除旧输入并排队中立报告，再加载新提交记录。

STM32 提供 `get_rf_binding`（config.read）、`prepare_rf_binding` / `commit_rf_binding` / `abort_rf_binding`（device.control）。它通过现有 `USB_BOARD_CMD_USB_CONTROL` 新增的 `RF_BINDING=0x08` 子命令访问 TX。维护模式允许操作，RF/USB 输入角色均拒绝；无需初始化 TX RF 协议栈。配对专用管理超时 2 秒，原命令超时不变。

旧 TX 的 UNSUPPORTED 会明确反馈需要更新固件。RX 无回复时提示检查固件和占用情况，不将其视为未绑定。校准、固件更新和图片上传期间拒绝绑定操作。

网页有独立的 RX 客户端和事务协调器，不复用 HBox 的单设备连接槽，不合并进延迟配置写入。跨 HBox 断连／重连后中止当前操作，避免写入另一台设备。

## 构建与验证

产品默认 `RFH_TEST_FIXED_BOND_ENABLE=0`；固定地址仅可显式以 `EXTRA_DEFINES=-DRFH_TEST_FIXED_BOND_ENABLE=1` 生成独立调试产物。切换宏必须完整重编译。

已执行：

- TX、RX 独立输出目录完整构建；STM32 使用 `HBOX_SECURE_BOOT_REQUIRED=0` 构建。
- `python -m unittest tools.tests.test_rf_binding tools.tests.test_rf_link_reliability.RfBondJournalTests.test_native_power_cut_journal tools.tests.test_frozen_flash_contract`：5 项通过，涵盖断电/部分写入、幂等、旧记录、地址换绑、存储读取错误、维护模式门禁及冻结烧录文件。
- `npm run test:rf-binding`：19 项通过；`test:webhid`：145 项通过；`test:mock`：49 项通过。
- hosted 和 mock 前端构建及产物隔离检查通过。Mock 接收器为独立虚拟设备，不访问真实 USB 接收器；不进入 hosted 产物。
- Playwright 离线浏览器验证首次选择、读取、配对成功、刷新恢复、RX 已提交而 TX 未提交时继续完成，以及仍保持 USB 模式；同时检查中英文显示。页面截图在 `output/playwright/`。
- 额外运行既有 RF reliability 测试时，`test_rx_guide_button_matches_usb_mapping` 失败：该测试仍断言旧 A1→HOME 的源码文本，当前工作区已有共享键位映射修改。本次未修改该键位映射或为了通过测试回退它。

验证日志与独立固件构建目录归档到 `.hbox/webconfig-rx-binding-20260923/`。仓库既有编译警告不代表实机通过。

## 实机验收仍待用户执行

1. 更新匹配的 STM32/TX/RX，双 USB 连接，授权并首次配对；确认 WebConfig 不退出。
2. 分别重启两端，网页仍显示相同地址和已配对；拨 RF 开关后确认无线输入正常。
3. 每个准备／提交节点拔线、断电，重新连接后继续完成；RX 已提交阶段不得恢复旧绑定。
4. RX 从 HBox-A 换绑 HBox-B；A 使用旧地址不能恢复连接或影响输入。
5. 验证多 RX 选择、接收器拔出、固件不支持、监视器同时打开以及各报告率下的 XInput/RF 行为。

未自动烧录、未采集设备、未运行硬件回归。现有无锁烧录入口、冻结文件、IAP、安全门禁及深度 Standby 禁令保持不变；未修改任何保护位或锁定状态。
