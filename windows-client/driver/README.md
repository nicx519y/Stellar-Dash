# HBox UMDF2 virtual gamepad backend (phase 2)

本目录固定正式驱动与用户态客户端之间的 ABI；它不是可分发的已签名驱动包。
公开发布还必须完成独立的 WDK 工程、Xbox 360/xinputhid 虚拟设备实现、INF、
HLK/Windows 10 与 11 x64 验证及 Microsoft 签名。

驱动必须暴露设备接口：

`{E54BDA55-57B6-4E32-A58B-48424F585631}`

只允许当前用户态 feeder 使用 `hbox_virtual_gamepad_ioctl.h` 的 V1 请求：

1. `IOCTL_HBOX_GAMEPAD_CREATE`：创建单个虚拟 Xbox 360 控制器并返回槽位。
2. `IOCTL_HBOX_GAMEPAD_UPDATE_STATE`：提交完整状态和源 stream sequence。
3. `IOCTL_HBOX_GAMEPAD_REMOVE`：先中立化，再移除子设备。

要求每个打开句柄最多拥有一个子设备，句柄关闭时自动中立化并清理；拒绝错误
magic/version/size；请求队列保持顺序；不实现输出报告或震动回传。正常应用
运行不能要求管理员权限，安装/升级驱动由安装包的提权阶段负责。

客户端在 `HBOX_INTERNAL_VIGEM_MVP=OFF` 时已经使用这个接口。不得把一个只接收
IOCTL、却未实际暴露可被 XInput 识别设备的占位驱动标为正式后端。
