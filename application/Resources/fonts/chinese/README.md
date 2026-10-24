# LCD 简体中文子集

主菜单末尾固定提供 `Language / 语言`，可选择 `English` 或 `简体中文`。
按下旋钮应用并通过已有延迟保存流程保存；返回项不会改变语言。
旧配置默认英文。语言使用 `ScreenControlConfig.reservedStyle[2]`，局部 schema
从 A2 升为 A3；配置结构仍为 68 字节，不改 Flash 布局。JSON 使用 `language`
的 `en` / `zh-CN` 值；普通补丁省略该字段保留当前语言，旧备份导入默认英文。
语言是固定系统菜单 ID 20，不占可隐藏功能的位图或排序槽位。

`screen_zh.json` 收录菜单、选项、说明、状态及弹窗翻译。品牌、协议缩写和
用户自定义名称保留原文。字库仅支持本目录列出的中文字符，不提供任意中文
输入；未收录的非 ASCII 字符显示 `?`。共享绘制路径按 UTF-8 字符计算宽度、
换行及裁切，避免截断一个汉字或覆盖侧栏。英文保持原有 Terminus 字模。

## 字体与生成

中文来自 Sarasa Mono SC 1.0.42 Regular / Bold，SIL OFL 1.1 授权见
[OFL.txt](OFL.txt)。原始压缩包来源与校验值见
[网页字体说明](../../../www/public/fonts/sarasa-mono-sc/README.md)。
这里只提交需要的 TTF 子集；完整 TTF 不进入仓库，也不编入固件。

- 12×12 Regular：辅助文字。
- 16×14 Regular：正文。
- 16×16 Bold：菜单及小标题。
- 24×24 Bold：说明页标题，使用原生 23px 字模，完整容纳标点下伸笔画。

每档字号的汉字及标点共用固定基线，不按单字包围盒重新居中，也不缩放位图。
生成时先在带留白的画布上绘制，再检查真实单色像素是否完整落在字框内；超界
直接报错。字体自身的字形高矮保留。基线与完整笔画由主机测试逐字验证。

从仓库根目录执行（Python Pillow；生成时使用 Pillow 12.1.1）：

```sh
python tools/generate_screen_chinese.py
python tools/generate_screen_chinese.py --check
```

增加翻译所需字符时，先用 fontTools（生成时使用 4.60.1）更新子集：

```sh
python tools/generate_screen_chinese.py --source-dir path/to/original-ttfs
```

输入目录须含 `SarasaMonoSC-Regular.ttf` 与 `SarasaMonoSC-Bold.ttf`。
一起提交 JSON、两个子集 TTF 和生成的 `screen_chinese_data.inc`。
普通固件构建直接使用该 C 数据，不需要字体工具或网络。

## 容量核对（2026-10-24）

无锁开发配置、Power Probe 开启并注入当前 trust header 的实际链接结果：

| 项目 | 字节 |
|---|---:|
| 202 条翻译、263 个字符 × 4 字号及索引（不含对齐） | 49,102 |
| 中英文字库 `.screen_fonts` 合计（含对齐） | 57,656 |
| CPU 专用菜单/电池状态常量 `.ui_rodata`（DTCM） | 1,564 |
| application.bin | 511,512 |
| 2 MiB 固件槽位剩余 | 1,585,640 |
| DTCM 堆容量（已扣除 8,192 字节栈预留） | 63,656 |
| AXI RAM 链接余量 | 1,184 |
| ITCM 链接余量 | 1,096 |

字库随固件保存，启动时走已有 `.screen_fonts` 复制路径进入 DTCM；绘制不分配
堆内存。屏幕清空测试记录新增文案后，菜单和电池状态常量通过独立 `.ui_rodata`
启动复制段放入 DTCM，列表绘制代码放入 ITCM；图标仍用原有独立绘制方式。
未移动 DMA 缓冲、缩小 32 KiB 最低堆或 8 KiB 栈。当前能放下，但代码余量仍较小；后续增加功能须重新
核对实际 ELF，不能仅看 Flash 剩余空间。上述堆容量是链接预算，不是实机峰值
用量测量，不代表烧录或实机验收。

验证入口：`test_screen_fonts`（全部字模像素与双语双主题页面）、
`test_screen_font_layout`（实际 ELF 中的完整表、加载地址、堆栈余量）、
`test_screen_power`（迁移、导航、语言保存与 JSON）、`test_sleep_charge_display`。
WebConfig 配置契约另见 `application/www/tests/screen-control-config.test.cjs`。
实机仍需核对小字清晰度、旋钮切换、延迟保存后断电重启和弹窗显示。
