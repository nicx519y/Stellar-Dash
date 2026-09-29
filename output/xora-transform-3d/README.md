# XORA 机械变形片头

延续已选用的 Blender / Cycles 三维制作标准，以冷银金属与蓝色逆光呈现机械变形。18 块实体装甲板、8 段 O 圆环、旋转核心、伸缩骨架、关节与连杆依序运动，最终归位为原 XORA 轮廓。

节奏：核心点亮 → 骨架伸展 → 装甲翻转与圆环闭合 → 依次锁定 → 扫光定格 → 暗场循环。

- GIF：`13-mechanical-transformation.gif`，320 × 172、120 帧、10 秒，无限循环。80 / 80 / 90 ms 的 GIF 延时循环实现平均精确 12 FPS。
- 视频：`13-mechanical-transformation-master.mp4`，640 × 344、12 FPS、10 秒，无音频。
- 预览：`index.html`，大尺寸视频与原始尺寸 GIF。
- 三维源文件：`13-mechanical-transformation.blend`，所有 120 帧的运动已烘焙为关键帧，可直接在时间轴播放。程序化几何、灯光与调度见 `scene.py`。

字形源：`../../application/www/public/images/xora-mono-slate.svg`。`base_scene.py` 是上一批已接受的本地三维场景的独立副本；未修改上一批结果。全部处理在本地完成，无上传、发布、设备访问或默认待机图替换。

验证：逐帧 GIF 解码和尺寸/帧数/延时/循环检查、WebConfig 实际 gifuct-js 解码与采样函数、JPEG payload 契约，以及视频解码器尺寸/帧数/帧率检查。JPEG 容量采用本地 sharp 质量 82 估算，浏览器编码可能不同。未实机验收。

复现（仓库根目录）：

1. `python output/xora-transform-3d/run.py final`
2. `python output/xora-transform-3d/encode.py`
3. `node output/xora-transform-3d/verify.cjs`
4. 使用本机 Blender 后台运行 `export_video.py` 与 `check_video.py`。

Blender 使用本机已安装版本 5.0.1、Cycles / OptiX、48 samples；使用现有 RTX GPU 和仓库/本机依赖。输出帧目录为 `frames/13-mechanical-transformation/`。
