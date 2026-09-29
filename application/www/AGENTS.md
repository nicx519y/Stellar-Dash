# WebConfig 协作规则

继承 [仓库规则](../../AGENTS.md) 与 [application 规则](../AGENTS.md)。本目录是 Next.js/React WebConfig；启动和部署细节按需查阅 [README](README.md)。

## 产品边界

- 网页产品名统一使用 **XORA**，包括中英文提示、连接浮层、帮助文案、页面标题和无障碍标签；命名与兼容性边界遵守[根目录产品命名规则](../../AGENTS.md#产品命名)。

- 产品为服务器托管静态站点，通过 CH585 Maintenance HID / WebHID 与 STM32 通信。Hosted 只用 WebHID，不允许自动回退到 Mock、HTTP 或其他设备通道。
- Mock 是独立开发变体。保持 `build:hosted` / `build:mock` 的环境与产物隔离，不将模拟设备打入产品。
- `makefsdata.js` 仅为旧 A/B artifact 兼容生成资源；日常网页构建不把网站写入 STM32，不恢复旧 httpd runtime。
- WebHID 设备选择需要用户手势。设备访问、会话租约、队列和错误处理使用 [device-transport](lib/device-transport/) 的现有抽象；组件不要自行打开另一条 HID 通道。
- Hosted WebConfig 通过 `session.open-direct` 建立加密 WebHID 会话，不要求设备身份、attestation 或服务端 permit。协议/命令调整同时核对 STM32 dispatcher、服务端资源权限和 Mock；用户登录与管理员权限继续独立生效。

## 配置交互

- 配置状态通过 [gamepad-config-context.tsx](contexts/gamepad-config-context.tsx) 和现有设备队列协调。持久化成功以设备应答为准，断线或拒绝不能显示为已保存。
- 普通自动保存保留按键监测与 LED 预览；校准、图片、导入和升级等独占操作走各自边界，见 [保存反馈说明](../../docs/webconfig-live-autosave-feedback.md)。
- Profile 列表依照设备返回的容量和 `slotIndex`，不恢复创建/删除入口；槽位迁移和兼容规则见 [固定 Profile 槽位](../../docs/fixed-profile-slots.md)。
- USB 网页配对只保存绑定，不启用 TX RF 或改变物理连接模式；部分提交遵守现有续作逻辑，见 [USB 配对说明](../../docs/WEBCONFIG_RX_BINDING_20260923.md)。
- 用户账户登录与管理员操作保持原有会话和来源校验；WebHID 设备直连不替代用户登录。

## 常用命令与验证

在本目录运行，脚本定义以 [package.json](package.json) 为准：

| 目的 | 命令 |
|---|---|
| 真实设备前端开发 | `npm run dev:hosted` |
| 无设备 UI 开发 | `npm run dev:mock` |
| 类型检查 | `npm run typecheck` |
| 协议/队列/保存流程 | `npm run test:webhid` |
| Mock / 路由 / 资源 | `npm run test:mock` / `npm run test:router` / `npm run test:resources` |
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
