# XORA 架构整理与验证记录

日期：2026-10-14。起点为 `a7fd6f184d9717faa647b4c781062517c8209509`，本地工作分支为 `codex/architecture-optimization`。本次只做代码组织、主机验证和本地提交，不包含部署、推送、烧录或设备采样。

## 当前职责

| 范围 | 所有者与边界 |
|---|---|
| 命令作用域 | [清单](../tools/webhid_command_manifest.json) 是静态表来源；[生成器](../tools/generate_webhid_command_policy.py) 输出 C 与 TypeScript 表。RPC 处理器仍显式注册，测试独立核对注册、授权表与特例。 |
| 前端状态 | [gamepad-config](../application/www/contexts/gamepad-config/) 按会话、配置、校准、预览反馈和升级组织状态与操作；Context 发布已提交快照，消费者显式选择字段，稳定操作入口读取最新提交状态。 |
| 配置事务 | [configuration-transactions.ts](../application/www/contexts/gamepad-config/configuration-transactions.ts) 负责保存、串行边界和退出协调，测试直接调用生产实现。客户端、会话代次和物理队列保持单实例。 |
| 轴标定 | [switch-marking](../application/www/components/switch-marking/) 分离控制器与展示组件；管理员访问门禁仍在外层，未授权时不挂载控制器。 |
| WebHID 组包 | [WebHidLogicalAssembler](../application/Inc/webconfig/webhid_logical_assembler.hpp) 独占固定容量明文缓冲与分片状态，输出未完成、完成、超长或非法结果。 |
| WebHID 遥测 | [WebHidTelemetryState](../application/Inc/webconfig/webhid_telemetry_state.hpp) 独占按钮/edge 队列、采样和 checkpoint；服务保留授权、硬件读取、发送优先级与成功后的提交。 |
| USB 描述符 | [common/usb](../common/usb/) 保存共用的 XInput/Switch 字节定义；旧 STM32 头文件兼容转发，TX 直接引用公共路径。 |
| RX 绑定 | [rx_binding_mailbox.c](../RF_PHY_Hop/RX/APP/rx_binding_mailbox.c) 独占请求、回复和分页状态，通过时钟、处理与发送回调连接平台。持久化仍在主循环，ISR/ACK 快路径保留。 |
| 发布离线逻辑 | [release_support](../tools/release_support/) 管理 HEX、metadata、签名与包校验、包信息和服务请求参数；[release.py](../tools/release.py) 保留兼容导出、CLI、网络执行和硬件入口。 |

本次未迁移认证密钥、permit 状态机、Flash 地址布局或芯片保护逻辑，也未新增前端状态管理依赖。

## 静态生成与仓库缓存

在仓库根目录执行：

```sh
python tools/generate_webhid_command_policy.py
python tools/generate_webhid_command_policy.py --check
python tools/check_repository_artifacts.py
python -m unittest tools.tests.test_generated_webhid_command_policy tools.tests.test_webhid_command_manifest
```

网页目录也提供 `npm run check:command-policy`。生成结果提交入库，`--check` 只读，文件缺失或过期会失败；更新命令时需要同时维护处理器注册及行为测试。`ping`、`session.end`、已退休认证入口及 binary/stream 特例继续由 dispatcher 明确处理，未知命令仍拒绝。

已逐文件归档并取消跟踪 3,106 个生成缓存，共 766,979,246 字节（约 731 MiB）。范围是七个明确列出的历史构建目录中的 `.o/.d/.lst/.elf/.map`、工具链标记，以及散落的八个 RF 编译缓存、一个 TypeScript 增量缓存和一个 Playwright 报告。源码、脚本、说明、资源样本及 13 个历史 BIN/HEX 镜像保留跟踪；所有被取消跟踪的原磁盘文件也保留。

[归档清单](architecture-optimization-artifacts.json) 逐文件记录原路径、Git blob ID、大小与 SHA-256，并记录 ZIP 的 SHA-256。ZIP 位于仓库同级的 `HBox_Git-artifacts/architecture-optimization-a7fd6f18-20261013-235850/generated-caches.zip`；该路径以仓库父目录为基准，不要求固定盘符。归档逐项回读并与磁盘校验后才执行 `git rm --cached`。本地归档应随备份保留；原 Git blob 也仍存在于起点提交的历史中。本次不重写 Git 历史，因此现有 `.git` 对象库不会立即缩小。

忽略规则仅匹配明确输出目录/文件。检查器防止已识别缓存重新入库，不把第三方源码中名为 build 的目录当成生成物。

