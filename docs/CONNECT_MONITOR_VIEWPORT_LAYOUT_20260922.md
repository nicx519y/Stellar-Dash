# connect-monitor 视口布局（2026-09-22）

底部 RF Quality Packets、Channel Events、Channels 三张卡片现占满窗口内的剩余高度。移除底部区域的 560px 固定高度及页面级滚动，顶部图表在 420px 与剩余区域的 48% 之间取较小值，为底部卡片保留空间。底部卡片不显示滚动条；仍可用滚轮或触控板查看历史行。

仅修改 connect-monitor 界面，固件不变。TypeScript 类型检查及构建通过；按用户要求未运行回归、采集设备或烧录。重启监视器以加载新构建。
