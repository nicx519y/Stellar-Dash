# 固定 Profile 槽位

## 行为

- 配置槽位数量来自 `NUM_PROFILES` 和实际 `Config.profiles` 数组，当前为 16。
- 全部槽位始终启用，按存储下标排列。界面编号为 01–16，无创建、删除或重置入口。
- 所有设置页面共享无卡片的 Profile Select 列表。左栏宽 228px，分隔线与右栏同为 1px / `border`。
- 悬浮或键盘聚焦时显示重命名按钮，触屏常显；重命名不会切换当前配置。
- `get_profile_list.profileList.items[].slotIndex` 是只读、从零开始的存储下标。容量由 `maxNumProfiles` 返回；缺少完整下标的旧固件显示升级提示并禁用槽位操作。
- `create_profile` / `delete_profile` 保留命令入口，但返回错误且不写存储。

## 存储兼容

已有启用配置保留内容、ID、位置和当前选择。旧的未启用槽位以第一个 Profile 当前配置为默认内容并启用，尽可能保留有效且唯一的 ID，生成不冲突的默认名称；若第一个槽位也未启用，则先恢复其出厂配置。全新配置的其他槽位同样复制第一个 Profile 的默认内容，各自使用独立 ID 和名称。迁移结果通过已有配置日志保存；再次加载已迁移配置不会请求重复迁移写入。

配置版本 `0x1E` 升级至 `0x1F` 时，所有已启用槽位的按键、触发、灯效、宏和比赛模式设置也会一次性复制第一个槽位；每个槽位的 ID、名称和当前选中项保留。后续启动不重复覆盖，用户仍可分别编辑每个 Profile。

再次同步时把配置版本提升至 `0x20`。设备中保存的版本若为 `0x1F`，下次运行新固件会再复制一次第一个 Profile 的当前设置并保存 `0x20`；重复运行或重刷 `0x20` 不会再次覆盖。

默认名称统一为 `Profile-01` 到 `Profile-16`。配置版本 `0x20` 升级至 `0x21` 时，会一次性按槽位覆盖所有现存名称，包括自定义名称；Profile 的其他设置、ID 和当前选择保持不变。若从更早版本直接升级，先执行待完成的设置同步，再统一名称。保存为 `0x21` 后，重刷同版不会再次改名。

保持 Config 二进制结构、版本和 Flash 地址布局。导入按 ID 更新固定槽位，即使旧客户端传入 `replaceProfiles`，旧备份中未包含的槽位也保持原状；导出包含全部槽位。

## 验证结果

- `npm run typecheck`：通过。
- `npm run test:mock`：51/51 通过，包括旧模拟状态补齐、16 槽切换、非当前槽重命名、ID/下标不变和备份恢复。
- `python -m unittest tools.tests.test_config_journal_atomicity tools.tests.test_fixed_profile_slots tools.tests.test_device_command_handler_contract tools.tests.test_frozen_flash_contract`：12/12 通过。真实生产 dispatcher/handler 覆盖 69 项原有接口契约，另含固定槽位专项检查。
- `python tools/hbox.py web local-build --unlocked-development --skip-web`：通过。产物 manifest 为 `unlocked-development`，`requiresManualLifecycleProvisioning=false`。此命令验证固件；前端本次使用类型检查和模拟浏览器验证。
- 浏览器检查 Global、Keys、Buttons Performance、Lighting 的 16 行列表；确认分隔线、短窗口独立滚动、悬浮/键盘/触屏编辑入口、最后一槽切换、重复名称拦截、取消编辑和校准期间禁用。
- 页面预览位于 `application/www/output/playwright/fixed-profile-*.png`。浏览器使用模拟设备；未进行实机验收、未烧录，未修改任何保护位或锁定状态。

## 已有通用回归断言的差异

本次涉及的测试均已更新并通过。扩展运行另发现以下未改动测试与未改动生产代码之间的既有差异，保留原样：

- `npm run test:webhid`：144/145 通过；`production WebHID construction and navigator access remain centralized behind the leased transport` 仍匹配旧的 `app/layout.tsx` 中 `deviceError?.transportCode === 'device-busy'` 源码形态。
- `test_device_command_transport_contract` 中命令注册数量仍期望 68，当前源码为 73。
- 同一通用测试仍期望 `webhid_service.hpp` 中的 `responseScratch` 成员，当前源码已使用其他发送缓冲结构。

上述三个测试及其所断言的生产文件均未被本次改动修改。
