# XORA 无摇杆 RGB 控制器变形版

完整的无摇杆控制器先展示约两秒，再由面板与按键翻转、伸展组成 XORA Logo。板面为黑色阳极氧化铝合金的细哑光外观；22 颗按键各有动态灯圈，板底最下层为整圈 3 mm 厚导光层，投射蓝紫色环境光。采用 Blender / Cycles 实体三维场景渲染。

## 布局依据

- WebConfig 的 `application/www/lib/device-transport/mock-device-transport.ts` 中 `HITBOX_LAYOUT` 提供全部 22 个位置和半径；`hitbox-constants.ts` 提供 787 × 489 板面与 2.55 坐标缩放，半径不缩放。
- 与 `application/Inc/system/board_cfg.h` 的 18 个主键和 4 个辅助键坐标逐项核对。辅助键为 19、20、21、Fn；2 号键半径更大。
- 使用固件 `BOARD_WIDTH=310.2 mm` 为模型建立物理尺度；底部导光层厚度独立验证为 3 mm。
- 原始数据、来源哈希保存在 `layout-source.json`；模型坐标保存在 `model-layout.json`；俯视图为 `layout-top-check.png`。

变形对应：十块面板组成 X / R / A；11–18 号键组成 O 的八段外圈，2 号大键组成 O 中央圆面；其余按键收回，底壳向后折叠。末尾保留金属 Logo 扫光，再淡出循环。

## 文件与复现

- `15-xora-leverless-rgb.gif`：320 × 172，120 帧，10 秒，无限循环。GIF 的 80 / 80 / 90 ms 帧延时实现平均精确 12 FPS。
- `15-xora-leverless-rgb-master.mp4`：640 × 344，12 FPS，10 秒，无音频。
- `15-xora-leverless-rgb.blend`：完整关键帧与材质的三维场景。
- `index.html`：只播放已渲染视频与 GIF 的本地预览页面。

仓库根目录运行 `python output/xora-leverless-transform/extract_layout.py`，再运行 `python output/xora-leverless-transform/run.py final`，最后运行 `python output/xora-leverless-transform/finish.py`。渲染上限 900 秒；各编码与验证阶段上限 120 秒，分别保存日志与验证 JSON。

验证覆盖布局一致性、22 个实际模型坐标、两秒完整展示、3 mm 导光层、物件变形终态、GIF 全帧解码与时序、WebConfig 实际解码及 12 FPS 采样、JPEG payload 契约、视频尺寸与帧率。JPEG 容量为本地 sharp 质量 82 估算，浏览器编码可能不同。未访问设备或进行实机验收。

所有产物只保存在本目录，未上传、发布或替换设备默认待机画面。原 Logo 使用仓库 `application/www/public/images/xora-mono-slate.svg`；之前各版本及生产代码保持原样。
