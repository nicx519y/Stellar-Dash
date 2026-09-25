# XORA WebConfig 模块缓存

WebConfig 在加密直连会话建立后核对设备配置版本，再进入配置页面。连接浮层在核对、补读、固件信息和设备布局加载完成前保持阻塞。缓存只优化传输，不改变设备保存确认或草稿恢复语义。

## 设备接口

`get_config_manifest` 属于 `config.read`，响应 `data` 包含：

```json
{
  "deviceCacheKey": "<64 lowercase hex characters>",
  "hardwareVersion": "2.0.0",
  "schemaVersion": 1,
  "modules": {
    "global": "<SHA-256 hex>",
    "screen-control": "<SHA-256 hex>",
    "hotkeys": "<SHA-256 hex>",
    "profile-list": "<SHA-256 hex>",
    "profile:<id>": "<SHA-256 hex>",
    "macros:<id>": "<SHA-256 hex>"
  }
}
```

每个指纹为 32 字节，在线 JSON 中编码为 64 个小写十六进制字符。当前 16 个固定 Profile 对应 36 个模块。`selected-profile` 从 `profile-list.defaultId` 派生。

设备通过现有 GET 处理器逐个构造模块，对模块正文的 `cJSON_PrintUnformatted` 输出计算 SHA-256，随后释放临时对象。不对包含 padding 的配置结构体做哈希，不增加 Flash 写入或版本计数。全局配置摘要包含已有读取响应中的物理模式、校准与能力状态。

GET 以及包含完整配置正文的成功写入响应增加 `configVersions` 映射；摘要和正文来自同一响应对象。`get_profile_list` 支持 `{"listOnly":true}`，省略旧响应附带的 `defaultProfileDetails`；默认行为不变。宏单独计算，不混入 Profile 详情摘要。

`deviceCacheKey = SHA256(UTF8("XORA/config-cache/v1") || UID)`，UID 为三个普通 silicon UID 字，各自按 little-endian 编码。此键只用于本地缓存/草稿隔离，不是认证凭证，也不涉及 Option Bytes 或受保护身份记录。

## 浏览器行为

入口：[增量读取](../application/www/lib/device-transport/config-sync.ts)、[缓存](../application/www/lib/device-transport/config-cache.ts)、[模块解码](../application/www/lib/device-transport/config-modules.ts)。

IndexedDB 数据库 `xora-config-cache` 的 `snapshots` store 按以下数组的 JSON 字符串分区：

`[transportKind, deviceCacheKey, hardwareVersion, schemaVersion, cacheFormat]`

每个模块包含 `{ version, data, checksum }`。`data` 是设备原始正文，`version` 是设备生成的指纹，`checksum` 是浏览器对缓存正文计算的完整性校验。浏览器不重算设备指纹；C++ 与 JS 的浮点数表示和 JSON 重序列化不需要逐字节相同。

同步步骤：

1. 读取清单，加载对应缓存，验证本地完整性及数据结构。
2. 按模块比较指纹；仅补读缺失、损坏或版本不同的正文。需要读取列表时优先处理列表。
3. 再次读取清单，确认所有模块仍匹配、模块集合与 Profile 列表一致。变化时最多追加两轮补读；持续变化、断线、取消或启动超时均不发布部分配置。
4. 全部初始化阶段完成后一次性填充会话配置，再激活缓存快照。缓存写入使用单个 IndexedDB 事务，丢弃不存在的旧模块。

保存前失效可能受影响的缓存；只有具有配套版本的完整成功回包才能更新缓存。无 ACK、部分回包及更新单条宏等场景下保留失效状态。导入完成命令使整份缓存失效。未保存草稿、预览与结果未知的写入不会被当作已确认配置。

存储操作上限为 1 秒，存储禁用、配额不足和事务失败均降级为缓存未命中，不阻断设备使用。异步写入绑定当前缓存会话，断线后取消，防止旧回包覆盖新会话。

仅明确、关联到 `get_config_manifest` 的 `Unknown command`（404 / -1）响应允许回退旧版全量读取。超时、权限错误或清单损坏仍报错，不绕过同步检查。

配置输出语义发生不兼容变更时提升固件 `schemaVersion` 与前端支持版本；前端缓存转换规则变更时提升 `CONFIG_CACHE_FORMAT`。普通固件更新只要语义兼容，仍可按模块摘要复用。

## 定向验证

网页目录执行：

```text
npm run typecheck
node -r sucrase/register/ts-legacy-module-interop --test tests/config-sync.test.cjs tests/config-autosave.test.cjs tests/deferred-config-coordinator.test.cjs tests/device-request-queue.test.cjs tests/connection-presentation.test.cjs tests/mock-device-transport.test.cjs
```

仓库根目录执行：

```text
python -m unittest tools.tests.test_device_command_handler_contract
python -m unittest tools.tests.test_webhid_command_manifest.WebHidCommandManifestTests.test_config_manifest_is_registered_as_read_only
make -C application HBOX_SECURE_BOOT_REQUIRED=0
```

安全宏未知时使用独立 `BUILD_DIR`，避免复用其他安全模式的对象。

2026-09-26 Mock 配置同步测量（不含会话建立、固件信息和布局）：

| 场景 | 请求数 | 响应 JSON 正文字节 |
|---|---:|---:|
| 首次，无缓存 | 38 | 55,480 |
| 全部命中 | 2 | 6,536 |
| 仅屏幕模块变化 | 3 | 7,375 |

这些数字不包含 HID 加密、分包开销，不代表实机耗时。真实设备端摘要耗时与重连速度仍需实机验收。本功能不要求刷写 bootloader、CH585 或更改硬件保护状态。

本次验证：网页类型检查、108 项定向前端测试、真实 C++ 命令处理器与摘要契约、独立目录下的无锁 STM32 编译通过。Playwright Mock 预览确认 IndexedDB 保存 36 个模块，刷新后读取缓存，版本核对期间保留阻塞浮层，完成后开放页面。

扩大检查发现的已有失败单独保留：命令清单/权限检查有 5 项失败，在 HEAD 原始版本上复现，涉及 RF 绑定命令登记及已移除的旧图片 stream 断言；`webhid-protocol.test.cjs` 有 1 项失败，要求布局直接显示原始设备错误文案，该测试及对应布局与 HEAD 一致。本功能不修改这些无关契约，也不据此宣称全仓测试全绿。