## 验证证据与边界

原始命令、PID、耗时与输出保存在本地 `.hbox/architecture-optimization/`。主机定向测试设置 120 秒真实超时，单次构建设置 600 秒；native 子阶段另有编译/执行限制。

| 检查 | 结果 |
|---|---|
| 命令清单、生成过期/缺失检查、实际 dispatcher lookup 编译执行 | 10 项通过；五组授权集合与起点一致 |
| 前端 WebHID 主机测试 | 194 项中 191 项通过、3 项已有文本断言失败；失败均有基线或起点源码对照 |
| 前端定向集成主机测试 | 182 项通过，约 14.2 秒；覆盖保存/队列、断线重连、旧响应、独占预览、升级、资源、路由和轴标定拆分 |
| 前端类型检查 | `npm run typecheck` 通过，最终约 4.6 秒；拆分视图 lint 与最终 10 项轴相关测试通过 |
| Hosted 构建 | `npm run build:hosted` 通过，约 38.3 秒；隔离校验覆盖 20 HTML、70 个引用 chunk、104 个产物 |
| Mock 浏览器交互 | 自动连接、管理员轴目录、添加轴、封面上传/裁剪保存、安装映射、曲线编辑、分步标定、全按键校准通过；平台修改确认后重载仍保留 |
| React 浏览器渲染计数 | 使用生产订阅模块在真实 Chrome/React 中通过：校准变化时 firmware 消费者计数保持 1，固件变化后变为 2；字段切换与稳定 action 读取最新实现通过 |
| release 定向测试 | 51 项通过；90 项原导出/常量/CLI 对照一致，16 项抽离函数/硬件入口 AST 对照一致 |
| 固件定向主机测试 | 13 项通过：真实组包与队列边界、背压下提前拒收、输出调度、C/C++ 描述符兼容、启动、配置写策略与结构契约 |
| STM32 无锁编译 | `make -C application HBOX_SECURE_BOOT_REQUIRED=0` 通过；最终增量约 12.8 秒 |
| TX / RX 编译 | `make -C RF_PHY_Hop/TX` 与 `python tools/hbox.py build rx` 均通过，各约 3.1 秒 |

STM32 起点链接已超出 RAM 32 字节；最终 `.text` 减少 32 字节，`.data/.bss` 大小不变，`.sleep_fault` 结束地址恰好为 `0x24080000`，AXI RAM **链接余量为 0**。没有改链接布局。TX 四个主要段大小和地址不变；RX `.text` 增加 168 字节，`.highcode/.data/.bss` 大小和地址不变，ISR 及 RAM 热路径未迁移。背压拒收判断保持在时间戳和 ADC 距离读取之前，新增测试编译实际服务方法并检查拒收时读取次数均为零。

订阅逻辑的主机用例已纳入 `npm run test:webhid`；实际渲染计数用 `npm run test:context-browser`，要求本地 Chrome，可独立执行。WebHID 服务对象仍位于 AXI RAM 的 `.bss`，大小保持 `0xcc60`，组包及遥测缓冲没有另行分配到堆上。

Mock 页面未出现运行时错误；favicon 404 和既有 Chart.js Filler 提示已记录。浏览器检查使用专用本地 Mock 服务，结束后关闭了本任务的浏览器与开发服务进程。截图保存在本地 `output/playwright/architecture-axis-editor.png`，不作为产品资源入库。

首次 hosted 构建因拆分视图中的未使用解构字段而未通过 lint；删除这些字段后，最终类型检查、lint、轴测试及 hosted 构建均通过，未修改检查门禁。最终构建进程返回 0，随后外层 Python 打印带勾号的日志时遇到 Windows GBK 编码错误；该错误发生在构建和隔离检查完成之后，原始构建日志及子进程结果均保留。

较广的既有固件 WebHID 检查在起点与重构后存在相同 6 项失败及 1 项错误（旧 scope 文本断言、binary ACK、描述符断言和 security golden 生成器）。前端 3 项旧文本断言分别涉及加载错误文案、图片目录 effect，以及 layout 中已有的固件目录分支；最后一项为新增覆盖，已用起点源码核对同样不匹配旧断言。冻结契约的 7 项哈希差异也在起点已存在；本次冻结文件及契约保持起点字节，不更新哈希掩盖差异。上述通过项不表示全仓测试全绿。

未执行硬件烧录、USB 实机枚举、绑定实测或 RF 自动回归/采样。内存段和主机测试不能证明实机时序、8K 吞吐或更新流程验收。
