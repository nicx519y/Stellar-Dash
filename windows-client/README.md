# HBox Windows Client

Windows 10/11 x64 的 HBox 高轮询率输入客户端。设备保持原生 XInput 作为
1 kHz 兼容路径；当设备配置为 2/4/8 kHz 且高性能模式开启时，客户端通过
32 字节租约协议申请接管，接收 WinUSB 64 字节流，并更新一个虚拟 Xbox 360
控制器。

## 当前交付范围

- CH585 原生 XInput / HighRate WinUSB 双 presentation 与租约自动回退。
- C++20 WinUSB 接收引擎：16 个预分配 overlapped read、IOCP、无逐包分配、
  有界 SPSC 队列、积压合并、序号/CRC/token 校验及 50 Hz 统计快照。
- 内部 MVP `IVirtualGamepad` 后端：运行时动态加载 `ViGEmClient.dll`，所有
  ViGEm 调用只发生在注入线程。程序不会下载或更新 ViGEm。
- 正式 `hbox-umdf2` 应用适配器及稳定 IOCTL V1 契约。对应已签名驱动包是
  第二阶段 WDK 交付物；应用侧无需再改协议或 UI。
- Next.js 静态导出 + Chakra UI 监控界面、WebView2 虚拟域、系统托盘、
  PnP/休眠恢复、当前用户登录自启命令。

`connect-monitor` 没有被依赖或修改；WebConfig、RF24G 及非 INPUT 状态也不
进入这条接管路径。

## 构建

要求：Visual Studio 2022 C++、Windows 10/11 SDK、CMake 3.24+、Node.js 20+。
首次配置会从 NuGet 获取官方 WebView2 SDK，UI 目标会执行 `npm ci` 和静态导出。

```powershell
cd windows-client
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix package
```

本机若使用 Ninja，应先进入 VS x64 Developer Command Prompt，再运行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

内部 MVP 运行还需要在应用目录或系统 DLL 搜索目录提供兼容的
`ViGEmClient.dll`，并预先安装 ViGEmBus。仓库和安装脚本都不会联网下载它。

## 发布门禁

内部 MVP：

```powershell
cmake -S . -B build -DHBOX_INTERNAL_VIGEM_MVP=ON -DHBOX_PUBLIC_RELEASE=OFF
```

正式后端构建：

```powershell
cmake -S . -B build -DHBOX_INTERNAL_VIGEM_MVP=OFF -DHBOX_PUBLIC_RELEASE=ON
```

若公开构建仍启用 ViGEm，CMake 会直接失败。正式构建通过设备接口
`{E54BDA55-57B6-4E32-A58B-48424F585631}` 访问已安装的 HBox UMDF2 驱动；
驱动负责 `CREATE / UPDATE_STATE / REMOVE`，不实现震动回传。

## 运行与自启

- 关闭窗口：隐藏到托盘，输入继续运行。
- 托盘“退出并释放设备”或 UI“退出”：先中立化并移除虚拟控制器，再发送
  `RELEASE`。
- 强杀/USB 异常：CH585 在租约心跳丢失约 1 秒后自行恢复原生 XInput。
- 设置文件：`%LocalAppData%\HBox Client\settings.json`。
- 开启当前用户自启：`HBoxClient.exe --install-autostart`。
- 移除当前用户自启：`HBoxClient.exe --remove-autostart`。
- 自启后台运行参数：`HBoxClient.exe --background`。

## 关键接口

- 共享线协议：`../common/hbox_high_rate_protocol.h`
- 虚拟手柄抽象：`native/include/hbox_client/virtual_gamepad.hpp`
- 正式驱动契约：`driver/include/hbox_virtual_gamepad_ioctl.h`
- WebView2 命令：`runtime.getSnapshot`、
  `runtime.setHighPerformanceEnabled(bool)`、`window.hide`、`app.exit`
- WebView2 事件：`runtime.snapshot`（50 Hz）、`runtime.stateChanged`

`producer_time_us` 只用于设备侧间隔/抖动分析。它不与主机时钟同步，因此不
作为端到端延迟指标。
