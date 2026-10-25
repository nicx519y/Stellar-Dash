# WebConfig 协作规则

继承 [仓库规则](../../AGENTS.md) 与 [application 规则](../AGENTS.md)。本目录是 Next.js/React WebConfig；启动和部署细节按需查阅 [README](README.md)。

WebConfig 与 admin 的双域名上线、Hosted 打包、更新和回滚统一按 [服务端部署指南](../../docs/webconfig-admin-deployment.md) 执行。

## 产品边界

- 网页产品名统一使用 **XORA**，包括中英文提示、连接浮层、帮助文案、页面标题和无障碍标签；命名与兼容性边界遵守[根目录产品命名规则](../../AGENTS.md#产品命名)。
- 所有 UI 组件尽量使用 Chakra UI 的组件，优先复用本模块已有的 Chakra UI 封装；选择框、按钮、表单、弹窗等已有对应组件时，不另用裸 HTML 控件或自建样式替代。
- 全站排版使用紧凑等宽风格：英文、数字沿用系统等宽字体（SFMono-Regular / Consolas / Liberation Mono），中文使用随站点托管的更纱 Mono SC Webfont；字体与字号统一取自 `lib/ui-typography.ts` 和 `components/ui/theme.ts`，字体资源、许可证及更新说明见 `public/fonts/sarasa-mono-sc/README.md`。正文与控件以 12px 为基准，辅助信息 10–11px，标题按主题层级区分；中英文切换不覆盖正文字体，加载期间中文回退到无衬线字体。新增页面、弹窗和图表优先复用主题 token，Canvas 图表在 Webfont 加载后重绘；图标字体及设备屏幕预览的像素尺寸独立保留。
- 页面中任何面向用户的文案（含中英文、标题、选项、提示、错误信息和无障碍标签）不出现 STM32、CH585 等具体芯片型号，统一使用“主控 / Main controller”“TX”“RX”等功能代称。外部错误或兼容声明中的型号在展示层转换；协议字段、命令、设备匹配值、签名数据及原始诊断导出等技术标识保持兼容，不为文案更名修改原值。

- 产品为服务器托管静态站点，通过 CH585 Maintenance HID / WebHID 与 STM32 通信。Hosted 只用 WebHID，不允许自动回退到 Mock、HTTP 或其他设备通道。
- Mock 是独立开发变体。保持 `build:hosted` / `build:mock` 的环境与产物隔离，不将模拟设备打入产品。
- `makefsdata.js` 仅为旧 A/B artifact 兼容生成资源；日常网页构建不把网站写入 STM32，不恢复旧 httpd runtime。
- WebHID 设备选择需要用户手势。设备访问、会话租约、队列和错误处理使用 [device-transport](lib/device-transport/) 的现有抽象；组件不要自行打开另一条 HID 通道。
- Hosted WebConfig 通过 `session.open-direct` 建立加密 WebHID 会话，不要求设备身份、attestation 或服务端 permit。协议/命令调整同时核对 STM32 dispatcher、服务端资源权限和 Mock；用户登录与管理员权限继续独立生效。

## 配置交互

- 配置状态通过 [gamepad-config-context.tsx](contexts/gamepad-config-context.tsx) 和现有设备队列协调。持久化成功以设备应答为准，断线或拒绝不能显示为已保存。
- 普通自动保存保留按键监测与 LED 预览；校准、图片、导入和升级等独占操作走各自边界，见 [保存反馈说明](../../docs/webconfig-live-autosave-feedback.md)。
- Profile 列表依照设备返回的容量和 `slotIndex`，不恢复创建/删除入口；槽位迁移和兼容规则见 [固定 Profile 槽位](../../docs/fixed-profile-slots.md)。
- USB 网页配对只保存绑定，不启用 TX RF 或改变屏幕记忆的连接模式；普通保存和备份导入也保留该选择。`connectionMode` 表示记忆选择，`effectiveConnectionMode` 表示当前链路，平台限制依据前者；见 [两档 PCB 适配](../../docs/two-position-pcb-firmware.md)。配对部分提交遵守现有续作逻辑，见 [USB 配对说明](../../docs/WEBCONFIG_RX_BINDING_20260923.md)。
- 用户账户登录与管理员操作保持原有会话和来源校验；WebHID 设备直连不替代用户登录。
- “设备日志”向已连接用户开放，主控/TX 使用现有请求队列和分页接口，RX 使用 device-transport 中与绑定共享独占租约的接收器诊断通道；RX 读取须告知会临时暂停游戏输入并有 CLOSE/超时释放。每个来源手动开始读取或刷新；读取期间显示进度并锁定本页操作，离开页面须确认中断，完成后使用虚拟列表展示。清空必须经用户确认、限定所选来源并以设备擦除/回读应答为准，不能自动清空或以本地隐藏代替；电池测试进行中拒绝清空。待落盘、部分失败及不完整导出须可见，不持续轮询或强制落盘，协议和 Mock 场景见 [设备日志](../../docs/device-fault-logs.md)。

## 常用命令与验证

在本目录运行，脚本定义以 [package.json](package.json) 为准：

| 目的 | 命令 |
|---|---|
| 真实设备前端开发 | `npm run dev:hosted` |
| 无设备 UI 开发 | `npm run dev:mock` |
| 类型检查 | `npm run typecheck` |
| 协议/队列/保存流程 | `npm run test:webhid` |
| Mock / 路由 / 资源 | `npm run test:mock` / `npm run test:router` / `npm run test:resources` |
| 设备日志分页与导出 | `npm run test:logs` |
| 接收器绑定 | `npm run test:rf-binding`（不在默认 `npm test` 链中） |
| 产品构建及变体检查 | `npm run build:hosted` |
| Mock 构建及变体检查 | `npm run build:mock` |

只运行与改动有关的检查；UI 改动适当使用 Mock 预览核对操作状态与布局。全套 `npm test` 不包含独立配对测试，不能据此声称配对已验证。实机配对/升级与 RF 采样不能由 Mock 结果替代，也不因前端修改自动执行。

### 定向选择与停止

- 纯布局优先类型检查和受影响页面预览，不自动运行全部协议测试或构建 STM32/TX/RX。共享 context、队列、transport 修改则覆盖相关调用方和错误状态。
- 保存行为优先 `tests/config-autosave.test.cjs`、`tests/deferred-config-coordinator.test.cjs`；队列优先 `tests/device-request-queue.test.cjs`；绑定使用独立 `test:rf-binding`。按实际改动补选协议/Mock 用例，不把这份列表当固定全跑清单。
- 单文件命令示例：`node -r sucrase/register/ts-legacy-module-interop --test tests/config-autosave.test.cjs`。检查通过后没有相关新改动就不重复跑同一文件及包含它的全套脚本。
- `build:hosted` / `build:mock` 已通过 postbuild 执行对应变体校验；成功且产物未变时不再重复执行同一 verify。仅在改动构建/部署边界、需要静态产物或完整验收时扩大到产品构建；不默认连跑 hosted 和 mock 两次构建。
- 保留有效 TypeScript 增量缓存；依赖未变且环境正常时不重装 `node_modules`。同目录的构建/类型检查存在共享产物时串行执行。
