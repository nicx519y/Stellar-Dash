# XORA 赛博装甲 · 高饱和蓝绿灯效变形版

完整的无摇杆控制器展示约两秒，然后装甲面板与按键翻转、伸展组成 XORA Logo。

- 黑色阳极氧化铝合金板面只有两条 45° 直斜缝，分别位于左下、右下角；中间和按键区域保留大块完整平面。三块外层面板在变形启动时收回，其下的十个内部部件展开组成 X / R / A。去掉发光装饰线、刻度和碎分缝。
- 22 颗按键保持 WebConfig 位置与尺寸，采用微凸烟灰半透明外壳，每键七道内部同心圆；内部发光保持低亮度 0.17。
- 按键与 3 mm 整圈底部灯带采用高饱和蓝色 #0014FF / 绿色 #00FF08。直接采样 WebConfig `Transform / 质变` 算法，每轮 2 秒，从左向右扫过后保持新颜色，下一轮再换回。
- 完全移除全局与局部体积雾；没有独立点光源或射灯。采用清晰、暗色背景和金属反光，加强赛博与机甲风格。


## 布局依据

- WebConfig 的 `application/www/lib/device-transport/mock-device-transport.ts` 中 `HITBOX_LAYOUT` 提供全部 22 个位置和半径；`hitbox-constants.ts` 提供 787 × 489 板面与 2.55 坐标缩放，半径不缩放。
- 与 `application/Inc/system/board_cfg.h` 的 18 个主键和 4 个辅助键坐标逐项核对。辅助键为 19、20、21、Fn；2 号键半径更大。
- 使用固件 `BOARD_WIDTH=310.2 mm` 为模型建立物理尺度；底部导光层厚度独立验证为 3 mm。
- 原始数据、来源哈希保存在 `layout-source.json`；模型坐标保存在 `model-layout.json`；俯视图为 `layout-top-check.png`。

变形对应：十个内部部件组成 X / R / A；11–18 号键组成 O 的八段外圈，2 号大键组成 O 中央圆面；其余按键收回，底壳向后折叠。末尾保留金属 Logo 扫光，再淡出循环。

## 文件与复现

- `17-xora-cyber-armor.gif`：320 × 172，120 帧，10 秒，无限循环。GIF 的 80 / 80 / 90 ms 帧延时实现平均精确 12 FPS。
- `17-xora-cyber-armor-master.mp4`：640 × 344，12 FPS，10 秒，无音频。
- `17-xora-cyber-armor.blend`：完整关键帧与材质的三维场景。
- `index.html`：只播放已渲染视频与 GIF 的本地预览页面。

仓库根目录运行 `python output/xora-cyber-armor/extract_layout.py`，运行 `node output/xora-cyber-armor/sample_transform.cjs` 从 WebConfig 算法采样灯效，再运行 `python output/xora-cyber-armor/run.py final`，最后运行 `python output/xora-cyber-armor/finish.py`。渲染上限 900 秒；各编码与验证阶段上限 120 秒，分别保存日志与验证 JSON。

验证覆盖布局一致性、22 个实际模型坐标、两秒完整展示、3 mm 导光层、22 个烟灰半透明弧面键帽与内部同心圆、全部按键与底部灯带的动态颜色、无体积雾与无独立射灯、物件变形终态、GIF 全帧解码与时序、WebConfig 实际解码及 12 FPS 采样、JPEG payload 契约、视频尺寸与帧率。JPEG 容量为本地 sharp 质量 82 估算，浏览器编码可能不同。未访问设备或进行实机验收。

所有产物只保存在本目录，未上传、发布或替换设备默认待机画面。原 Logo 使用仓库 `application/www/public/images/xora-mono-slate.svg`；之前各版本及生产代码保持原样。

灯效源文件为 `application/www/components/hitbox/hitbox-animation.ts`，源码哈希与 120 帧、22 键的实际算法输出保存在 `transform-samples.json`。按键与氛围灯共用覆盖整块板面的横向边界，便于同步扫色。

`material-detail.png` 为高分辨率材质近景，可查看弧面、烟灰半透明质感和内部同心圆；最终屏幕 GIF 仍为 320 × 172。

分缝视觉参考：[装甲刻线示例](https://lightindustries.ca/cdn/shop/products/masterguide07.jpg?v=1571068324)。本版仅采用少量硬直斜切语言，不复制图案。
