# windows-client 协作规则

继承 [根目录规则](../AGENTS.md)。这是 Windows 高轮询率输入客户端：C++20/WinUSB 引擎 + WebView2/Next.js UI，与 Electron `connect-monitor` 是两个独立程序。使用细节见 [README](README.md)。

## 实现与行为边界

- [client_runtime.cpp](native/src/client_runtime.cpp) 管理接管/释放与虚拟手柄；[winusb_transport.cpp](native/src/winusb_transport.cpp) 负责接收，协议定义在 [hbox_high_rate_protocol.h](../common/hbox_high_rate_protocol.h)。保持 token、序号、CRC 和租约校验。
- 保留预分配 overlapped I/O、有界队列及注入线程所有权；不要在逐包路径引入动态分配或 UI 操作。
- WebConfig、RF24G 和非 INPUT 状态不属于此接管路径。退出必须中立化、移除虚拟控制器并释放租约；关闭窗口隐藏到托盘不等于退出。
- `producer_time_us` 是设备侧时间，没有与主机同步，不作为端到端延迟。
- 内部 MVP 动态使用 ViGEm；公开构建必须关闭 `HBOX_INTERNAL_VIGEM_MVP` 并启用 `HBOX_PUBLIC_RELEASE`。保留 [CMake 门禁](CMakeLists.txt)，不得将内部后端打包为正式产品。
- [driver](driver/README.md) 当前提供 IOCTL ABI 和交付要求，不是已签名可分发驱动。应用编译成功不证明虚拟 XInput 驱动、HLK 或签名已完成。
- UI 通过 [runtime.ts](ui/lib/runtime.ts) 和原生 host 交互；保持命令白名单和参数校验。修改协议同时检查原生端与 UI。
- 开发/构建不自动安装驱动、配置用户自启、接管真实设备或发布安装包。程序也不应自行下载/更新 ViGEm。

## 构建与验证

在本目录运行；首次桌面配置会获取 WebView2 SDK，UI 构建会执行 `npm ci`。

| 目的 | 命令 |
|---|---|
| Windows x64 桌面配置（VS 2022 环境） | `cmake -S . -B build -A x64` |
| 桌面构建 | `cmake --build build --config Release --parallel` |
| 仅 core 配置，避免桌面/UI 下载 | `cmake -S . -B build-core -DHBOX_BUILD_DESKTOP=OFF -DBUILD_TESTING=ON` |
| core 构建与测试 | `cmake --build build-core --config Release`；`ctest --test-dir build-core -C Release --output-on-failure` |
| UI 检查（在 ui 目录） | `npm run typecheck`；需要导出时 `npm run build` |

已有 build 目录的生成器/架构必须与命令兼容；不兼容时使用新的构建目录，不删除其他任务的产物。core 测试只覆盖相应主机逻辑，不替代 WinUSB、租约异常恢复、虚拟手柄或真实 2/4/8K 吞吐验收。

日常 core 逻辑修改优先使用 `HBOX_BUILD_DESKTOP=OFF` 的构建/测试；仅改 UI 先做 UI 检查；原生 host/WinUSB/WebView2 集成变化再做桌面构建。core 通过不能替代受影响的集成验证。测试使用 CTest 的 `--timeout` 按根目录耗时规则设置；不重复执行桌面构建来间接重装 UI 依赖，也不为普通修改生成安装包。
