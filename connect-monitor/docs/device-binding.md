# XORA USB / RF 设备绑定

Gamepad Buttons 跟随顶部 USB / RF 来源。USB 自动匹配有线 TX（`CAFE:4024`），RF 自动匹配 RX（`045E:02FF`，兼容 `1A86:FE0C`）。角色规则集中在 `electron/sources/hid-device-selection.ts`。遥测接口还须通过 UMC1 / CTL1 应答或有效 RF 遥测确认能力；探测失败不推断为 USB。WebConfig `CAFE:4021` 始终排除。

点击 Gamepad Buttons 右上角来源状态可以分别选择游戏手柄和遥测设备。存在多个同角色设备时须明确配对，未确认前不自动派发配置。`045E:028E` 是通用旧 Xbox 标识，不能据此自动确认为 XORA，需要手动选择。使用“自动识别”清除该来源的手动绑定。

手动绑定保存在用户数据目录的 `device-bindings.json`。WGI 使用 `NonRoamableId` 识别手柄：重新枚举到相同身份才恢复；身份改变时重新选择。Windows 身份接口不可用或没有可读取的 WGI 手柄时，辅助程序列出 XInput 临时槽位供手动选择；槽位不保存到磁盘，观察到断开或连接代次改变后失效。按键显示独立于遥测、延迟开关。

## 运行与构建

- Windows 使用独立 C++/WinRT 辅助进程，通过标准输入输出交换选择及快照，约 60 Hz 更新按键；设备清单每 250 ms 刷新。主进程只读取缓存，不执行同步设备读取。
- `npm run build:electron` 同时执行 `build:gamepad`；需要 CMake 3.24+、Visual Studio C++ 桌面工具链和 Windows SDK。普通 VS 生成器不可用时，脚本通过已发现的同一套 vcvars 工具链使用 NMake，不修改系统 SDK 配置。
- 产物是 `dist/native/xora-gamepad-helper.exe`；安装包将它解包到 `app.asar.unpacked`，使用系统自带 Windows 输入 API。非 Windows 环境显示不支持，不自动猜测浏览器手柄。
- 升级后完整退出旧监视器，再从 `connect-monitor` 执行 `npm start`。按键窗口、主窗口和延迟窗口统一使用当前绑定。

切换或重新绑定会立即清空按键，旧绑定的异步结果被丢弃；一秒无新数据时清零。切换遥测物理设备会清空当前图表聚合及待发队列，历史日志保留。按键辅助进程不参与延迟测量，未修改原有延迟计算语义。

## 验证记录与人工验收

2026-09-27：类型检查通过（约 6 秒），Electron 构建及原生辅助程序增量编译通过（约 6.5 秒），renderer 构建通过（约 11 秒）。原生辅助程序首次成功构建约 11 秒；最初遇到 MSBuild SDK 识别失败，改用已安装的桌面工具链后解决。Vite 仍提示大于 500 kB 的 chunk。日志位于本地 `.native-build/`。

按现行暂停要求，未运行自动回归、设备采样、实机观察或烧录，未生成安装包。已准备 `tests/device-bindings.test.cjs` 并更新 `tests/native-gamepad.test.cjs`；这些用例尚未执行。构建成功不代表自动识别和实机按键已验收。

人工验收步骤：

1. RX 和有线 USB 同时插入，分别以 RX 先插、USB 先插两种顺序启动；切换顶部来源，确认各自按下、松开和组合键正确。
2. 按住按键拔出当前设备，确认清零；另一来源不能接管显示。重插原设备，确认按身份恢复。
3. 插入其他 Xbox 手柄或第二台同型号设备，确认歧义提示、手动配对和控制目标正确；WebConfig 不出现在候选中。
4. 关闭遥测与延迟开关，确认绑定按键继续响应；缺少监测能力时显示“遥测不可用”。
5. 快速切换 USB/RF、拔插和重新绑定，确认旧按键不残留，遥测图表、按键与延迟窗口使用同一来源。
